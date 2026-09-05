#pragma once

#include "core/Types.h"

namespace purrfind {

class QueryParser {
public:
    static ParsedQuery parse(const QString &input);
    static qint64 parseSize(const QString &value, bool *ok = nullptr);
};

} // namespace purrfind

