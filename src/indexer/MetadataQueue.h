#pragma once

#include "core/Config.h"

#include <QObject>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace purrfind {

class MetadataQueue : public QObject {
    Q_OBJECT
public:
    explicit MetadataQueue(QObject *parent = nullptr);
    ~MetadataQueue() override;
    void start(const ConfigData &config);
    void stop();
    void configure(const ConfigData &config);
    void notifyWork();
    void invalidate(const QString &path);

signals:
    void progressChanged();

private:
    void run();
    std::mutex mutex_;
    std::condition_variable condition_;
    ConfigData config_;
    std::thread worker_;
    std::atomic_bool stopped_{false};
    std::atomic_bool paused_{false};
    std::atomic_bool currentCancelled_{false};
    std::atomic_uint64_t generation_{0};
    bool wake_{false};
    bool resetRequested_{false};
    QString currentPath_;
};

} // namespace purrfind
