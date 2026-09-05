#pragma once

#include "core/Config.h"
#include "core/Database.h"
#include "core/Types.h"
#include "indexer/EventQueue.h"
#include "indexer/ContentIndexQueue.h"
#include "indexer/InotifyWatcher.h"
#include "indexer/MetadataQueue.h"
#include "ocr/OcrQueue.h"

#include <QObject>
#include <atomic>
#include <optional>
#include <thread>

namespace purrfind {

class IndexerService : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.purrfind.Indexer1")
public:
    explicit IndexerService(QObject *parent = nullptr);
    ~IndexerService() override;
    bool initialize(QString *error = nullptr);

public slots:
    QString Search(const QString &query, int limit);
    QString Status();
    QString ContentMetrics();
    QString GetConfig();
    bool SetConfig(const QString &json);
    bool Reindex();
    bool PauseContentIndexing();
    bool ResumeContentIndexing();
    bool ReindexContent();
    bool RecordOpen(qint64 fileId);
    bool ClearUsageHistory();
    bool ReindexMetadata();
    bool PauseOcr();
    bool ResumeOcr();
    bool ReindexOcr();
    bool RebuildIndex();
    QString Ping() const { return QStringLiteral(PURRFIND_VERSION); }

signals:
    void StatusChanged(const QString &json);
    void IndexChanged();
    void ConfigApplied(const QString &json);

private:
    void startCrawl();
    void applyPendingConfig();
    void processEvents(const QVector<FsEvent> &events);
    QString statusJson() const;
    QString rootFor(const QString &path) const;
    bool openOrRecoverDatabase(QString *error);
    bool preserveAndCreateDatabase(const QString &reason, QString *error);

    ConfigData config_;
    Database database_;
    InotifyWatcher watcher_;
    EventQueue queue_;
    ContentIndexQueue contentQueue_;
    MetadataQueue metadataQueue_;
    OcrQueue ocrQueue_;
    IndexStatus status_;
    std::thread crawlerThread_;
    std::atomic_bool cancelCrawl_{false};
    std::atomic_bool crawling_{false};
    qint64 generation_{0};
    QString recoveryBackup_;
    std::optional<ConfigData> pendingConfig_;
    bool configApplyScheduled_{false};
};

} // namespace purrfind
