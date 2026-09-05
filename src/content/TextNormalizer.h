#pragma once

#include <QString>

namespace purrfind {

class TextNormalizer {
public:
    static QString normalize(QString text, qint64 maximumUtf8Bytes, bool *truncated = nullptr);
    static bool appearsBinary(const QByteArray &sample);
};

} // namespace purrfind

