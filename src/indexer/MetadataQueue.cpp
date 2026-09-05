#include "indexer/MetadataQueue.h"

#include "core/Database.h"
#include "core/FileSystem.h"
#include "core/Logging.h"
#include "metadata/MetadataRegistry.h"

#include <QMetaObject>
#include <QThread>
#include <sys/resource.h>

namespace purrfind {

MetadataQueue::MetadataQueue(QObject *parent) : QObject(parent) {}
MetadataQueue::~MetadataQueue() { stop(); }

void MetadataQueue::start(const ConfigData &config)
{
    if (worker_.joinable()) return;
    config_ = config;
    paused_.store(!config.advancedImageMetadata);
    stopped_.store(false);
    wake_ = true;
    worker_ = std::thread(&MetadataQueue::run, this);
}

void MetadataQueue::stop()
{
    stopped_.store(true);
    currentCancelled_.store(true);
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void MetadataQueue::configure(const ConfigData &config)
{
    {
        std::lock_guard lock(mutex_);
        if (!config_.advancedImageMetadata && config.advancedImageMetadata) resetRequested_ = true;
        config_ = config;
        generation_.fetch_add(1);
        wake_ = true;
    }
    currentCancelled_.store(true);
    paused_.store(!config.advancedImageMetadata);
    condition_.notify_all();
}

void MetadataQueue::notifyWork()
{
    { std::lock_guard lock(mutex_); wake_ = true; }
    condition_.notify_one();
}

void MetadataQueue::invalidate(const QString &path)
{
    {
        std::lock_guard lock(mutex_);
        if (currentPath_ == path || (!currentPath_.isEmpty() && FileSystem::isWithin(currentPath_, path)))
            currentCancelled_.store(true);
        wake_ = true;
    }
    condition_.notify_one();
}

void MetadataQueue::run()
{
    ::setpriority(PRIO_PROCESS, 0, 12);
    Database database;
    QString error;
    if (!database.open(Config::databasePath(), false, &error) || !database.migrate(&error)) {
        qCWarning(logIndexer).noquote() << "metadata queue database:" << error;
        return;
    }
    database.resetInterruptedMetadata(nullptr);
    MetadataRegistry registry;
    while (!stopped_.load()) {
        std::uint64_t generation = 0;
        bool reset = false;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] { return stopped_.load() || (wake_ && !paused_.load()); });
            if (stopped_.load()) break;
            wake_ = false;
            generation = generation_.load();
            reset = resetRequested_;
            resetRequested_ = false;
        }
        if (reset) database.resetImageMetadata(&error);
        while (!stopped_.load() && !paused_.load() && generation == generation_.load()) {
            const auto pending = database.pendingMetadata(8, &error);
            if (!error.isEmpty()) { qCWarning(logDatabase).noquote() << error; error.clear(); break; }
            if (pending.isEmpty()) { database.optimize(nullptr); break; }
            QVector<MetadataUpdate> extractedBatch;
            for (const auto &file : pending) {
                if (stopped_.load() || paused_.load() || generation != generation_.load()) break;
                currentCancelled_.store(false);
                { std::lock_guard lock(mutex_); currentPath_ = file.path; }
                const MetadataProvider *provider = registry.providerFor(file);
                if (!provider) {
                    database.setMetadataState(file.id, MetadataState::Unsupported, "no metadata provider", nullptr);
                    continue;
                }
                database.setMetadataState(file.id, MetadataState::Indexing, {}, nullptr);
                RichMetadata metadata = provider->extract(file, CancellationToken(&currentCancelled_));
                if (currentCancelled_.load() || generation != generation_.load())
                    database.setMetadataState(file.id, MetadataState::Queued, {}, nullptr);
                else
                    extractedBatch.append({file, std::move(metadata)});
                QThread::msleep(35);
            }
            if (!extractedBatch.isEmpty() && generation == generation_.load()) {
                if (!database.storeRichMetadataBatch(extractedBatch, &error)) {
                    qCWarning(logDatabase).noquote() << "metadata batch:" << error;
                    for (const auto &update : extractedBatch)
                        database.setMetadataState(update.revision.id, MetadataState::Queued, {}, nullptr);
                    error.clear();
                }
            } else if (!extractedBatch.isEmpty()) {
                for (const auto &update : extractedBatch)
                    database.setMetadataState(update.revision.id, MetadataState::Queued, {}, nullptr);
            }
            QMetaObject::invokeMethod(this, &MetadataQueue::progressChanged, Qt::QueuedConnection);
        }
    }
}

} // namespace purrfind
