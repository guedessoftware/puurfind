#pragma once

#include <QString>
#include <QVector>
#include <cstdint>

namespace purrfind {

enum FileFlag : uint32_t {
    Hidden = 1U << 0,
    Offline = 1U << 1,
};

struct FileRecord {
    qint64 id{-1};
    QString name;
    QString path;
    QString parentPath;
    QString extension;
    QString type;
    qint64 size{0};
    qint64 mtime{0};
    qint64 ctime{0};
    quint64 inode{0};
    quint64 device{0};
    bool directory{false};
    bool symlink{false};
    bool hidden{false};
    QString root;
    qint64 scanGeneration{0};
};

enum class ModifiedFilter { Any, Today, Days7, Days30 };

enum class SearchScope { Unified, Name, Path, Content };

enum class ContentState : int {
    NotIndexed = 0,
    Queued = 1,
    Indexing = 2,
    Indexed = 3,
    NoText = 4,
    Unsupported = 5,
    Encrypted = 6,
    Failed = 7,
    TooLarge = 8,
};

enum class MetadataState : int {
    NotIndexed = 0,
    Queued = 1,
    Indexing = 2,
    Indexed = 3,
    Unsupported = 4,
    Failed = 5,
};

enum class OcrState : int {
    NotRequired = 0,
    Pending = 1,
    Queued = 2,
    Processing = 3,
    Indexed = 4,
    NoText = 5,
    Failed = 6,
    Unsupported = 7,
    Skipped = 8,
    Paused = 9,
};

// Bump whenever OCR preprocessing/recognition changes in a way that should
// invalidate persisted results. Schema migrations use this value to make the
// refresh explicit instead of silently keeping output from an older pipeline.
inline constexpr int OcrPipelineVersion = 2;

struct ParsedQuery {
    QString text;
    QString extension;
    QString folder;
    ModifiedFilter modified{ModifiedFilter::Any};
    qint64 minimumSize{-1};
    qint64 maximumSize{-1};
    bool directoriesOnly{false};
    bool filesOnly{false};
    QString category;
    QString author;
    QString camera;
    QString source;
    int minimumPages{-1};
    int maximumPages{-1};
    int minimumWidth{-1};
    int maximumWidth{-1};
    int minimumHeight{-1};
    int maximumHeight{-1};
    SearchScope scope{SearchScope::Unified};
    bool phrase{false};
    QString ftsExpression;
};

struct SearchResult {
    FileRecord file;
    double score{0.0};
    QString snippet;
    QString matchOrigin{"name"};
    QString documentTitle;
    QString documentAuthor;
    int pageCount{0};
    int matchPage{0};
    QString cameraMake;
    QString cameraModel;
    int imageWidth{0};
    int imageHeight{0};
    qint64 dateTaken{0};
    QString scoreExplanation;
};

struct RichMetadata {
    QString cameraMake;
    QString cameraModel;
    QString lens;
    QString software;
    QString copyright;
    QString dateTakenText;
    qint64 dateTaken{0};
    int width{0};
    int height{0};
    int orientation{1};
    int iso{0};
    QString aperture;
    QString exposureTime;
    QString focalLength;
    bool hasGps{false};
    double latitude{0.0};
    double longitude{0.0};
    QString detailsJson{"{}"};
    QString error;
};

struct MetadataUpdate {
    FileRecord revision;
    RichMetadata metadata;
};

struct OcrPageResult {
    int pageNumber{1};
    int totalPages{1};
    QString text;
    double confidence{0.0};
    bool nativeText{false};
};

struct ExtractorMetrics {
    QString extractor;
    qint64 documents{0};
    double averageMilliseconds{0.0};
    qint64 p95Milliseconds{0};
    qint64 bytesProcessed{0};
};

struct IndexStatus {
    QString state{"idle"};
    QString currentPath;
    qint64 indexed{0};
    qint64 total{0};
    qint64 lastUpdate{0};
    QString lastError;
};

} // namespace purrfind
