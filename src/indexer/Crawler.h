#pragma once

#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>

namespace purrfind {

struct CrawlResult {
    qint64 indexed{0};
    qint64 skipped{0};
    QString error;
    bool cancelled{false};
};

class Crawler {
public:
    using Progress = std::function<void(qint64, const QString &)>;
    static CrawlResult crawl(const QString &databasePath, const QString &root,
                             const QStringList &exclusions, qint64 generation,
                             const Progress &progress = {},
                             const std::atomic_bool *cancelled = nullptr,
                             bool excludeHidden = false);
};

} // namespace purrfind
