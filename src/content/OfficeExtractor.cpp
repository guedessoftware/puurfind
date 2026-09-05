#include "content/OfficeExtractor.h"

#include "content/ArchiveReader.h"
#include "content/TextNormalizer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace purrfind {

bool OfficeExtractor::supports(const FileRecord &file) const
{
    return QStringList{"docx", "xlsx", "pptx"}.contains(file.extension);
}

ExtractResult OfficeExtractor::extract(const FileRecord &file, const ExtractionLimits &limits,
                                       const CancellationToken &cancel) const
{
    ExtractResult result;
#ifdef PURRFIND_WITH_OFFICE
    const QString extension = file.extension;
    auto selector = [extension](const QString &name) {
        if (name == "docProps/core.xml" || name == "docProps/app.xml") return true;
        if (extension == "docx")
            return name == "word/document.xml" || name.startsWith("word/header")
                || name.startsWith("word/footer") || name == "word/footnotes.xml"
                || name == "word/endnotes.xml" || name == "word/comments.xml";
        if (extension == "xlsx")
            return name == "xl/sharedStrings.xml" || name == "xl/workbook.xml"
                || name.startsWith("xl/worksheets/sheet");
        return name.startsWith("ppt/slides/slide") || name.startsWith("ppt/notesSlides/notesSlide");
    };
    const auto archive = ArchiveReader::read(file.path, selector, limits, cancel);
    result.bytesRead = archive.bytesRead;
    if (!archive.error.isEmpty()) { result.error = archive.error; return result; }
    if (archive.encrypted) { result.state = ContentState::Encrypted; result.error = "encrypted package"; return result; }
    if (archive.entries.isEmpty()) { result.error = "required XML parts are missing"; return result; }

    const auto metadata = ArchiveReader::xmlMetadata(archive.entries.value("docProps/core.xml"));
    const auto applicationMetadata = ArchiveReader::xmlMetadata(archive.entries.value("docProps/app.xml"));
    result.title = metadata.value("title");
    result.author = metadata.value("creator");
    result.subject = metadata.value("subject");
    result.keywords = metadata.value("keywords");
    QStringList names = archive.entries.keys();
    std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
        return a.localeAwareCompare(b) < 0;
    });
    QString text;
    QJsonArray parts;
    for (const auto &name : names) {
        if (name == "docProps/core.xml" || name == "docProps/app.xml") continue;
        QString xmlError;
        const QString partText = ArchiveReader::xmlText(archive.entries.value(name), &xmlError);
        if (!xmlError.isEmpty()) { result.error = xmlError + " in " + name; return result; }
        if (!partText.trimmed().isEmpty()) {
            text += partText + "\n";
            parts.append(name);
        }
    }
    result.detailsJson = QString::fromUtf8(QJsonDocument(QJsonObject{
        {"parts", parts}, {"created", metadata.value("created")},
        {"modified", metadata.value("modified")},
        {"lastModifiedBy", metadata.value("lastmodifiedby")},
        {"application", applicationMetadata.value("application")},
        {"company", applicationMetadata.value("company")},
    }).toJson(QJsonDocument::Compact));
    result.text = TextNormalizer::normalize(std::move(text), limits.maximumTextBytes, &result.truncated);
    result.state = result.text.isEmpty() ? ContentState::NoText : ContentState::Indexed;
#else
    Q_UNUSED(file); Q_UNUSED(limits); Q_UNUSED(cancel);
    result.state = ContentState::Unsupported;
#endif
    return result;
}

} // namespace purrfind
