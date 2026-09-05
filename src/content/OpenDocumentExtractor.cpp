#include "content/OpenDocumentExtractor.h"

#include "content/ArchiveReader.h"
#include "content/TextNormalizer.h"

namespace purrfind {

bool OpenDocumentExtractor::supports(const FileRecord &file) const
{
    return QStringList{"odt", "ods", "odp"}.contains(file.extension);
}

ExtractResult OpenDocumentExtractor::extract(const FileRecord &file, const ExtractionLimits &limits,
                                             const CancellationToken &cancel) const
{
    ExtractResult result;
#ifdef PURRFIND_WITH_OFFICE
    const auto archive = ArchiveReader::read(file.path,
        [](const QString &name) { return name == "content.xml" || name == "meta.xml"; }, limits, cancel);
    result.bytesRead = archive.bytesRead;
    if (!archive.error.isEmpty()) { result.error = archive.error; return result; }
    if (archive.encrypted) { result.state = ContentState::Encrypted; result.error = "encrypted package"; return result; }
    if (!archive.entries.contains("content.xml")) { result.error = "content.xml is missing"; return result; }
    QString xmlError;
    result.text = ArchiveReader::xmlText(archive.entries.value("content.xml"), &xmlError);
    if (!xmlError.isEmpty()) { result.error = xmlError; return result; }
    const auto metadata = ArchiveReader::xmlMetadata(archive.entries.value("meta.xml"));
    result.title = metadata.value("title");
    result.author = metadata.value("creator");
    result.subject = metadata.value("subject");
    result.keywords = metadata.value("keyword");
    result.text = TextNormalizer::normalize(std::move(result.text), limits.maximumTextBytes, &result.truncated);
    result.state = result.text.isEmpty() ? ContentState::NoText : ContentState::Indexed;
#else
    Q_UNUSED(file); Q_UNUSED(limits); Q_UNUSED(cancel);
    result.state = ContentState::Unsupported;
#endif
    return result;
}

} // namespace purrfind
