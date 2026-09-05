#include "ocr/OcrQueue.h"

#include "core/Database.h"
#include "core/FileSystem.h"
#include "core/Logging.h"
#include "ocr/OcrLanguageManager.h"
#include "ocr/OcrScheduler.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>
#include <sys/resource.h>

namespace purrfind {

OcrQueue::OcrQueue(QObject *parent) : QObject(parent) {}
OcrQueue::~OcrQueue() { stop(); }

void OcrQueue::start(const ConfigData &config)
{
    if (worker_.joinable()) return;
    config_ = config;
#ifdef PURRFIND_WITH_OCR
    paused_.store(config.ocrPaused);
    stopped_.store(false);
    wake_ = true;
    worker_ = std::thread(&OcrQueue::run, this);
#else
    paused_.store(true);
#endif
}

void OcrQueue::stop()
{
    stopped_.store(true);
    currentCancelled_.store(true);
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void OcrQueue::configure(const ConfigData &config)
{
    {
        std::lock_guard lock(mutex_);
        config_ = config;
        generation_.fetch_add(1);
        wake_ = true;
    }
    paused_.store(config.ocrPaused);
    currentCancelled_.store(true);
    condition_.notify_all();
}

void OcrQueue::notifyWork()
{
    { std::lock_guard lock(mutex_); wake_ = true; }
    condition_.notify_one();
}

void OcrQueue::invalidate(const QString &path)
{
    {
        std::lock_guard lock(mutex_);
        if (currentPath_ == path || (!currentPath_.isEmpty() && FileSystem::isWithin(currentPath_, path)))
            currentCancelled_.store(true);
        wake_ = true;
    }
    condition_.notify_one();
}

void OcrQueue::reindex()
{
    {
        std::lock_guard lock(mutex_);
        reindexRequested_ = true;
        generation_.fetch_add(1);
        wake_ = true;
    }
    currentCancelled_.store(true);
    condition_.notify_one();
}

QString OcrQueue::waitReason() const
{
    std::lock_guard lock(mutex_);
    return waitReason_;
}

bool OcrQueue::processFile(Database &database, const FileRecord &file,
                           const ConfigData &config, const QStringList &languages)
{
    if (FileSystem::isExcluded(file.path, config.ocrExcludedPaths)) {
        database.setOcrState(file.id, OcrState::Skipped, "excluded from OCR", nullptr);
        return true;
    }
    if (file.extension == "pdf" && file.size > config.ocrMaximumPdfBytes) {
        database.setOcrState(file.id, OcrState::Skipped, "PDF exceeds automatic OCR size limit", nullptr);
        return true;
    }
    const QString languageSpec = languages.join('+');
    QString reuseError;
    if (database.reuseOcrForHardlink(file, languageSpec, &reuseError)) return true;
    if (!database.beginOcr(file, languageSpec, nullptr)) return false;

    const QString configuredWorker = qEnvironmentVariable("PURRFIND_OCR_WORKER");
    const QString executable = configuredWorker.isEmpty()
        ? QCoreApplication::applicationDirPath() + "/purrfind-ocr-worker" : configuredWorker;
    const QString profile = OcrScheduler::effectiveProfile(config);
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("OMP_THREAD_LIMIT", QString::number(OcrScheduler::threadLimit(profile)));
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(executable, {"--path", file.path, "--kind", file.extension == "pdf" ? "pdf" : "image",
                               "--languages", languageSpec, "--dpi", QString::number(config.ocrDpi),
                               "--max-pages", QString::number(config.ocrMaximumPdfPages),
                               "--profile", profile});
    if (!process.waitForStarted(5000)) {
        database.failOcr(file, "OCR worker failed to start", nullptr);
        return false;
    }

    QByteArray pending;
    QString workerError;
    int totalPages = 0;
    int recognizedPages = 0;
    int nativePages = 0;
    int limitedPages = 0;
    bool done = false;
    QElapsedTimer pageTimer; pageTimer.start();
    auto consume = [&] {
        pending += process.readAllStandardOutput();
        while (true) {
            const qsizetype newline = pending.indexOf('\n');
            if (newline < 0) break;
            const QByteArray line = pending.left(newline); pending.remove(0, newline + 1);
            QJsonParseError parseError;
            const QJsonObject object = QJsonDocument::fromJson(line, &parseError).object();
            if (parseError.error != QJsonParseError::NoError) { workerError = "invalid OCR worker response"; continue; }
            const QString event = object.value("event").toString();
            if (event == "start") {
                totalPages = object.value("totalPages").toInt();
                limitedPages = object.value("limitedPages").toInt();
                pageTimer.restart();
            } else if (event == "page") {
                OcrPageResult page;
                page.pageNumber = object.value("page").toInt();
                page.totalPages = object.value("totalPages").toInt(totalPages);
                page.text = object.value("text").toString();
                page.confidence = object.value("confidence").toDouble();
                page.nativeText = object.value("nativeText").toBool();
                if (!database.storeOcrPage(file, page, languageSpec, &workerError)) currentCancelled_.store(true);
                if (!page.nativeText && !page.text.trimmed().isEmpty()) ++recognizedPages;
                if (page.nativeText) ++nativePages;
                pageTimer.restart();
                QMetaObject::invokeMethod(this, &OcrQueue::progressChanged, Qt::QueuedConnection);
            } else if (event == "error") {
                workerError = object.value("message").toString("OCR worker error");
            } else if (event == "done") done = true;
        }
    };

    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(250);
        consume();
        if (currentCancelled_.load() || stopped_.load()) {
            process.terminate();
            if (!process.waitForFinished(1500)) process.kill();
            process.waitForFinished(1500);
            database.setOcrState(file.id, paused_.load() ? OcrState::Paused : OcrState::Pending,
                                 paused_.load() ? "paused" : QString(), nullptr);
            return false;
        }
        if (pageTimer.elapsed() > config.ocrPageTimeoutSeconds * 1000LL) {
            process.kill(); process.waitForFinished(1500);
            database.failOcr(file, "OCR page timeout", nullptr);
            return false;
        }
    }
    consume();
    if (!done || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (workerError.isEmpty()) workerError = QString::fromUtf8(process.readAllStandardError().left(512)).trimmed();
        if (workerError.isEmpty()) workerError = "OCR worker failed";
        database.failOcr(file, workerError, nullptr);
        return false;
    }
    const QString limitation = limitedPages > 0 && totalPages > limitedPages
        ? QString("automatic OCR limited to first %1 pages").arg(limitedPages) : QString();
    const OcrState finalState = recognizedPages > 0 ? OcrState::Indexed
        : nativePages == limitedPages && limitedPages > 0 ? OcrState::NotRequired : OcrState::NoText;
    database.finishOcr(file, finalState,
                       totalPages, limitation, nullptr);
    return true;
}

void OcrQueue::run()
{
#ifdef PURRFIND_WITH_OCR
    ::setpriority(PRIO_PROCESS, 0, 19);
    Database database;
    QString error;
    if (!database.open(Config::databasePath(), false, &error) || !database.migrate(&error)) {
        qCWarning(logIndexer).noquote() << "OCR queue database:" << error;
        return;
    }
    database.resetInterruptedOcr(nullptr);
    while (!stopped_.load()) {
        ConfigData config;
        bool reindex = false;
        std::uint64_t generation = 0;
        {
            std::unique_lock lock(mutex_);
            // Work is announced by the crawler, inotify queue, configuration
            // changes, and explicit reindex requests. A drained OCR queue must
            // not periodically wake up just to query SQLite.
            condition_.wait(lock, [this] { return stopped_.load() || wake_; });
            if (stopped_.load()) break;
            wake_ = false;
            config = config_;
            generation = generation_.load();
            reindex = reindexRequested_;
            reindexRequested_ = false;
        }
        if (reindex) database.resetOcr(true, nullptr);
        if (!config.ocrPaused)
            database.applyOcrPolicy(config.ocrPdfEnabled, config.ocrImagesEnabled, nullptr);
        QString reason;
        if (!OcrScheduler::mayRun(config, &reason)) {
            std::lock_guard lock(mutex_); waitReason_ = reason;
            continue;
        }
        const QStringList languages = OcrLanguageManager::usableLanguages(config.ocrLanguages);
        if (languages.isEmpty()) {
            const auto pending = database.pendingOcr(config.ocrPdfEnabled, config.ocrImagesEnabled,
                                                     config.contentIndexingEnabled && config.contentTypes.contains("pdf"), 32, nullptr);
            for (const auto &file : pending)
                database.setOcrState(file.id, OcrState::Unsupported, "OCR language pack not found", nullptr);
            std::lock_guard lock(mutex_); waitReason_ = "OCR language pack not found";
            QMetaObject::invokeMethod(this, &OcrQueue::progressChanged, Qt::QueuedConnection);
            continue;
        }
        { std::lock_guard lock(mutex_); waitReason_.clear(); }
        while (!stopped_.load() && !paused_.load() && generation == generation_.load()) {
            const auto pending = database.pendingOcr(config.ocrPdfEnabled, config.ocrImagesEnabled,
                                                     config.contentIndexingEnabled && config.contentTypes.contains("pdf"), 1, &error);
            if (!error.isEmpty()) { qCWarning(logDatabase).noquote() << "OCR queue:" << error; error.clear(); break; }
            if (pending.isEmpty()) { database.optimize(nullptr); break; }
            const FileRecord file = pending.first();
            currentCancelled_.store(false);
            { std::lock_guard lock(mutex_); currentPath_ = file.path; }
            processFile(database, file, config, languages);
            { std::lock_guard lock(mutex_); currentPath_.clear(); }
            QThread::msleep(250);
        }
    }
#endif
}

} // namespace purrfind
