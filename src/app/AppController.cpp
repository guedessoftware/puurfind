#include "app/AppController.h"
#include "core/Config.h"

#include <QClipboard>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDesktopServices>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMimeDatabase>
#include <QUrl>
#include <QtConcurrentRun>

namespace purrfind {
namespace {

QDBusPendingCall indexerCall(const QString &method, const QVariantList &arguments = {})
{
    QDBusMessage message = QDBusMessage::createMethodCall(
        "org.purrfind.Indexer", "/org/purrfind/Indexer", "org.purrfind.Indexer1", method);
    message.setArguments(arguments);
    return QDBusConnection::sessionBus().asyncCall(message);
}

bool isImage(const FileRecord &file)
{
    static const QStringList extensions{
        "jpg", "jpeg", "png", "gif", "webp", "tif", "tiff", "bmp", "avif", "heic", "heif"
    };
    return file.type.startsWith("image/") || extensions.contains(file.extension);
}

bool isDocument(const FileRecord &file)
{
    static const QStringList extensions{
        "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx",
        "odt", "ods", "odp", "txt", "md", "markdown"
    };
    return extensions.contains(file.extension);
}

bool matchesCategory(const FileRecord &file, const QString &category)
{
    if (category.isEmpty()) return true;
    if (category == "kind:file") return !file.directory;
    if (category == "kind:folder") return file.directory;
    if (category == "category:image") return !file.directory && isImage(file);
    if (category == "category:document") return !file.directory && isDocument(file);
    if (category == "category:video") return !file.directory && file.type.startsWith("video/");
    if (category == "category:other")
        return !file.directory && !isImage(file) && !isDocument(file) && !file.type.startsWith("video/");
    return true;
}

QString friendlyIndexerError(const QDBusError &error)
{
    switch (error.type()) {
    case QDBusError::NoReply:
    case QDBusError::TimedOut:
    case QDBusError::Timeout:
        return QStringLiteral("O indexador demorou para responder. Aguarde a indexação ou reinicie o serviço nas Configurações.");
    case QDBusError::ServiceUnknown:
    case QDBusError::NoServer:
    case QDBusError::Disconnected:
        return QStringLiteral("O indexador não está disponível. Abra as Configurações para verificar ou reconstruir o índice.");
    case QDBusError::AccessDenied:
        return QStringLiteral("O indexador não tem permissão para concluir esta operação.");
    default:
        return QStringLiteral("Não foi possível comunicar com o indexador. Tente novamente ou reconstrua o índice nas Configurações.");
    }
}

} // namespace

AppController::AppController(std::shared_ptr<PreviewCache> previewCache, QObject *parent)
    : QObject(parent), previewCache_(std::move(previewCache)), previewRegistry_(previewCache_)
{
    previewPool_.setMaxThreadCount(1);
    previewPool_.setThreadPriority(QThread::NormalPriority);
    statusTimer_.setSingleShot(true);
    statusTimer_.setInterval(5000);
    connect(&statusTimer_, &QTimer::timeout, this, &AppController::refreshStatus);
    QDBusConnection::sessionBus().connect("org.purrfind.Indexer", "/org/purrfind/Indexer",
        "org.purrfind.Indexer1", "StatusChanged", this, SLOT(applyStatus(QString)));
    previewTimer_.setSingleShot(true);
    previewTimer_.setInterval(110);
    connect(&previewTimer_, &QTimer::timeout, this, &AppController::requestPreview);
    refreshStatus();
    loadConfig();
}

void AppController::setError(const QString &value)
{
    if (error_ == value) return;
    error_ = value;
    emit errorChanged();
}

void AppController::search(const QString &text, const QString &category)
{
    ++searchGeneration_;
    QString query = text.trimmed();
    currentQuery_ = query + (category.isEmpty() ? QString() : " " + category);
    if (query.isEmpty()) {
        pendingQuery_.clear();
        searchPending_ = false;
        model_.setResults({});
        setError({});
        lastSearchMilliseconds_ = 0;
        categoryCounts_ = {0, 0, 0, 0, 0, 0, 0};
        emit categoryCountsChanged();
        if (!searching_) emit searchingChanged();
        return;
    }
    pendingQuery_ = query;
    pendingCategory_ = category;
    searchPending_ = true;
    if (!searching_) dispatchSearch();
}

void AppController::dispatchSearch()
{
    if (!searchPending_) return;
    const QString query = pendingQuery_;
    const QString category = pendingCategory_;
    searchPending_ = false;
    const quint64 generation = searchGeneration_;
    searching_ = true;
    searchTimer_.restart();
    emit searchingChanged();
    auto *watcher = new QDBusPendingCallWatcher(indexerCall("Search", {query, 200}), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
        [this, watcher, generation, category](QDBusPendingCallWatcher *) {
            QDBusPendingReply<QString> reply = *watcher;
            watcher->deleteLater();
            const bool current = generation == searchGeneration_;
            if (!current) {
                if (searchPending_) dispatchSearch();
                else { searching_ = false; emit searchingChanged(); }
                return;
            }
            searching_ = false;
            lastSearchMilliseconds_ = searchTimer_.isValid() ? searchTimer_.elapsed() : 0;
            emit searchingChanged();
            if (reply.isError()) {
                model_.setResults({});
                setError(friendlyIndexerError(reply.error()));
                if (searchPending_) dispatchSearch();
                return;
            }
            QJsonParseError parseError;
            const auto document = QJsonDocument::fromJson(reply.value().toUtf8(), &parseError);
            if (parseError.error != QJsonParseError::NoError) {
                setError(parseError.errorString());
                if (searchPending_) dispatchSearch();
                return;
            }
            QVector<SearchResult> entries;
            const auto response = document.object();
            for (const auto &value : response.value("results").toArray()) {
                const auto object = value.toObject();
                SearchResult result;
                result.file.id = object.value("id").toInteger();
                result.file.name = object.value("name").toString();
                result.file.path = object.value("path").toString();
                result.file.parentPath = object.value("parentPath").toString();
                result.file.extension = object.value("extension").toString();
                result.file.type = object.value("type").toString();
                result.file.size = object.value("size").toInteger();
                result.file.mtime = object.value("mtime").toInteger();
                result.file.ctime = object.value("ctime").toInteger();
                result.file.inode = object.value("inode").toInteger();
                result.file.device = object.value("device").toInteger();
                result.file.directory = object.value("directory").toBool();
                result.file.symlink = object.value("symlink").toBool();
                result.file.hidden = object.value("hidden").toBool();
                result.score = object.value("score").toDouble();
                result.snippet = object.value("snippet").toString();
                result.matchOrigin = object.value("matchOrigin").toString();
                result.documentTitle = object.value("documentTitle").toString();
                result.documentAuthor = object.value("documentAuthor").toString();
                result.pageCount = object.value("pageCount").toInt();
                result.matchPage = object.value("matchPage").toInt();
                result.cameraMake = object.value("cameraMake").toString();
                result.cameraModel = object.value("cameraModel").toString();
                result.imageWidth = object.value("imageWidth").toInt();
                result.imageHeight = object.value("imageHeight").toInt();
                result.dateTaken = object.value("dateTaken").toInteger();
                result.scoreExplanation = object.value("scoreExplanation").toString();
                entries.append(std::move(result));
            }
            qint64 files = 0, folders = 0, images = 0, documents = 0, videos = 0, others = 0;
            QVector<SearchResult> visibleEntries;
            visibleEntries.reserve(entries.size());
            for (const auto &entry : entries) {
                if (entry.file.directory) ++folders;
                else {
                    ++files;
                    if (isImage(entry.file)) ++images;
                    else if (entry.file.type.startsWith("video/")) ++videos;
                    else if (isDocument(entry.file)) ++documents;
                    else ++others;
                }
                if (matchesCategory(entry.file, category)) visibleEntries.append(entry);
            }
            categoryCounts_ = {entries.size(), files, folders, images, documents, videos, others};
            emit categoryCountsChanged();
            model_.setResults(std::move(visibleEntries));
            setError(response.value("error").toString());
            if (searchPending_) dispatchSearch();
        });
}

void AppController::open(int row)
{
    if (const auto *result = model_.at(row)) {
        if (QDesktopServices::openUrl(QUrl::fromLocalFile(result->file.path)))
            indexerCall("RecordOpen", {result->file.id});
    }
}

void AppController::reveal(int row)
{
    if (const auto *result = model_.at(row))
        QDesktopServices::openUrl(QUrl::fromLocalFile(result->file.directory
            ? result->file.path : result->file.parentPath));
}

void AppController::copyPath(int row)
{
    if (const auto *result = model_.at(row)) QGuiApplication::clipboard()->setText(result->file.path);
}

void AppController::select(int row)
{
    selectedRow_ = row;
    requestedPage_ = 0;
    ++previewGeneration_;
    if (previewCancellation_) previewCancellation_->store(true);
    previewTimer_.stop();
    previewText_.clear();
    previewImageUrl_.clear();
    previewDetails_.clear();
    previewPage_ = 0;
    previewPageCount_ = 0;
    previewLoading_ = false;
    const auto *result = model_.at(row);
    previewTitle_ = result ? result->file.name : QString();
    if (result) {
        previewDetails_ = QString("%1\n%2")
            .arg(result->file.type.isEmpty() ? (result->file.directory ? "Folder" : "File") : result->file.type,
                 result->file.directory ? QStringLiteral("—") : QLocale().formattedDataSize(result->file.size));
        previewText_ = result->snippet;
        if (previewEnabled_) {
            previewLoading_ = true;
            previewTimer_.start();
        }
    }
    emit previewChanged();
}

void AppController::requestPreview()
{
    const auto *selected = model_.at(selectedRow_);
    if (!selected || !previewEnabled_) return;
    PreviewRequest request;
    request.file = selected->file;
    request.targetSize = QSize(640, 800);
    request.query = currentQuery_;
    request.snippet = selected->snippet;
    request.pageCount = selected->pageCount;
    request.page = requestedPage_ > 0 ? requestedPage_ : (selected->matchPage > 0 ? selected->matchPage : 1);
    const QString key = previewCache_->keyFor(request);
    const quint64 generation = ++previewGeneration_;
    if (previewCancellation_) previewCancellation_->store(true);
    previewCancellation_ = std::make_shared<std::atomic_bool>(false);
    const auto cancellation = previewCancellation_;
    PreviewRegistry registry = previewRegistry_;
    auto *watcher = new QFutureWatcher<PreviewResult>(this);
    connect(watcher, &QFutureWatcher<PreviewResult>::finished, this,
        [this, watcher, generation, key, cancellation] {
            const PreviewResult result = watcher->result();
            watcher->deleteLater();
            if (generation != previewGeneration_ || cancellation->load()) return;
            previewLoading_ = false;
            previewTitle_ = result.title;
            previewText_ = result.text;
            previewDetails_ = result.details;
            if (!result.error.isEmpty() && result.error != "cancelled") {
                if (!previewDetails_.isEmpty()) previewDetails_ += '\n';
                previewDetails_ += "Preview unavailable: " + result.error;
            }
            previewPage_ = result.page;
            previewPageCount_ = result.pageCount;
            previewImageUrl_ = result.image.isNull() ? QString() : "image://purrfind-preview/preview/" + key;
            emit previewChanged();
        });
    watcher->setFuture(QtConcurrent::run(&previewPool_, [registry, request, cancellation] {
        return registry.generate(request, CancellationToken(cancellation.get()));
    }));
}

QString AppController::properties(int row) const
{
    const auto *result = model_.at(row);
    if (!result) return {};
    const auto &file = result->file;
    const QString kind = file.directory ? "Folder" : (file.type.isEmpty() ? "File" : file.type);
    const QString size = file.directory ? QStringLiteral("—") : QLocale().formattedDataSize(file.size);
    const QString modified = QLocale().toString(QDateTime::fromSecsSinceEpoch(file.mtime), QLocale::LongFormat);
    const QFileInfo info(file.path);
    QString properties = QString("Name: %1\nPath: %2\nType: %3\nSize: %4\nModified: %5\nCreated/status changed: %6\nOwner: %7\nPermissions: %8\nInode: %9\nDevice: %10\nHidden: %11\nSymlink: %12")
        .arg(file.name, file.path, kind, size, modified,
             QLocale().toString(QDateTime::fromSecsSinceEpoch(file.ctime), QLocale::LongFormat),
             info.owner(), QString::number(static_cast<int>(info.permissions()), 8))
        .arg(file.inode).arg(file.device)
        .arg(file.hidden ? QStringLiteral("yes") : QStringLiteral("no"),
             file.symlink ? QStringLiteral("yes") : QStringLiteral("no"));
    if (!result->documentTitle.isEmpty()) properties += "\nTitle: " + result->documentTitle;
    if (!result->documentAuthor.isEmpty()) properties += "\nAuthor: " + result->documentAuthor;
    if (result->pageCount > 0) properties += QString("\nPages: %1").arg(result->pageCount);
    const QString camera = (result->cameraMake + ' ' + result->cameraModel).simplified();
    if (!camera.isEmpty()) properties += "\nCamera: " + camera;
    if (result->imageWidth > 0) properties += QString("\nDimensions: %1 × %2").arg(result->imageWidth).arg(result->imageHeight);
    if (qEnvironmentVariableIsSet("PURRFIND_SCORE_DEBUG") && !result->scoreExplanation.isEmpty())
        properties += "\n\nRanking debug: " + result->scoreExplanation;
    return properties;
}

void AppController::refreshStatus()
{
    auto *watcher = new QDBusPendingCallWatcher(indexerCall("Status"), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError()) {
            statusText_ = "Indexer offline";
            emit statusChanged();
            statusTimer_.start();
            return;
        }
        applyStatus(reply.value());
    });
}

void AppController::applyStatus(const QString &json)
{
        statusTimer_.stop();
        statusJson_ = json;
        const auto object = QJsonDocument::fromJson(json.toUtf8()).object();
        const qint64 count = object.value("indexed").toInteger();
        const QString state = object.value("state").toString();
        statusText_ = state == "indexing" ? QString("Indexing · %1 items").arg(count)
            : state == "rebuilding" ? QString("Rebuilding index · %1 items").arg(count)
            : state == "error" ? QStringLiteral("Index error")
            : QString("Ready · %1 items · Content %2 indexed / %3 pending%4")
                .arg(count).arg(object.value("contentIndexed").toInteger())
                .arg(object.value("contentPending").toInteger())
                .arg(object.value("contentPaused").toBool() ? " · paused" : "");
        if (object.value("ocrActive").toInteger() > 0) statusText_ += " · OCR in background";
        emit statusChanged();
}

void AppController::loadConfig()
{
    auto *watcher = new QDBusPendingCallWatcher(indexerCall("GetConfig"), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (!reply.isError()) {
            configJson_ = reply.value();
            const auto config = QJsonDocument::fromJson(configJson_.toUtf8()).object();
            const bool enabled = config.value("previewAutomatically").toBool(true);
            if (previewEnabled_ != enabled) { previewEnabled_ = enabled; emit previewEnabledChanged(); }
            emit configChanged();
        }
    });
}

bool AppController::saveConfig(const QString &json)
{
    ConfigData validated;
    QString validationError;
    if (!Config::fromJson(json, &validated, &validationError)) {
        setError(validationError);
        return false;
    }
    configJson_ = Config::toJson(validated);
    const auto config = QJsonDocument::fromJson(configJson_.toUtf8()).object();
    const bool enabled = config.value("previewAutomatically").toBool(true);
    if (previewEnabled_ != enabled) { previewEnabled_ = enabled; emit previewEnabledChanged(); }
    emit configChanged();
    auto *watcher = new QDBusPendingCallWatcher(indexerCall("SetConfig", {configJson_}), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        QDBusPendingReply<bool> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError() || !reply.value())
            setError(reply.isError() ? friendlyIndexerError(reply.error())
                                     : QStringLiteral("O indexador rejeitou a configuração. Revise os valores e tente novamente."));
        else setError({});
    });
    return true;
}

void AppController::reindex() { indexerCall("Reindex"); }
void AppController::pauseContent() { indexerCall("PauseContentIndexing"); refreshStatus(); }
void AppController::resumeContent() { indexerCall("ResumeContentIndexing"); refreshStatus(); }
void AppController::reindexContent() { indexerCall("ReindexContent"); refreshStatus(); }
void AppController::reindexMetadata() { indexerCall("ReindexMetadata"); refreshStatus(); }

void AppController::navigatePreview(int delta)
{
    if (previewPageCount_ <= 0) return;
    requestedPage_ = qBound(1, previewPage_ + delta, previewPageCount_);
    previewLoading_ = true;
    emit previewChanged();
    requestPreview();
}

void AppController::togglePreview()
{
    previewEnabled_ = !previewEnabled_;
    if (!previewEnabled_) {
        if (previewCancellation_) previewCancellation_->store(true);
        previewTimer_.stop();
        previewLoading_ = false;
    } else if (selectedRow_ >= 0) {
        previewLoading_ = true;
        previewTimer_.start();
    }
    emit previewEnabledChanged();
    emit previewChanged();
}

void AppController::clearPreviewCache()
{
    if (previewCancellation_) previewCancellation_->store(true);
    previewCache_->clear();
    previewImageUrl_.clear();
    emit previewChanged();
}

void AppController::clearUsageHistory() { indexerCall("ClearUsageHistory"); }
void AppController::pauseOcr() { indexerCall("PauseOcr"); refreshStatus(); }
void AppController::resumeOcr() { indexerCall("ResumeOcr"); refreshStatus(); }
void AppController::reindexOcr() { indexerCall("ReindexOcr"); refreshStatus(); }
void AppController::rebuildIndex() { indexerCall("RebuildIndex"); refreshStatus(); }

QString AppController::iconUrl(const QString &mimeType, bool directory) const
{
    const QString mime = directory ? "inode/directory" : (mimeType.isEmpty() ? "application/octet-stream" : mimeType);
    return "image://purrfind-preview/icon/" + QString::fromLatin1(QUrl::toPercentEncoding(mime));
}

} // namespace purrfind
