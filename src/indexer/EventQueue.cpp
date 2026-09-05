#include "indexer/EventQueue.h"

namespace purrfind {

EventQueue::EventQueue(QObject *parent, int delayMs) : QObject(parent)
{
    timer_.setSingleShot(true);
    timer_.setInterval(delayMs);
    connect(&timer_, &QTimer::timeout, this, [this] {
        const auto events = takeNow();
        if (!events.isEmpty()) emit ready(events);
    });
}

void EventQueue::enqueue(const FsEvent &event)
{
    if (event.kind == EventKind::Rename && !event.oldPath.isEmpty()) pending_.remove(event.oldPath);
    auto existing = pending_.find(event.path);
    if (existing == pending_.end() || event.kind == EventKind::Remove
        || event.kind == EventKind::Reconcile) {
        pending_.insert(event.path, event);
    } else if (existing->kind == EventKind::Remove && event.kind == EventKind::Upsert) {
        *existing = event;
    }
    timer_.start();
}

QVector<FsEvent> EventQueue::takeNow()
{
    timer_.stop();
    QVector<FsEvent> result;
    result.reserve(pending_.size());
    for (const auto &event : std::as_const(pending_)) result.append(event);
    pending_.clear();
    return result;
}

} // namespace purrfind
