#include "indexer/IndexerService.h"

#include "core/FileSystem.h"
#include "core/Logging.h"
#include "core/SearchEngine.h"
#include "indexer/Crawler.h"
#include "ocr/OcrLanguageManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QSaveFile>
#include <QTimer>

namespace purrfind {
namespace {

constexpr int RecoveryBackupLimit = 3;

void pruneRecoveryBackups()
{
    QDir recoveryRoot(Config::dataDirectory() + "/recovery");
    const QFileInfoList backups = recoveryRoot.entryInfoList(
        {"index-*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (int index = RecoveryBackupLimit; index < backups.size(); ++index)
        QDir(backups.at(index).absoluteFilePath()).removeRecursively();
}

} // namespace

IndexerService::IndexerService(QObject *parent)
    : QObject(parent), watcher_(this), queue_(this), contentQueue_(this), metadataQueue_(this), ocrQueue_(this)
{
    connect(&watcher_, &InotifyWatcher::eventReceived, &queue_, &EventQueue::enqueue);
    connect(&watcher_, &InotifyWatcher::warning, this, [this](const QString &message) {
        qCWarning(logIndexer).noquote() << message;
        status_.lastError = message;
    });
    connect(&queue_, &EventQueue::ready, this, &IndexerService::processEvents);
    connect(&contentQueue_, &ContentIndexQueue::progressChanged, this, [this] {
        emit StatusChanged(statusJson());
        emit IndexChanged();
    });
    connect(&metadataQueue_, &MetadataQueue::progressChanged, this, [this] {
        emit StatusChanged(statusJson());
        emit IndexChanged();
    });
    connect(&ocrQueue_, &OcrQueue::progressChanged, this, [this] {
        emit StatusChanged(statusJson());
        emit IndexChanged();
    });
}

IndexerService::~IndexerService()
{
    cancelCrawl_.store(true);
    if (crawlerThread_.joinable()) crawlerThread_.join();
    watcher_.stop();
    contentQueue_.stop();
    metadataQueue_.stop();
    ocrQueue_.stop();
    database_.checkpointWal(nullptr);
}

bool IndexerService::preserveAndCreateDatabase(const QString &reason, QString *error)
{
    database_.close();
    const QString source = Config::databasePath();
    const QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz");
    const QString recoveryDir = Config::dataDirectory() + "/recovery/index-" + stamp;
    if (!QDir().mkpath(recoveryDir)) {
        if (error) *error = "Cannot create the index recovery directory";
        return false;
    }
    QFile::setPermissions(Config::dataDirectory() + "/recovery",
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QFile::setPermissions(recoveryDir,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    bool preserved = false;
    for (const QString &suffix : QStringList{"", "-wal", "-shm"}) {
        const QString candidate = source + suffix;
        if (!QFileInfo::exists(candidate)) continue;
        if (!QFile::rename(candidate, recoveryDir + "/index.sqlite3" + suffix)) {
            if (error) *error = "Cannot preserve the damaged index";
            return false;
        }
        preserved = true;
    }
    recoveryBackup_ = preserved ? recoveryDir : QString();
    if (!preserved) QDir(recoveryDir).removeRecursively();
    if (!database_.open(source, false, error) || !database_.migrate(error)) return false;
    pruneRecoveryBackups();
    status_.state = "rebuilding";
    status_.lastError = reason;
    return true;
}

bool IndexerService::openOrRecoverDatabase(QString *error)
{
    const QString path = Config::databasePath();
    if (!database_.open(path, false, error)) {
        if (!QFileInfo::exists(path)) return false;
        return preserveAndCreateDatabase("The search index could not be opened and is being rebuilt", error);
    }
    if (!database_.migrate(error)) {
        if (error && error->contains("newer than this application")) return false;
        const QString reason = "The search index could not be opened and is being rebuilt";
        return preserveAndCreateDatabase(reason, error);
    }
    const QString marker = Config::dataDirectory() + "/last-quick-check";
    const QFileInfo markerInfo(marker);
    const bool due = !markerInfo.exists()
        || markerInfo.lastModified().daysTo(QDateTime::currentDateTime()) >= 7;
    if (due) {
        QString integrityError;
        if (!database_.quickCheck(&integrityError))
            return preserveAndCreateDatabase("The search index failed an integrity check and is being rebuilt", error);
        QSaveFile checked(marker);
        if (checked.open(QIODevice::WriteOnly)) {
            checked.write("ok\n");
            if (checked.commit()) QFile::setPermissions(marker, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        }
    }
    return true;
}

bool IndexerService::initialize(QString *error)
{
    config_ = Config::load(error);
    if (!openOrRecoverDatabase(error)) return false;
    contentQueue_.start(config_);
    metadataQueue_.start(config_);
    ocrQueue_.start(config_);
    QString watchError;
    if (!watcher_.start(config_.includedPaths, config_.excludedPaths, &watchError,
                        config_.excludeHidden)) {
        qCWarning(logIndexer).noquote() << watchError;
        status_.lastError = watchError;
    }
    QMetaObject::invokeMethod(this, &IndexerService::startCrawl, Qt::QueuedConnection);
    return true;
}

QString IndexerService::rootFor(const QString &path) const
{
    QString best;
    for (const auto &root : config_.includedPaths) {
        if (FileSystem::isWithin(path, root) && root.size() > best.size()) best = root;
    }
    return best;
}

void IndexerService::startCrawl()
{
    if (crawling_.exchange(true)) return;
    if (crawlerThread_.joinable()) crawlerThread_.join();
    cancelCrawl_.store(false);
    generation_ = QDateTime::currentMSecsSinceEpoch();
    status_.state = "indexing";
    status_.indexed = 0;
    status_.currentPath.clear();
    status_.lastError.clear();
    emit StatusChanged(statusJson());

    const auto paths = config_.includedPaths;
    const auto exclusions = config_.excludedPaths;
    const bool excludeHidden = config_.excludeHidden;
    const auto databasePath = Config::databasePath();
    const auto generation = generation_;
    crawlerThread_ = std::thread([this, paths, exclusions, databasePath, generation, excludeHidden] {
        qint64 total = 0;
        QStringList errors;
        for (const auto &root : paths) {
            if (cancelCrawl_.load()) break;
            const auto result = Crawler::crawl(databasePath, root, exclusions, generation,
                [this, total, generation](qint64 indexed, const QString &path) {
                    QMetaObject::invokeMethod(this, [this, indexed, total, path, generation] {
                        if (generation != generation_) return;
                        status_.indexed = total + indexed;
                        status_.currentPath = path;
                        emit StatusChanged(statusJson());
                        contentQueue_.notifyWork();
                        metadataQueue_.notifyWork();
                        ocrQueue_.notifyWork();
                    }, Qt::QueuedConnection);
                }, &cancelCrawl_, excludeHidden);
            total += result.indexed;
            if (!result.error.isEmpty()) errors.append(result.error);
        }
        QMetaObject::invokeMethod(this, [this, total, errors, generation] {
            if (generation != generation_) return;
            status_.indexed = database_.fileCount(nullptr);
            if (status_.indexed < 0) status_.indexed = total;
            status_.lastUpdate = QDateTime::currentSecsSinceEpoch();
            status_.state = cancelCrawl_.load() ? "idle" : "idle";
            status_.currentPath.clear();
            status_.lastError = errors.join("; ");
            crawling_.store(false);
            emit StatusChanged(statusJson());
            emit IndexChanged();
            contentQueue_.notifyWork();
            metadataQueue_.notifyWork();
            ocrQueue_.notifyWork();
        }, Qt::QueuedConnection);
    });
}

void IndexerService::processEvents(const QVector<FsEvent> &events)
{
    bool reconcile = false;
    QString error;
    if (!database_.begin(&error)) {
        qCWarning(logDatabase).noquote() << error;
        return;
    }
    bool ok = true;
    for (const auto &event : events) {
        if (event.kind == EventKind::Reconcile) {
            reconcile = true;
            continue;
        }
        if (event.kind == EventKind::Rename) {
            contentQueue_.invalidate(event.oldPath);
            metadataQueue_.invalidate(event.oldPath);
            ocrQueue_.invalidate(event.oldPath);
            const QString root = rootFor(event.path);
            if (root.isEmpty() || FileSystem::isExcluded(event.path, config_.excludedPaths)
                || (config_.excludeHidden && FileSystem::isHiddenWithin(event.path, root))) {
                ok = database_.removePath(event.oldPath, event.directory, &error);
            } else if (auto record = FileSystem::inspect(event.path, root, generation_, &error))
                ok = database_.movePathPreservingContent(event.oldPath, *record, event.directory, &error);
            else
                ok = database_.removePath(event.oldPath, event.directory, &error);
        } else if (event.kind == EventKind::Remove) {
            contentQueue_.invalidate(event.path);
            metadataQueue_.invalidate(event.path);
            ocrQueue_.invalidate(event.path);
            ok = database_.removePath(event.path, event.directory, &error);
        } else {
            contentQueue_.invalidate(event.path);
            metadataQueue_.invalidate(event.path);
            ocrQueue_.invalidate(event.path);
            const QString root = rootFor(event.path);
            if (root.isEmpty() || FileSystem::isExcluded(event.path, config_.excludedPaths)
                || (config_.excludeHidden && FileSystem::isHiddenWithin(event.path, root))) continue;
            if (auto record = FileSystem::inspect(event.path, root, generation_, &error))
                ok = database_.upsert(*record, &error);
            else
                ok = database_.removePath(event.path, event.directory, &error);

            // A directory moved in from outside a watched tree may already
            // contain files. Reconcile on the crawler thread instead of
            // recursively walking it while D-Bus requests are waiting.
            if (ok && event.kind == EventKind::Upsert && event.directory
                && QFileInfo(event.path).isDir()) reconcile = true;
        }
        if (!ok) break;
    }
    if (ok) ok = database_.commit(&error);
    else database_.rollback();
    if (!ok) {
        status_.lastError = error;
        qCWarning(logDatabase).noquote() << error;
    } else if (!events.isEmpty()) {
        status_.lastUpdate = QDateTime::currentSecsSinceEpoch();
        status_.indexed = database_.fileCount(nullptr);
        emit IndexChanged();
        emit StatusChanged(statusJson());
        contentQueue_.notifyWork();
        metadataQueue_.notifyWork();
        ocrQueue_.notifyWork();
    }
    if (reconcile) startCrawl();
}

QString IndexerService::Search(const QString &query, int limit)
{
    SearchEngine engine(database_, config_.usageRankingEnabled);
    QString error;
    const auto results = engine.search(query, qBound(1, limit, config_.maxResults),
                                       config_.showHidden, &error);
    QJsonArray array;
    for (const auto &result : results) {
        const auto &file = result.file;
        array.append(QJsonObject{
            {"id", file.id}, {"name", file.name}, {"path", file.path},
            {"parentPath", file.parentPath}, {"extension", file.extension},
            {"type", file.type}, {"size", file.size}, {"mtime", file.mtime},
            {"ctime", file.ctime}, {"inode", static_cast<qint64>(file.inode)},
            {"device", static_cast<qint64>(file.device)},
            {"directory", file.directory}, {"symlink", file.symlink},
            {"hidden", file.hidden}, {"score", result.score},
            {"snippet", result.snippet}, {"matchOrigin", result.matchOrigin},
            {"documentTitle", result.documentTitle}, {"documentAuthor", result.documentAuthor},
            {"pageCount", result.pageCount}, {"matchPage", result.matchPage},
            {"cameraMake", result.cameraMake}, {"cameraModel", result.cameraModel},
            {"imageWidth", result.imageWidth}, {"imageHeight", result.imageHeight},
            {"dateTaken", result.dateTaken}, {"scoreExplanation", result.scoreExplanation},
        });
    }
    if (!error.isEmpty()) qCWarning(logDatabase).noquote() << "search:" << error;
    QJsonObject response{{"results", array},
                         {"error", error.isEmpty() ? QString() : QStringLiteral("Search index is temporarily unavailable")}};
    return QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact));
}

QString IndexerService::statusJson() const
{
    const auto content = database_.contentStateCounts(nullptr);
    const auto metadata = database_.metadataStateCounts(nullptr);
    const auto ocr = database_.ocrStateCounts(nullptr);
    QStringList availableOcrLanguages;
#ifdef PURRFIND_WITH_OCR
    availableOcrLanguages = OcrLanguageManager::availableLanguages();
#endif
    QStringList missingOcrLanguages;
    for (const auto &language : config_.ocrLanguages)
        if (!availableOcrLanguages.contains(language)) missingOcrLanguages.append(language);
    const qint64 indexedContent = content.value(ContentState::Indexed);
    const qint64 pendingContent = content.value(ContentState::NotIndexed) + content.value(ContentState::Queued)
        + content.value(ContentState::Indexing);
    const qint64 detectedOcr = ocr.value(OcrState::Pending) + ocr.value(OcrState::Queued)
        + ocr.value(OcrState::Processing) + ocr.value(OcrState::Indexed)
        + ocr.value(OcrState::NoText) + ocr.value(OcrState::Failed)
        + ocr.value(OcrState::Unsupported) + ocr.value(OcrState::Skipped)
        + ocr.value(OcrState::Paused);
    const QJsonObject object{
        {"state", status_.state}, {"currentPath", status_.currentPath},
        {"indexed", status_.indexed}, {"lastUpdate", status_.lastUpdate},
        {"lastError", status_.lastError}, {"databaseSize", database_.databaseSize()},
        {"watchCount", watcher_.watchCount()},
        {"watchLimitReached", watcher_.watchLimitReached()},
        {"systemWatchLimit", watcher_.systemWatchLimit()},
        {"recoveryBackup", recoveryBackup_},
        {"contentIndexed", indexedContent}, {"contentPending", pendingContent},
        {"contentFailed", content.value(ContentState::Failed)},
        {"contentNoText", content.value(ContentState::NoText)},
        {"contentUnsupported", content.value(ContentState::Unsupported)},
        {"contentPaused", contentQueue_.paused()},
        {"metadataIndexed", metadata.value(MetadataState::Indexed)},
        {"metadataPending", metadata.value(MetadataState::NotIndexed) + metadata.value(MetadataState::Queued)
                            + metadata.value(MetadataState::Indexing)},
        {"metadataFailed", metadata.value(MetadataState::Failed)},
        {"ocrAvailable",
#ifdef PURRFIND_WITH_OCR
            true
#else
            false
#endif
        },
        {"ocrAvailableLanguages", QJsonArray::fromStringList(availableOcrLanguages)},
        {"ocrMissingLanguages", QJsonArray::fromStringList(missingOcrLanguages)},
        {"ocrDetected", detectedOcr},
        {"ocrProcessed", ocr.value(OcrState::Indexed) + ocr.value(OcrState::NoText)},
        {"ocrPending", ocr.value(OcrState::Pending) + ocr.value(OcrState::Queued)
                       + ocr.value(OcrState::Processing) + ocr.value(OcrState::Paused)},
        {"ocrFailed", ocr.value(OcrState::Failed)},
        {"ocrActive", ocr.value(OcrState::Processing)},
        {"ocrPagesProcessed", database_.ocrPageCount(nullptr)},
        {"ocrPaused", ocrQueue_.paused()},
        {"ocrWaitReason", ocrQueue_.waitReason()},
    };
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString IndexerService::Status()
{
    status_.indexed = database_.fileCount(nullptr);
    return statusJson();
}

QString IndexerService::ContentMetrics()
{
    QString error;
    QJsonArray extractors;
    for (const auto &metric : database_.contentExtractorMetrics(&error)) {
        extractors.append(QJsonObject{
            {"extractor", metric.extractor}, {"documents", metric.documents},
            {"averageMilliseconds", metric.averageMilliseconds},
            {"p95Milliseconds", metric.p95Milliseconds},
            {"bytesProcessed", metric.bytesProcessed},
        });
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"extractors", extractors}, {"error", error}})
                                 .toJson(QJsonDocument::Compact));
}

QString IndexerService::GetConfig() { return Config::toJson(config_); }

bool IndexerService::SetConfig(const QString &json)
{
    ConfigData updated;
    QString error;
    if (!Config::fromJson(json, &updated, &error) || !Config::save(updated, &error)) {
        status_.lastError = error;
        return false;
    }
    pendingConfig_ = std::move(updated);
    cancelCrawl_.store(true);
    status_.state = "configuring";
    status_.currentPath.clear();
    emit StatusChanged(statusJson());
    if (!configApplyScheduled_) {
        configApplyScheduled_ = true;
        QTimer::singleShot(0, this, &IndexerService::applyPendingConfig);
    }
    return true;
}

void IndexerService::applyPendingConfig()
{
    if (!pendingConfig_) { configApplyScheduled_ = false; return; }
    if (crawling_.load()) {
        QTimer::singleShot(25, this, &IndexerService::applyPendingConfig);
        return;
    }
    if (crawlerThread_.joinable()) crawlerThread_.join();
    const QStringList previousRoots = config_.includedPaths;
    ConfigData updated = std::move(*pendingConfig_);
    pendingConfig_.reset();
    config_ = std::move(updated);
    for (const auto &oldRoot : previousRoots) {
        if (!config_.includedPaths.contains(oldRoot))
            database_.removePath(oldRoot, true, nullptr);
    }
    QString error;
    watcher_.start(config_.includedPaths, config_.excludedPaths, &error, config_.excludeHidden);
    contentQueue_.configure(config_);
    metadataQueue_.configure(config_);
    ocrQueue_.configure(config_);
    if (!error.isEmpty()) status_.lastError = error;
    emit ConfigApplied(Config::toJson(config_));
    configApplyScheduled_ = false;
    startCrawl();
}

bool IndexerService::Reindex()
{
    if (crawling_.load()) return false;
    startCrawl();
    return true;
}

bool IndexerService::PauseContentIndexing()
{
    config_.contentIndexingPaused = true;
    Config::save(config_, nullptr);
    contentQueue_.setPaused(true);
    emit StatusChanged(statusJson());
    return true;
}

bool IndexerService::ResumeContentIndexing()
{
    config_.contentIndexingPaused = false;
    Config::save(config_, nullptr);
    contentQueue_.setPaused(false);
    emit StatusChanged(statusJson());
    return true;
}

bool IndexerService::ReindexContent()
{
    contentQueue_.reindex();
    return true;
}

bool IndexerService::RecordOpen(qint64 fileId)
{
    return config_.usageRankingEnabled && database_.recordOpen(fileId, nullptr);
}

bool IndexerService::ClearUsageHistory()
{
    return database_.clearUsageHistory(nullptr);
}

bool IndexerService::ReindexMetadata()
{
    const bool ok = database_.resetImageMetadata(nullptr);
    if (ok) metadataQueue_.notifyWork();
    return ok;
}

bool IndexerService::PauseOcr()
{
    config_.ocrPaused = true;
    Config::save(config_, nullptr);
    ocrQueue_.configure(config_);
    emit StatusChanged(statusJson());
    return true;
}

bool IndexerService::ResumeOcr()
{
    config_.ocrPaused = false;
    Config::save(config_, nullptr);
    ocrQueue_.configure(config_);
    ocrQueue_.notifyWork();
    emit StatusChanged(statusJson());
    return true;
}

bool IndexerService::ReindexOcr()
{
    ocrQueue_.reindex();
    return true;
}

bool IndexerService::RebuildIndex()
{
    status_.state = "rebuilding";
    emit StatusChanged(statusJson());
    cancelCrawl_.store(true);
    if (crawlerThread_.joinable()) crawlerThread_.join();
    crawling_.store(false);
    watcher_.stop();
    contentQueue_.stop();
    metadataQueue_.stop();
    ocrQueue_.stop();
    QString error;
    if (!preserveAndCreateDatabase("Index rebuild requested by the user", &error)) {
        status_.state = "error";
        status_.lastError = error;
        emit StatusChanged(statusJson());
        return false;
    }
    contentQueue_.start(config_);
    metadataQueue_.start(config_);
    ocrQueue_.start(config_);
    if (!watcher_.start(config_.includedPaths, config_.excludedPaths, &error,
                        config_.excludeHidden))
        status_.lastError = error;
    startCrawl();
    return true;
}

} // namespace purrfind
