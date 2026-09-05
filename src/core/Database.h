#pragma once

#include "core/Types.h"

#include <QString>
#include <QVector>
#include <sqlite3.h>
#include "core/ContentExtractor.h"

namespace purrfind {

class Database {
public:
    Database() = default;
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    bool open(const QString &path, bool readOnly = false, QString *error = nullptr);
    void close();
    bool isOpen() const { return db_ != nullptr; }
    bool migrate(QString *error = nullptr, int targetVersion = 6);
    int schemaVersion(QString *error = nullptr) const;

    bool begin(QString *error = nullptr);
    bool commit(QString *error = nullptr);
    void rollback();
    bool upsert(const FileRecord &record, QString *error = nullptr);
    bool upsertBatch(const QVector<FileRecord> &records, QString *error = nullptr);
    bool removePath(const QString &path, bool recursive, QString *error = nullptr);
    bool removeMissing(const QString &root, qint64 generation, QString *error = nullptr);
    bool markRootOffline(const QString &root, bool offline, QString *error = nullptr);
    QVector<SearchResult> search(const ParsedQuery &query, int limit,
                                 bool showHidden, QString *error = nullptr,
                                 bool useUsageHistory = true) const;
    QVector<SearchResult> searchContent(const ParsedQuery &query, int limit,
                                        bool showHidden, QString *error = nullptr,
                                        bool useUsageHistory = true) const;
    QVector<SearchResult> searchOcr(const ParsedQuery &query, int limit,
                                    bool showHidden, QString *error = nullptr,
                                    bool useUsageHistory = true) const;
    QVector<FileRecord> pendingContent(const QStringList &extensions, int limit,
                                       QString *error = nullptr) const;
    bool setContentState(qint64 fileId, ContentState state, const QString &extractor,
                         const QString &message = {}, QString *error = nullptr);
    bool storeContent(const FileRecord &revision, const ExtractResult &result,
                      const QString &extractor, QString *error = nullptr);
    bool storeContentBatch(const QVector<ContentUpdate> &updates, QString *error = nullptr);
    bool resetContent(const QStringList &extensions, QString *error = nullptr);
    bool resetInterruptedContent(QString *error = nullptr);
    QHash<ContentState, qint64> contentStateCounts(QString *error = nullptr) const;
    QVector<ExtractorMetrics> contentExtractorMetrics(QString *error = nullptr) const;
    QVector<FileRecord> pendingMetadata(int limit, QString *error = nullptr) const;
    bool setMetadataState(qint64 fileId, MetadataState state, const QString &message = {},
                          QString *error = nullptr);
    bool storeRichMetadata(const FileRecord &revision, const RichMetadata &metadata,
                           QString *error = nullptr);
    bool storeRichMetadataBatch(const QVector<MetadataUpdate> &updates, QString *error = nullptr);
    QHash<MetadataState, qint64> metadataStateCounts(QString *error = nullptr) const;
    bool resetInterruptedMetadata(QString *error = nullptr);
    bool resetImageMetadata(QString *error = nullptr);
    bool recordOpen(qint64 fileId, QString *error = nullptr);
    bool clearUsageHistory(QString *error = nullptr);
    QVector<FileRecord> pendingOcr(bool pdfEnabled, bool imagesEnabled, bool waitForContent, int limit,
                                   QString *error = nullptr) const;
    bool setOcrState(qint64 fileId, OcrState state, const QString &message = {},
                     QString *error = nullptr);
    bool beginOcr(const FileRecord &revision, const QString &languages,
                  QString *error = nullptr);
    bool storeOcrPage(const FileRecord &revision, const OcrPageResult &page,
                      const QString &languages, QString *error = nullptr);
    bool finishOcr(const FileRecord &revision, OcrState state, int totalPages,
                   const QString &message, QString *error = nullptr);
    bool failOcr(const FileRecord &revision, const QString &message,
                 QString *error = nullptr);
    bool resetInterruptedOcr(QString *error = nullptr);
    bool resetOcr(bool includeIndexed, QString *error = nullptr);
    bool applyOcrPolicy(bool pdfEnabled, bool imagesEnabled, QString *error = nullptr);
    bool reuseOcrForHardlink(const FileRecord &revision, const QString &languages,
                             QString *error = nullptr);
    QHash<OcrState, qint64> ocrStateCounts(QString *error = nullptr) const;
    qint64 ocrPageCount(QString *error = nullptr) const;
    bool movePathPreservingContent(const QString &oldPath, const FileRecord &record,
                                   bool directory, QString *error = nullptr);
    bool optimize(QString *error = nullptr);
    bool quickCheck(QString *error = nullptr) const;
    bool checkpointWal(QString *error = nullptr);

    qint64 fileCount(QString *error = nullptr) const;
    qint64 databaseSize() const;
    QString path() const { return path_; }
    sqlite3 *handle() const { return db_; }

private:
    bool execute(const char *sql, QString *error = nullptr) const;
    static QString sqliteError(sqlite3 *db);
    bool storeContentInTransaction(const FileRecord &revision, const ExtractResult &result,
                                   const QString &extractor, QString *error);
    bool storeRichMetadataInTransaction(const FileRecord &revision, const RichMetadata &metadata,
                                        QString *error);
    sqlite3 *db_{nullptr};
    QString path_;
};

} // namespace purrfind
