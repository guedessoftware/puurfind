#include "indexer/ContentIndexQueue.h"

#include "content/ExtractorRegistry.h"
#include "core/Database.h"
#include "core/FileSystem.h"
#include "core/Logging.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QThread>
#include <sys/resource.h>

namespace purrfind {

ContentIndexQueue::ContentIndexQueue(QObject *parent) : QObject(parent) {}
ContentIndexQueue::~ContentIndexQueue() { stop(); }

void ContentIndexQueue::start(const ConfigData &config)
{
    if (worker_.joinable()) return;
    config_ = config;
    paused_.store(config.contentIndexingPaused || !config.contentIndexingEnabled);
    stopped_.store(false);
    wake_ = true;
    worker_ = std::thread(&ContentIndexQueue::run, this);
}

void ContentIndexQueue::stop()
{
    stopped_.store(true);
    currentCancelled_.store(true);
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void ContentIndexQueue::configure(const ConfigData &config)
{
    {
        std::lock_guard lock(mutex_);
        const bool extractionPolicyChanged = config_.contentTypes != config.contentTypes
            || config_.contentExcludedPaths != config.contentExcludedPaths
            || config_.maximumContentFileBytes != config.maximumContentFileBytes
            || config_.maximumExtractedTextBytes != config.maximumExtractedTextBytes;
        config_ = config;
        if (extractionPolicyChanged) reindexRequested_ = true;
        configGeneration_.fetch_add(1);
        wake_ = true;
    }
    currentCancelled_.store(true);
    paused_.store(config.contentIndexingPaused || !config.contentIndexingEnabled);
    condition_.notify_all();
}

void ContentIndexQueue::notifyWork()
{
    {
        std::lock_guard lock(mutex_);
        wake_ = true;
    }
    condition_.notify_one();
}

void ContentIndexQueue::invalidate(const QString &path)
{
    {
        std::lock_guard lock(mutex_);
        if (currentPath_ == path || (!currentPath_.isEmpty() && FileSystem::isWithin(currentPath_, path)))
            currentCancelled_.store(true);
        wake_ = true;
    }
    condition_.notify_one();
}

void ContentIndexQueue::setPaused(bool paused)
{
    paused_.store(paused);
    if (paused) currentCancelled_.store(true);
    notifyWork();
}

void ContentIndexQueue::reindex()
{
    {
        std::lock_guard lock(mutex_);
        reindexRequested_ = true;
        wake_ = true;
    }
    currentCancelled_.store(true);
    condition_.notify_one();
}

void ContentIndexQueue::run()
{
    ::setpriority(PRIO_PROCESS, 0, 10);
    Database database;
    QString error;
    if (!database.open(Config::databasePath(), false, &error) || !database.migrate(&error)) {
        qCWarning(logIndexer).noquote() << "content queue database:" << error;
        return;
    }
    database.resetInterruptedContent(nullptr);
    ExtractorRegistry registry;

    while (!stopped_.load()) {
        ConfigData config;
        std::uint64_t configGeneration = 0;
        bool shouldReindex = false;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] { return stopped_.load() || (wake_ && !paused_.load()); });
            if (stopped_.load()) break;
            wake_ = false;
            config = config_;
            configGeneration = configGeneration_.load();
            shouldReindex = reindexRequested_;
            reindexRequested_ = false;
        }
        if (shouldReindex) database.resetContent(config.contentTypes, &error);
        if (!config.contentIndexingEnabled || paused_.load()) continue;

        while (!stopped_.load() && !paused_.load()
               && configGeneration == configGeneration_.load()) {
            const auto pending = database.pendingContent(config.contentTypes, 16, &error);
            if (!error.isEmpty()) { qCWarning(logDatabase).noquote() << error; error.clear(); break; }
            if (pending.isEmpty()) {
                database.optimize(nullptr);
                QMetaObject::invokeMethod(this, &ContentIndexQueue::progressChanged, Qt::QueuedConnection);
                break;
            }
            QVector<ContentUpdate> extractedBatch;
            for (const auto &file : pending) {
                if (stopped_.load() || paused_.load()
                    || configGeneration != configGeneration_.load()) break;
                currentCancelled_.store(false);
                {
                    std::lock_guard lock(mutex_);
                    currentPath_ = file.path;
                }
                const ContentExtractor *extractor = registry.extractorFor(file, config.contentTypes);
                if (!extractor) {
                    database.setContentState(file.id, ContentState::Unsupported, {}, "unsupported or disabled type", nullptr);
                    continue;
                }
                if (FileSystem::isExcluded(file.path, config.contentExcludedPaths)) {
                    database.setContentState(file.id, ContentState::Unsupported, extractor->id(), "content excluded by user", nullptr);
                    continue;
                }
                if (file.size > config.maximumContentFileBytes) {
                    database.setContentState(file.id, ContentState::TooLarge, extractor->id(), "content size limit exceeded", nullptr);
                    continue;
                }
                database.setContentState(file.id, ContentState::Indexing, extractor->id(), {}, nullptr);
                ExtractionLimits limits;
                limits.maximumFileBytes = config.maximumContentFileBytes;
                limits.maximumTextBytes = config.maximumExtractedTextBytes;
                QElapsedTimer timer;
                timer.start();
                ExtractResult result = extractor->extract(file, limits, CancellationToken(&currentCancelled_));
                result.extractionMs = timer.elapsed();
                if (currentCancelled_.load() || configGeneration != configGeneration_.load()) {
                    database.setContentState(file.id, ContentState::Queued, extractor->id(), "", nullptr);
                } else {
                    extractedBatch.append({file, result, extractor->id()});
                }
                QThread::msleep(20);
            }
            if (!extractedBatch.isEmpty() && configGeneration == configGeneration_.load()) {
                if (!database.storeContentBatch(extractedBatch, &error)) {
                    qCWarning(logDatabase).noquote() << "content batch:" << error;
                    for (const auto &update : extractedBatch)
                        database.setContentState(update.revision.id, ContentState::Queued, update.extractor, {}, nullptr);
                    error.clear();
                } else {
                    QMetaObject::invokeMethod(this, [this, extractedBatch] {
                        for (const auto &update : extractedBatch)
                            emit documentProcessed(update.extractor, update.result.extractionMs, update.result.bytesRead);
                        emit progressChanged();
                    }, Qt::QueuedConnection);
                }
            } else if (!extractedBatch.isEmpty()) {
                for (const auto &update : extractedBatch)
                    database.setContentState(update.revision.id, ContentState::Queued, update.extractor, {}, nullptr);
            }
        }
    }
}

} // namespace purrfind
