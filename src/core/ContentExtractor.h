#pragma once

#include "core/Types.h"

#include <QString>
#include <QVector>
#include <atomic>

namespace purrfind {

struct ExtractResult {
    ContentState state{ContentState::Failed};
    QString text;
    QString title;
    QString author;
    QString subject;
    QString keywords;
    QString detailsJson;
    int pageCount{0};
    qint64 bytesRead{0};
    qint64 extractionMs{0};
    bool truncated{false};
    QString error;
    QVector<QString> pages;
};

struct ExtractionLimits {
    qint64 maximumFileBytes{100LL * 1024 * 1024};
    qint64 maximumTextBytes{8LL * 1024 * 1024};
    qint64 maximumArchiveEntryBytes{64LL * 1024 * 1024};
    qint64 maximumArchiveBytes{256LL * 1024 * 1024};
    int maximumArchiveEntries{2048};
    int maximumCompressionRatio{200};
};

struct ContentUpdate {
    FileRecord revision;
    ExtractResult result;
    QString extractor;
};

class CancellationToken {
public:
    explicit CancellationToken(const std::atomic_bool *cancelled = nullptr) : cancelled_(cancelled) {}
    bool isCancelled() const { return cancelled_ && cancelled_->load(); }
private:
    const std::atomic_bool *cancelled_;
};

// Internal extension point for document extractors and a future OCR pipeline.
class ContentExtractor {
public:
    virtual ~ContentExtractor() = default;
    virtual QString id() const = 0;
    virtual bool supports(const FileRecord &file) const = 0;
    virtual ExtractResult extract(const FileRecord &file, const ExtractionLimits &limits,
                                  const CancellationToken &cancel) const = 0;
};

} // namespace purrfind
