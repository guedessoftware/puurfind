#pragma once

#include <QString>
#include <QMetaType>

namespace purrfind {

enum class EventKind { Upsert, Remove, Rename, Reconcile };

struct FsEvent {
    EventKind kind{EventKind::Upsert};
    QString path;
    bool directory{false};
    QString oldPath;
};

} // namespace purrfind

Q_DECLARE_METATYPE(purrfind::FsEvent)
