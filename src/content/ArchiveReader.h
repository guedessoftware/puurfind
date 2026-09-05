#pragma once

#include "core/ContentExtractor.h"

#include <QHash>
#include <functional>

namespace purrfind {

struct ArchiveResult {
    QHash<QString, QByteArray> entries;
    qint64 bytesRead{0};
    bool encrypted{false};
    QString error;
};

class ArchiveReader {
public:
    using Selector = std::function<bool(const QString &)>;
    static ArchiveResult read(const QString &path, const Selector &selector,
                              const ExtractionLimits &limits, const CancellationToken &cancel);
    static QString xmlText(const QByteArray &xml, QString *error = nullptr);
    static QHash<QString, QString> xmlMetadata(const QByteArray &xml, QString *error = nullptr);
};

} // namespace purrfind

