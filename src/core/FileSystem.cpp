#include "core/FileSystem.h"

#include <QDir>
#include <QFileInfo>
#include <QMimeDatabase>

#include <sys/stat.h>

namespace purrfind {

QString FileSystem::normalizePath(const QString &path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    return QDir::cleanPath(absolute);
}

bool FileSystem::isWithin(const QString &path, const QString &directory)
{
    const QString candidate = normalizePath(path);
    QString base = normalizePath(directory);
    if (candidate == base) return true;
    if (!base.endsWith('/')) base += '/';
    return candidate.startsWith(base);
}

bool FileSystem::isExcluded(const QString &path, const QStringList &exclusions)
{
    for (const auto &excluded : exclusions) {
        if (isWithin(path, excluded)) return true;
    }
    return false;
}

QString FileSystem::basicType(const QString &path, bool directory)
{
    if (directory) return "folder";
    static QMimeDatabase mimeDatabase;
    return mimeDatabase.mimeTypeForFile(path, QMimeDatabase::MatchExtension).name();
}

std::optional<FileRecord> FileSystem::inspect(const QString &path, const QString &root,
                                               qint64 generation, QString *error)
{
    const QByteArray encoded = QFile::encodeName(path);
    struct stat info {};
    if (::lstat(encoded.constData(), &info) != 0) {
        if (error) *error = QString::fromLocal8Bit(strerror(errno));
        return std::nullopt;
    }
    QFileInfo fileInfo(path);
    FileRecord record;
    record.name = fileInfo.fileName();
    if (record.name.isEmpty()) record.name = QDir(path).dirName();
    record.path = normalizePath(path);
    record.parentPath = fileInfo.dir().absolutePath();
    record.extension = fileInfo.suffix().toLower();
    record.directory = S_ISDIR(info.st_mode);
    record.symlink = S_ISLNK(info.st_mode);
    record.hidden = fileInfo.isHidden() || record.name.startsWith('.');
    record.type = basicType(record.path, record.directory);
    record.size = record.directory ? 0 : static_cast<qint64>(info.st_size);
    record.mtime = static_cast<qint64>(info.st_mtim.tv_sec);
    record.ctime = static_cast<qint64>(info.st_ctim.tv_sec);
    record.inode = static_cast<quint64>(info.st_ino);
    record.device = static_cast<quint64>(info.st_dev);
    record.root = normalizePath(root);
    record.scanGeneration = generation;
    return record;
}

} // namespace purrfind

