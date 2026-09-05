#pragma once

#include "core/Database.h"
#include "core/QueryParser.h"

namespace purrfind {

class SearchEngine {
public:
    explicit SearchEngine(Database &database, bool useUsageHistory = true)
        : database_(database), useUsageHistory_(useUsageHistory) {}
    QVector<SearchResult> search(const QString &input, int limit, bool showHidden,
                                 QString *error = nullptr) const;
private:
    Database &database_;
    bool useUsageHistory_{true};
};

} // namespace purrfind
