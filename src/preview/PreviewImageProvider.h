#pragma once

#include "preview/PreviewCache.h"

#include <QQuickImageProvider>
#include <memory>

namespace purrfind {

class PreviewImageProvider final : public QQuickImageProvider {
public:
    explicit PreviewImageProvider(std::shared_ptr<PreviewCache> cache);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
private:
    QImage icon(const QString &mime, const QSize &size) const;
    std::shared_ptr<PreviewCache> cache_;
};

} // namespace purrfind
