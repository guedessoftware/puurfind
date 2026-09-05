#pragma once

#include "core/Config.h"
#include "core/Types.h"

#include <QObject>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace purrfind {

class OcrQueue : public QObject {
    Q_OBJECT
public:
    explicit OcrQueue(QObject *parent = nullptr);
    ~OcrQueue() override;
    void start(const ConfigData &config);
    void stop();
    void configure(const ConfigData &config);
    void notifyWork();
    void invalidate(const QString &path);
    void reindex();
    bool paused() const { return paused_.load(); }
    QString waitReason() const;

signals:
    void progressChanged();

private:
    void run();
    bool processFile(class Database &database, const FileRecord &file,
                     const ConfigData &config, const QStringList &languages);
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
    ConfigData config_;
    std::atomic_bool stopped_{false};
    std::atomic_bool paused_{false};
    std::atomic_bool currentCancelled_{false};
    std::atomic_uint64_t generation_{0};
    bool wake_{false};
    bool reindexRequested_{false};
    QString currentPath_;
    QString waitReason_;
};

} // namespace purrfind
