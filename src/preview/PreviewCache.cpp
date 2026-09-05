#include "preview/PreviewCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>

namespace purrfind {

PreviewCache::PreviewCache(int memoryMiB, int diskMiB, const QString &directory)
    : memory_(memoryMiB * 1024),
      directory_(directory.isEmpty()
          ? QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/purrfind/previews"
          : directory),
      diskLimitBytes_(static_cast<qint64>(diskMiB) * 1024 * 1024)
{
    QDir().mkpath(directory_);
    QFile::setPermissions(QFileInfo(directory_).absolutePath(),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QFile::setPermissions(directory_, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

QString PreviewCache::keyFor(const PreviewRequest &request) const
{
    const QByteArray revision = QString("%1|%2|%3|%4|%5|%6x%7")
        .arg(request.file.id).arg(request.file.mtime).arg(request.file.size).arg(request.file.inode)
        .arg(request.page).arg(request.targetSize.width()).arg(request.targetSize.height()).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(revision, QCryptographicHash::Sha256).toHex());
}

bool PreviewCache::lookup(const QString &key, PreviewResult *result)
{
    QMutexLocker lock(&mutex_);
    if (auto *cached = memory_.object(key)) {
        *result = *cached;
        result->cacheHit = true;
        return true;
    }
    QImage image(directory_ + '/' + key + ".png");
    if (image.isNull()) return false;
    result->image = image;
    QFile metadataFile(directory_ + '/' + key + ".json");
    if (metadataFile.open(QIODevice::ReadOnly)) {
        const QJsonObject metadata = QJsonDocument::fromJson(metadataFile.readAll()).object();
        result->provider = metadata.value("provider").toString();
        result->title = metadata.value("title").toString();
        result->details = metadata.value("details").toString();
        result->page = metadata.value("page").toInt();
        result->pageCount = metadata.value("pageCount").toInt();
    }
    result->cacheHit = true;
    const int cost = qMax(1, static_cast<int>(image.sizeInBytes() / 1024));
    memory_.insert(key, new PreviewResult(*result), cost);
    return true;
}

void PreviewCache::store(const QString &key, const PreviewResult &result)
{
    QMutexLocker lock(&mutex_);
    const int cost = qMax(1, static_cast<int>((result.image.sizeInBytes() + result.text.size() * 2LL) / 1024));
    memory_.insert(key, new PreviewResult(result), cost);
    if (!result.image.isNull()) {
        const QString path = directory_ + '/' + key + ".png";
        result.image.save(path, "PNG");
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QSaveFile metadataFile(directory_ + '/' + key + ".json");
        if (metadataFile.open(QIODevice::WriteOnly)) {
            metadataFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            metadataFile.write(QJsonDocument(QJsonObject{
                {"provider", result.provider}, {"title", result.title}, {"details", result.details},
                {"page", result.page}, {"pageCount", result.pageCount},
            }).toJson(QJsonDocument::Compact));
            metadataFile.commit();
        }
        if (++storesSincePrune_ >= 20) { storesSincePrune_ = 0; pruneDisk(); }
    }
}

void PreviewCache::pruneDisk()
{
    QDir directory(directory_);
    QFileInfoList files = directory.entryInfoList({"*.png"}, QDir::Files, QDir::Time | QDir::Reversed);
    qint64 total = 0;
    for (const auto &file : files) total += file.size();
    for (const auto &file : files) {
        if (total <= diskLimitBytes_) break;
        total -= file.size();
        QFile::remove(file.absoluteFilePath());
        QFile::remove(directory_.isEmpty() ? QString() : directory_ + '/' + file.completeBaseName() + ".json");
    }
}

void PreviewCache::clear()
{
    QMutexLocker lock(&mutex_);
    memory_.clear();
    const QDir directory(directory_);
    for (const auto &file : directory.entryList({"*.png", "*.json"}, QDir::Files))
        QFile::remove(directory.absoluteFilePath(file));
}

} // namespace purrfind
