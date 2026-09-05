#include "indexer/Crawler.h"

#include "core/Database.h"
#include "core/FileSystem.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <filesystem>

namespace purrfind {

CrawlResult Crawler::crawl(const QString &databasePath, const QString &root,
                           const QStringList &exclusions, qint64 generation,
                           const Progress &progress, const std::atomic_bool *cancelled)
{
    CrawlResult result;
    Database database;
    if (!database.open(databasePath, false, &result.error) || !database.migrate(&result.error))
        return result;

    const QString normalizedRoot = FileSystem::normalizePath(root);
    QFileInfo rootInfo(normalizedRoot);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        database.markRootOffline(normalizedRoot, true, nullptr);
        result.error = QString("Root is unavailable: %1").arg(normalizedRoot);
        return result;
    }
    database.markRootOffline(normalizedRoot, false, nullptr);

    QVector<FileRecord> batch;
    batch.reserve(512);
    auto flush = [&]() {
        if (batch.isEmpty()) return true;
        if (!database.upsertBatch(batch, &result.error)) return false;
        result.indexed += batch.size();
        batch.clear();
        return true;
    };

    if (auto inspected = FileSystem::inspect(normalizedRoot, normalizedRoot, generation))
        batch.append(*inspected);

    std::error_code iteratorError;
    const std::filesystem::path nativeRoot(QFile::encodeName(normalizedRoot).constData());
    std::filesystem::recursive_directory_iterator iterator(
        nativeRoot, std::filesystem::directory_options::skip_permission_denied, iteratorError);
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        if (cancelled && cancelled->load()) {
            result.cancelled = true;
            return result;
        }
        const QString path = QFile::decodeName(QByteArray::fromStdString(iterator->path().native()));
        std::error_code typeError;
        const bool directory = iterator->is_directory(typeError);
        const bool symlink = iterator->is_symlink(typeError);
        if (FileSystem::isExcluded(path, exclusions)) {
            if (directory) iterator.disable_recursion_pending();
            ++result.skipped;
            iterator.increment(iteratorError);
            continue;
        }
        // Symlinks themselves are indexed, but QDirIterator does not follow them.
        QString inspectError;
        auto inspected = FileSystem::inspect(path, normalizedRoot, generation, &inspectError);
        if (inspected) batch.append(std::move(*inspected));
        else ++result.skipped;
        if (batch.size() >= 512 && !flush()) return result;
        if (progress && ((result.indexed + batch.size()) % 2048 == 0))
            progress(result.indexed + batch.size(), path);
        if (directory && symlink) iterator.disable_recursion_pending();
        iterator.increment(iteratorError);
        if (iteratorError) {
            ++result.skipped;
            iteratorError.clear();
        }
    }
    if (!flush()) return result;
    if (!database.removeMissing(normalizedRoot, generation, &result.error)) return result;
    if (progress) progress(result.indexed, normalizedRoot);
    return result;
}

} // namespace purrfind
