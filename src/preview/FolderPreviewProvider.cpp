#include "preview/FolderPreviewProvider.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLocale>
#include <QMimeDatabase>

namespace purrfind {

PreviewResult FolderPreviewProvider::generate(const PreviewRequest &request,
                                               const CancellationToken &cancel) const
{
    PreviewResult result; result.provider = id(); result.title = request.file.name;
    QDir directory(request.file.path);
    if (!directory.exists()) { result.error = "folder is unavailable"; return result; }
    int count = 0;
    bool approximate = false;
    constexpr int previewLimit = 120;
    QStringList entries;
    QDirIterator iterator(request.file.path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                          QDirIterator::NoIteratorFlags);
    while (iterator.hasNext()) {
        iterator.next();
        if (cancel.isCancelled()) { result.error = "cancelled"; return result; }
        const QFileInfo info = iterator.fileInfo();
        if (entries.size() < previewLimit) {
            QString marker = QStringLiteral("[[PFFILE]]");
            if (info.isDir()) {
                marker = QStringLiteral("[[PFFOLDER]]");
            } else {
                const QString mime = QMimeDatabase().mimeTypeForFile(info).name();
                if (mime.startsWith(QStringLiteral("image/"))) marker = QStringLiteral("[[PFIMAGE]]");
                else if (mime.startsWith(QStringLiteral("video/"))) marker = QStringLiteral("[[PFVIDEO]]");
                else if (mime.startsWith(QStringLiteral("application/zip"))
                         || mime.startsWith(QStringLiteral("application/x-7z"))
                         || mime.startsWith(QStringLiteral("application/x-rar"))
                         || mime.startsWith(QStringLiteral("application/x-tar"))) {
                    marker = QStringLiteral("[[PFARCHIVE]]");
                } else if (mime.startsWith(QStringLiteral("text/"))
                           || mime == QStringLiteral("application/pdf")
                           || mime.startsWith(QStringLiteral("application/vnd."))
                           || mime.contains(QStringLiteral("opendocument"))) {
                    marker = QStringLiteral("[[PFDOC]]");
                }
            }
            const QString size = info.isDir() ? QStringLiteral("—") : QLocale().formattedDataSize(info.size());
            // The marker is converted to a colored icon by the QML preview;
            // do not duplicate the generic "[Arquivo]/[Pasta]" labels.
            entries.append(QString("%1%2  (%3)").arg(marker, info.fileName(), size));
        }
        if (++count >= 1000) { approximate = true; break; }
    }
    result.details = QString("%1%2 immediate items\nNon-recursive folder preview")
        .arg(approximate ? "At least " : QString()).arg(count);
    QStringList preview;
    preview.append(QString("Conteúdo imediato · %1%2 itens")
                       .arg(approximate ? "pelo menos " : QString()).arg(count));
    preview.append(QString());
    preview.append(entries);
    if (approximate || count > previewLimit)
        preview.append(QString("… e mais %1 item(ns)").arg(approximate ? "" : QString::number(count - previewLimit)));
    result.text = preview.join('\n');
    return result;
}

} // namespace purrfind
