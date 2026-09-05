#include "core/SearchEngine.h"

#include <QHash>
#include <algorithm>

namespace purrfind {

QVector<SearchResult> SearchEngine::search(const QString &input, int limit,
                                           bool showHidden, QString *error) const
{
    ParsedQuery query = QueryParser::parse(input);
    if (query.category == "content") query.scope = SearchScope::Content;
    QVector<SearchResult> combined;
    if (query.source != "ocr" && query.scope != SearchScope::Content)
        combined = database_.search(query, qBound(1, limit * 2, 2000), showHidden, error, useUsageHistory_);
    if (query.source != "ocr" && (query.scope == SearchScope::Unified || query.scope == SearchScope::Content)) {
        const auto content = database_.searchContent(query, qBound(1, limit * 2, 2000), showHidden, error, useUsageHistory_);
        QHash<qint64, int> positions;
        for (int i = 0; i < combined.size(); ++i) positions.insert(combined.at(i).file.id, i);
        for (const auto &entry : content) {
            const auto found = positions.constFind(entry.file.id);
            if (found == positions.cend()) {
                positions.insert(entry.file.id, combined.size());
                combined.append(entry);
            } else if (combined[*found].snippet.isEmpty()) {
                combined[*found].snippet = entry.snippet;
                combined[*found].documentTitle = entry.documentTitle;
                combined[*found].documentAuthor = entry.documentAuthor;
                combined[*found].pageCount = entry.pageCount;
                combined[*found].matchPage = entry.matchPage;
                if (combined[*found].cameraMake.isEmpty()) combined[*found].cameraMake = entry.cameraMake;
                if (combined[*found].cameraModel.isEmpty()) combined[*found].cameraModel = entry.cameraModel;
                if (!combined[*found].imageWidth) combined[*found].imageWidth = entry.imageWidth;
                if (!combined[*found].imageHeight) combined[*found].imageHeight = entry.imageHeight;
            }
        }
    }
    if (query.source != "native" && (query.scope == SearchScope::Unified || query.scope == SearchScope::Content)) {
        const auto ocr = database_.searchOcr(query, qBound(1, limit * 2, 2000), showHidden, error, useUsageHistory_);
        QHash<qint64, int> positions;
        for (int i = 0; i < combined.size(); ++i) positions.insert(combined.at(i).file.id, i);
        for (const auto &entry : ocr) {
            const auto found = positions.constFind(entry.file.id);
            if (found == positions.cend()) {
                positions.insert(entry.file.id, combined.size());
                combined.append(entry);
            } else if (combined[*found].snippet.isEmpty()) {
                combined[*found].snippet = entry.snippet;
                combined[*found].matchPage = entry.matchPage;
            }
        }
    }
    std::stable_sort(combined.begin(), combined.end(), [](const SearchResult &a, const SearchResult &b) {
        if (a.score != b.score) return a.score > b.score;
        return a.file.mtime > b.file.mtime;
    });
    if (combined.size() > limit) combined.resize(limit);
    return combined;
}

} // namespace purrfind
