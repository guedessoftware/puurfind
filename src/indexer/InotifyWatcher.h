#pragma once

#include "indexer/Events.h"

#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QStringList>
#include <QTimer>

class QSocketNotifier;

namespace purrfind {

class InotifyWatcher : public QObject {
    Q_OBJECT
public:
    explicit InotifyWatcher(QObject *parent = nullptr);
    ~InotifyWatcher() override;
    bool start(const QStringList &roots, const QStringList &exclusions,
               QString *error = nullptr, bool excludeHidden = false);
    void stop();
    int watchCount() const { return watches_.size(); }
    bool watchLimitReached() const { return watchLimitReached_; }
    qint64 systemWatchLimit() const { return systemWatchLimit_; }

signals:
    void eventReceived(const purrfind::FsEvent &event);
    void warning(const QString &message);

private:
    void readEvents();
    void addDirectory(const QString &path);
    void enqueueTree(const QString &root);
    void scanWatchBatch();
    bool isIgnored(const QString &path) const;

    int descriptor_{-1};
    QSocketNotifier *notifier_{nullptr};
    QHash<int, QString> watches_;
    QSet<QString> watchedPaths_;
    QSet<QString> queuedPaths_;
    QQueue<QString> pendingDirectories_;
    QTimer scanTimer_;
    QStringList roots_;
    QStringList exclusions_;
    bool excludeHidden_{false};
    QHash<uint32_t, FsEvent> movedFrom_;
    bool watchLimitReached_{false};
    qint64 systemWatchLimit_{-1};
};

} // namespace purrfind
