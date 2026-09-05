#include "preview/PreviewImageProvider.h"

#include <QIcon>
#include <QMimeDatabase>
#include <QPainter>
#include <QUrl>

namespace purrfind {

PreviewImageProvider::PreviewImageProvider(std::shared_ptr<PreviewCache> cache)
    : QQuickImageProvider(QQuickImageProvider::Image), cache_(std::move(cache)) {}

QImage PreviewImageProvider::icon(const QString &mime, const QSize &requested) const
{
    const QSize target = requested.isValid() ? requested : QSize(64, 64);
    QMimeDatabase database;
    const auto mimeType = database.mimeTypeForName(mime);
    QIcon themed = QIcon::fromTheme(mimeType.iconName(), QIcon::fromTheme(mimeType.genericIconName()));
    if (!themed.isNull()) return themed.pixmap(target).toImage();
    QImage fallback(target, QImage::Format_ARGB32_Premultiplied);
    fallback.fill(Qt::transparent);
    QPainter painter(&fallback);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#a98be8"), qMax(2, target.width() / 18)));
    painter.setBrush(QColor("#302842"));
    painter.drawRoundedRect(fallback.rect().adjusted(5, 5, -5, -5), 8, 8);
    painter.drawLine(target.width() * 2 / 3, 6, target.width() * 2 / 3, target.height() / 3);
    painter.drawLine(target.width() * 2 / 3, target.height() / 3, target.width() - 6, target.height() / 3);
    return fallback;
}

QImage PreviewImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    if (id.startsWith("icon/")) {
        const QString mime = QUrl::fromPercentEncoding(id.mid(5).toUtf8());
        QImage result = icon(mime, requestedSize);
        if (size) *size = result.size();
        return result;
    }
    const QString key = id.startsWith("preview/") ? id.mid(8) : id;
    PreviewResult preview;
    if (!cache_->lookup(key, &preview)) return {};
    QImage image = preview.image;
    if (requestedSize.isValid()) image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (size) *size = image.size();
    return image;
}

} // namespace purrfind
