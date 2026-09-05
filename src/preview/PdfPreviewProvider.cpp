#include "preview/PdfPreviewProvider.h"

#include <QDateTime>

#ifdef PURRFIND_WITH_PDF
#include <poppler-qt6.h>
#endif

namespace purrfind {

PreviewResult PdfPreviewProvider::generate(const PreviewRequest &request,
                                            const CancellationToken &cancel) const
{
    PreviewResult result;
    result.provider = id(); result.title = request.file.name;
#ifdef PURRFIND_WITH_PDF
    auto document = Poppler::Document::load(request.file.path);
    if (!document) { result.error = "invalid PDF"; return result; }
    if (document->isLocked()) { result.error = "password-protected PDF"; return result; }
    result.pageCount = document->numPages();
    result.page = qBound(1, request.page > 0 ? request.page : 1, qMax(1, result.pageCount));
    if (cancel.isCancelled()) { result.error = "cancelled"; return result; }
    auto page = document->page(result.page - 1);
    if (!page) { result.error = "PDF page unavailable"; return result; }
    const QSizeF points = page->pageSizeF();
    const double scale = qMin(request.targetSize.width() / qMax(1.0, points.width()),
                              request.targetSize.height() / qMax(1.0, points.height()));
    const double dpi = qBound(72.0, 72.0 * scale * 2.0, 180.0);
    result.image = page->renderToImage(dpi, dpi);
    if (cancel.isCancelled()) { result.image = {}; result.error = "cancelled"; return result; }
    if (result.image.isNull()) { result.error = "PDF rendering failed"; return result; }
    result.image = result.image.scaled(request.targetSize * 2, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    result.details = QString("Page %1 of %2").arg(result.page).arg(result.pageCount);
    result.text = request.snippet;
    const QString author = document->info("Author");
    if (!author.isEmpty()) result.details += "\nAuthor: " + author;
    const QString title = document->info("Title");
    if (!title.isEmpty()) result.details += "\nTitle: " + title;
    const QDateTime created = document->date("CreationDate");
    const QDateTime modified = document->date("ModDate");
    if (created.isValid()) result.details += "\nCreated: " + created.toString(Qt::ISODate);
    if (modified.isValid()) result.details += "\nModified: " + modified.toString(Qt::ISODate);
    const auto version = document->getPdfVersion();
    result.details += QString("\nPDF %1.%2 · not encrypted").arg(version.major).arg(version.minor);
#else
    Q_UNUSED(request); Q_UNUSED(cancel);
    result.error = "PDF preview support disabled at build time";
#endif
    return result;
}

} // namespace purrfind
