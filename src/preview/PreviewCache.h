#pragma once

#include "preview/PreviewProvider.h"

#include <QCache>
#include <QMutex>

namespace purrfind {

class PreviewCache {
public:
    explicit PreviewCache(int memoryMiB = 96, int diskMiB = 256, const QString &directory = {});
    QString keyFor(const PreviewRequest &request) const;
    bool lookup(const QString &key, PreviewResult *result);
    void store(const QString &key, const PreviewResult &result);
    void clear();
    QString directory() const { return directory_; }

private:
    void pruneDisk();
    QMutex mutex_;
    QCache<QString, PreviewResult> memory_;
    QString directory_;
    qint64 diskLimitBytes_{0};
    int storesSincePrune_{0};
};

} // namespace purrfind
