#pragma once

#include "core/Types.h"

#include <optional>

namespace purrfind {

class FileSystem {
public:
    static QString normalizePath(const QString &path);
    static bool isWithin(const QString &path, const QString &directory);
    // Returns true when a path below an explicitly indexed root contains a
    // hidden component. The root itself is not considered hidden, so adding a
    // hidden directory as an indexed root is an intentional exception.
    static bool isHiddenWithin(const QString &path, const QString &root);
    static bool isExcluded(const QString &path, const QStringList &exclusions);
    static std::optional<FileRecord> inspect(const QString &path, const QString &root,
                                             qint64 generation, QString *error = nullptr);
    static QString basicType(const QString &path, bool directory);
};

} // namespace purrfind
