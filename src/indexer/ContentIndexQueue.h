#pragma once

#include "core/Config.h"

#include <QObject>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace purrfind {

class ContentIndexQueue : public QObject {
    Q_OBJECT
public:
    explicit ContentIndexQueue(QObject *parent = nullptr);
    ~ContentIndexQueue() override;
    void start(const ConfigData &config);
    void stop();
    void configure(const ConfigData &config);
    void notifyWork();
    void invalidate(const QString &path);
    void setPaused(bool paused);
    bool paused() const { return paused_.load(); }
    void reindex();

signals:
    void progressChanged();
    void documentProcessed(const QString &extractor, qint64 milliseconds, qint64 bytes);

private:
    void run();
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    ConfigData config_;
    std::thread worker_;
    std::atomic_bool stopped_{false};
    std::atomic_bool paused_{false};
    std::atomic_bool currentCancelled_{false};
    std::atomic_uint64_t configGeneration_{0};
    bool wake_{false};
    bool reindexRequested_{false};
    QString currentPath_;
};

} // namespace purrfind
