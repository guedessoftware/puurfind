#pragma once

#include "app/SearchModel.h"
#include "preview/PreviewRegistry.h"

#include <QObject>
#include <QElapsedTimer>
#include <QThreadPool>
#include <QTimer>
#include <memory>

namespace purrfind {

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(purrfind::SearchModel* results READ results CONSTANT)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QString statusJson READ statusJson NOTIFY statusChanged)
    Q_PROPERTY(QString configJson READ configJson NOTIFY configChanged)
    Q_PROPERTY(QString previewText READ previewText NOTIFY previewChanged)
    Q_PROPERTY(QString previewImageUrl READ previewImageUrl NOTIFY previewChanged)
    Q_PROPERTY(QString previewTitle READ previewTitle NOTIFY previewChanged)
    Q_PROPERTY(QString previewDetails READ previewDetails NOTIFY previewChanged)
    Q_PROPERTY(bool previewLoading READ previewLoading NOTIFY previewChanged)
    Q_PROPERTY(bool previewEnabled READ previewEnabled NOTIFY previewEnabledChanged)
    Q_PROPERTY(int previewPage READ previewPage NOTIFY previewChanged)
    Q_PROPERTY(int previewPageCount READ previewPageCount NOTIFY previewChanged)
    Q_PROPERTY(qint64 lastSearchMilliseconds READ lastSearchMilliseconds NOTIFY searchingChanged)
    Q_PROPERTY(QVariantList categoryCounts READ categoryCounts NOTIFY categoryCountsChanged)
public:
    explicit AppController(std::shared_ptr<PreviewCache> previewCache, QObject *parent = nullptr);
    SearchModel *results() { return &model_; }
    bool searching() const { return searching_; }
    QString error() const { return error_; }
    QString statusText() const { return statusText_; }
    QString statusJson() const { return statusJson_; }
    QString configJson() const { return configJson_; }
    QString previewText() const { return previewText_; }
    QString previewImageUrl() const { return previewImageUrl_; }
    QString previewTitle() const { return previewTitle_; }
    QString previewDetails() const { return previewDetails_; }
    bool previewLoading() const { return previewLoading_; }
    bool previewEnabled() const { return previewEnabled_; }
    int previewPage() const { return previewPage_; }
    int previewPageCount() const { return previewPageCount_; }
    qint64 lastSearchMilliseconds() const { return lastSearchMilliseconds_; }
    QVariantList categoryCounts() const { return categoryCounts_; }

    Q_INVOKABLE void search(const QString &text, const QString &category = {});
    Q_INVOKABLE void open(int row);
    Q_INVOKABLE void reveal(int row);
    Q_INVOKABLE void copyPath(int row);
    Q_INVOKABLE void select(int row);
    Q_INVOKABLE QString properties(int row) const;
    Q_INVOKABLE void refreshStatus();
    Q_INVOKABLE void loadConfig();
    Q_INVOKABLE bool saveConfig(const QString &json);
    Q_INVOKABLE void reindex();
    Q_INVOKABLE void pauseContent();
    Q_INVOKABLE void resumeContent();
    Q_INVOKABLE void reindexContent();
    Q_INVOKABLE void reindexMetadata();
    Q_INVOKABLE void navigatePreview(int delta);
    Q_INVOKABLE void togglePreview();
    Q_INVOKABLE void clearPreviewCache();
    Q_INVOKABLE void clearUsageHistory();
    Q_INVOKABLE void pauseOcr();
    Q_INVOKABLE void resumeOcr();
    Q_INVOKABLE void reindexOcr();
    Q_INVOKABLE void rebuildIndex();
    Q_INVOKABLE QString iconUrl(const QString &mimeType, bool directory) const;
    Q_INVOKABLE QString displayPath(const QString &path) const;
    void reportError(const QString &message) { setError(message); }

public slots:
    void applyStatus(const QString &json);

signals:
    void searchingChanged();
    void errorChanged();
    void statusChanged();
    void configChanged();
    void previewChanged();
    void previewEnabledChanged();
    void categoryCountsChanged();
    void showRequested();

private:
    void setError(const QString &error);
    void requestPreview();
    void dispatchSearch();
    SearchModel model_;
    bool searching_{false};
    QString error_;
    QString statusText_{"Connecting to indexer…"};
    QString statusJson_{"{}"};
    QString configJson_{"{}"};
    QString previewText_;
    QString previewImageUrl_;
    QString previewTitle_;
    QString previewDetails_;
    QString currentQuery_;
    QString pendingQuery_;
    QString pendingCategory_;
    bool searchPending_{false};
    bool previewLoading_{false};
    bool previewEnabled_{true};
    int selectedRow_{-1};
    int requestedPage_{0};
    int previewPage_{0};
    int previewPageCount_{0};
    quint64 previewGeneration_{0};
    std::shared_ptr<std::atomic_bool> previewCancellation_;
    std::shared_ptr<PreviewCache> previewCache_;
    PreviewRegistry previewRegistry_;
    quint64 searchGeneration_{0};
    qint64 lastSearchMilliseconds_{0};
    QVariantList categoryCounts_{0, 0, 0, 0, 0, 0, 0};
    QElapsedTimer searchTimer_;
    QTimer statusTimer_;
    QTimer previewTimer_;
    QThreadPool previewPool_;
};

} // namespace purrfind
