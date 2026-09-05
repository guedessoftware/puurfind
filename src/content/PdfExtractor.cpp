#include "content/PdfExtractor.h"

#include "content/TextNormalizer.h"

#include <QJsonDocument>
#include <QJsonObject>

#ifdef PURRFIND_WITH_PDF
#include <poppler-qt6.h>
#endif

namespace purrfind {

bool PdfExtractor::supports(const FileRecord &file) const { return file.extension == "pdf"; }

ExtractResult PdfExtractor::extract(const FileRecord &file, const ExtractionLimits &limits,
                                    const CancellationToken &cancel) const
{
    ExtractResult result;
#ifdef PURRFIND_WITH_PDF
    auto document = Poppler::Document::load(file.path);
    if (!document) { result.error = "invalid or truncated PDF"; return result; }
    if (document->isLocked()) {
        result.state = ContentState::Encrypted;
        result.error = "password-protected PDF";
        return result;
    }
    result.pageCount = document->numPages();
    result.title = document->info("Title");
    result.author = document->info("Author");
    result.subject = document->info("Subject");
    result.keywords = document->info("Keywords");
    const auto pdfVersion = document->getPdfVersion();
    result.detailsJson = QString::fromUtf8(QJsonDocument(QJsonObject{
        {"created", document->date("CreationDate").toString(Qt::ISODate)},
        {"modified", document->date("ModDate").toString(Qt::ISODate)},
        {"pdfVersion", QString("%1.%2").arg(pdfVersion.major).arg(pdfVersion.minor)},
        {"encrypted", false},
    }).toJson(QJsonDocument::Compact));
    QString text;
    for (int index = 0; index < result.pageCount; ++index) {
        if (cancel.isCancelled()) { result.error = "cancelled"; return result; }
        auto page = document->page(index);
        if (!page) continue;
        const QString pageText = page->text(QRectF(), Poppler::Page::PhysicalLayout);
        result.pages.append(pageText);
        text += pageText;
        text += "\n\n";
        if (text.size() * 2LL > limits.maximumTextBytes * 2) {
            result.truncated = true;
            break;
        }
    }
    result.bytesRead = file.size;
    result.text = TextNormalizer::normalize(std::move(text), limits.maximumTextBytes, &result.truncated);
    result.state = result.text.isEmpty() ? ContentState::NoText : ContentState::Indexed;
#else
    Q_UNUSED(file); Q_UNUSED(limits); Q_UNUSED(cancel);
    result.state = ContentState::Unsupported;
    result.error = "PDF support was disabled at build time";
#endif
    return result;
}

} // namespace purrfind
