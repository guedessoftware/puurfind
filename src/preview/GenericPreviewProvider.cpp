#include "preview/GenericPreviewProvider.h"

#include <QDateTime>
#include <QFileInfo>
#include <QLocale>

namespace purrfind {

PreviewResult GenericPreviewProvider::generate(const PreviewRequest &request,
                                                const CancellationToken &cancel) const
{
    PreviewResult result; result.provider = id(); result.title = request.file.name;
    if (cancel.isCancelled()) { result.error = "cancelled"; return result; }
    QFileInfo info(request.file.path);
    const QString permissions = QString::number(static_cast<int>(info.permissions()), 8);
    result.details = QString("Type: %1\nSize: %2\nModified: %3\nOwner: %4\nPermissions: %5\nInode: %6\nDevice: %7%8")
        .arg(request.file.type.isEmpty() ? "File" : request.file.type,
             request.file.directory ? QStringLiteral("—") : QLocale().formattedDataSize(request.file.size),
             QLocale().toString(info.lastModified(), QLocale::LongFormat),
             info.owner(), permissions)
        .arg(request.file.inode).arg(request.file.device)
        .arg(request.file.symlink ? "\nSymbolic link" : QString());
    return result;
}

} // namespace purrfind
