#include "preview/OfficePreviewProvider.h"

#include "content/ExtractorRegistry.h"
#include "preview/TextPreviewProvider.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace purrfind {

bool OfficePreviewProvider::supports(const FileRecord &file) const
{
    return QStringList{"docx","xlsx","pptx","odt","ods","odp"}.contains(file.extension);
}

PreviewResult OfficePreviewProvider::generate(const PreviewRequest &request,
                                               const CancellationToken &cancel) const
{
    PreviewResult result; result.provider = id(); result.title = request.file.name;
    ExtractorRegistry registry;
    const QStringList types{request.file.extension};
    const ContentExtractor *extractor = registry.extractorFor(request.file, types);
    if (!extractor) { result.error = "document preview support disabled"; return result; }
    ExtractionLimits limits;
    limits.maximumTextBytes = 512 * 1024;
    const ExtractResult extracted = extractor->extract(request.file, limits, cancel);
    if (cancel.isCancelled()) { result.error = "cancelled"; return result; }
    if (extracted.state != ContentState::Indexed) {
        result.error = extracted.error.isEmpty() ? "no previewable document text" : extracted.error;
        return result;
    }
    result.title = extracted.title.isEmpty() ? request.file.name : extracted.title;
    result.text = previewExcerpt(extracted.text, request.query);
    QStringList details;
    if (!extracted.author.isEmpty()) details << "Author: " + extracted.author;
    if (request.file.extension == "xlsx" || request.file.extension == "ods") details << "Spreadsheet text and sheet names";
    else if (request.file.extension == "pptx" || request.file.extension == "odp") details << "Slides and notes";
    else details << "Document text";
    const QJsonObject rich = QJsonDocument::fromJson(extracted.detailsJson.toUtf8()).object();
    if (!rich.value("created").toString().isEmpty()) details << "Created: " + rich.value("created").toString();
    if (!rich.value("modified").toString().isEmpty()) details << "Modified: " + rich.value("modified").toString();
    if (!rich.value("lastModifiedBy").toString().isEmpty()) details << "Last modified by: " + rich.value("lastModifiedBy").toString();
    if (!rich.value("application").toString().isEmpty()) details << "Application: " + rich.value("application").toString();
    if (!rich.value("company").toString().isEmpty()) details << "Company: " + rich.value("company").toString();
    result.details = details.join('\n');
    return result;
}

} // namespace purrfind
