#pragma once

#include "indexer/Events.h"

#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVector>

namespace purrfind {

class EventQueue : public QObject {
    Q_OBJECT
public:
    explicit EventQueue(QObject *parent = nullptr, int delayMs = 200);
    void enqueue(const FsEvent &event);
    QVector<FsEvent> takeNow();
    qsizetype pendingCount() const { return pending_.size(); }

signals:
    void ready(QVector<purrfind::FsEvent> events);

private:
    QHash<QString, FsEvent> pending_;
    QTimer timer_;
};

} // namespace purrfind

Q_DECLARE_METATYPE(QVector<purrfind::FsEvent>)
