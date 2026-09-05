#include "content/ExtractorRegistry.h"

#include "content/OfficeExtractor.h"
#include "content/OpenDocumentExtractor.h"
#include "content/PdfExtractor.h"
#include "content/PlainTextExtractor.h"

namespace purrfind {

ExtractorRegistry::ExtractorRegistry()
{
    extractors_.push_back(std::make_unique<PlainTextExtractor>());
#ifdef PURRFIND_WITH_PDF
    extractors_.push_back(std::make_unique<PdfExtractor>());
#endif
#ifdef PURRFIND_WITH_OFFICE
    extractors_.push_back(std::make_unique<OfficeExtractor>());
    extractors_.push_back(std::make_unique<OpenDocumentExtractor>());
#endif
}

const ContentExtractor *ExtractorRegistry::extractorFor(const FileRecord &file,
                                                        const QStringList &enabledTypes) const
{
    if (!enabledTypes.contains(file.extension, Qt::CaseInsensitive)) return nullptr;
    for (const auto &extractor : extractors_) {
        if (extractor->supports(file)) return extractor.get();
    }
    return nullptr;
}

QStringList ExtractorRegistry::availableExtensions() const
{
    QStringList result{"txt", "md", "markdown"};
#ifdef PURRFIND_WITH_PDF
    result << "pdf";
#endif
#ifdef PURRFIND_WITH_OFFICE
    result << "docx" << "xlsx" << "pptx" << "odt" << "ods" << "odp";
#endif
    return result;
}

} // namespace purrfind
