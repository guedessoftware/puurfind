#include "content/ArchiveReader.h"

#ifdef PURRFIND_WITH_OFFICE
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <zip.h>
#endif

#include <QFile>

namespace purrfind {
namespace {

#ifdef PURRFIND_WITH_OFFICE
bool unsafeName(const QString &name)
{
    if (name.startsWith('/') || name.startsWith('\\')) return true;
    const auto parts = name.split('/', Qt::SkipEmptyParts);
    return parts.contains("..");
}

void appendText(xmlNode *node, QString *output)
{
    for (xmlNode *current = node; current; current = current->next) {
        if (current->type == XML_TEXT_NODE || current->type == XML_CDATA_SECTION_NODE) {
            if (current->content) *output += QString::fromUtf8(reinterpret_cast<const char *>(current->content));
        } else if (current->type == XML_ELEMENT_NODE) {
            for (xmlAttr *attribute = current->properties; attribute; attribute = attribute->next) {
                const QByteArray attributeName(reinterpret_cast<const char *>(attribute->name));
                if (attributeName == "name" || attributeName == "value") {
                    xmlChar *value = xmlNodeListGetString(current->doc, attribute->children, 1);
                    if (value) {
                        *output += QString::fromUtf8(reinterpret_cast<const char *>(value)) + ' ';
                        xmlFree(value);
                    }
                }
            }
            appendText(current->children, output);
            const QByteArray name(reinterpret_cast<const char *>(current->name));
            if (name == "p" || name == "tr" || name == "row" || name == "text:p"
                || name == "h" || name == "br") *output += '\n';
            else *output += ' ';
        }
    }
}

xmlDocPtr parseXml(const QByteArray &xml)
{
    if (xml.isEmpty() || xml.size() > 64 * 1024 * 1024) return nullptr;
    xmlParserCtxtPtr context = xmlNewParserCtxt();
    if (!context) return nullptr;
    int options = XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING
        | XML_PARSE_COMPACT | XML_PARSE_NOCDATA;
#if LIBXML_VERSION >= 21400
    options |= XML_PARSE_NO_XXE;
#endif
    xmlDocPtr document = xmlCtxtReadMemory(context, xml.constData(), xml.size(),
                                           "document.xml", nullptr, options);
    xmlFreeParserCtxt(context);
    return document;
}
#endif

} // namespace

ArchiveResult ArchiveReader::read(const QString &path, const Selector &selector,
                                  const ExtractionLimits &limits, const CancellationToken &cancel)
{
    ArchiveResult result;
#ifdef PURRFIND_WITH_OFFICE
    int zipError = 0;
    const QByteArray encoded = QFile::encodeName(path);
    zip_t *archive = zip_open(encoded.constData(), ZIP_RDONLY, &zipError);
    if (!archive) { result.error = "invalid or truncated ZIP container"; return result; }
    const zip_int64_t count = zip_get_num_entries(archive, 0);
    if (count < 0 || count > limits.maximumArchiveEntries) {
        result.error = "archive entry limit exceeded";
        zip_close(archive);
        return result;
    }
    qint64 totalUncompressed = 0;
    for (zip_uint64_t index = 0; index < static_cast<zip_uint64_t>(count); ++index) {
        if (cancel.isCancelled()) { result.error = "cancelled"; break; }
        zip_stat_t status;
        zip_stat_init(&status);
        if (zip_stat_index(archive, index, ZIP_FL_ENC_GUESS, &status) != 0 || !status.name) continue;
        const QString name = QString::fromUtf8(status.name);
        if (unsafeName(name)) { result.error = "unsafe archive entry path"; break; }
        if ((status.valid & ZIP_STAT_ENCRYPTION_METHOD) && status.encryption_method != ZIP_EM_NONE) {
            result.encrypted = true;
            continue;
        }
        if (!(status.valid & ZIP_STAT_SIZE) || status.size > static_cast<zip_uint64_t>(limits.maximumArchiveEntryBytes)) {
            result.error = "archive entry size limit exceeded";
            break;
        }
        totalUncompressed += static_cast<qint64>(status.size);
        if (totalUncompressed > limits.maximumArchiveBytes) {
            result.error = "archive uncompressed size limit exceeded";
            break;
        }
        if ((status.valid & ZIP_STAT_COMP_SIZE) && status.comp_size > 0
            && status.size / status.comp_size > static_cast<zip_uint64_t>(limits.maximumCompressionRatio)) {
            result.error = "archive compression ratio limit exceeded";
            break;
        }
        if (!selector(name) || name.endsWith('/')) continue;
        zip_file_t *entry = zip_fopen_index(archive, index, 0);
        if (!entry) { result.error = "cannot open archive entry"; break; }
        QByteArray data;
        data.resize(static_cast<qsizetype>(status.size));
        zip_int64_t offset = 0;
        while (offset < static_cast<zip_int64_t>(status.size)) {
            const zip_int64_t amount = zip_fread(entry, data.data() + offset, status.size - offset);
            if (amount <= 0) { result.error = "truncated archive entry"; break; }
            offset += amount;
        }
        zip_fclose(entry);
        if (!result.error.isEmpty()) break;
        result.bytesRead += data.size();
        result.entries.insert(name, std::move(data));
    }
    zip_close(archive);
#else
    Q_UNUSED(path); Q_UNUSED(selector); Q_UNUSED(limits); Q_UNUSED(cancel);
    result.error = "Office support was disabled at build time";
#endif
    return result;
}

QString ArchiveReader::xmlText(const QByteArray &xml, QString *error)
{
#ifdef PURRFIND_WITH_OFFICE
    xmlDocPtr document = parseXml(xml);
    if (!document) { if (error) *error = "invalid XML"; return {}; }
    QString output;
    appendText(xmlDocGetRootElement(document), &output);
    xmlFreeDoc(document);
    return output;
#else
    Q_UNUSED(xml); if (error) *error = "Office support disabled"; return {};
#endif
}

QHash<QString, QString> ArchiveReader::xmlMetadata(const QByteArray &xml, QString *error)
{
    QHash<QString, QString> values;
#ifdef PURRFIND_WITH_OFFICE
    xmlDocPtr document = parseXml(xml);
    if (!document) { if (error) *error = "invalid metadata XML"; return values; }
    std::function<void(xmlNode *)> visit = [&](xmlNode *node) {
        for (xmlNode *current = node; current; current = current->next) {
            if (current->type == XML_ELEMENT_NODE && current->children
                && current->children->type == XML_TEXT_NODE && current->children->content) {
                values.insert(QString::fromUtf8(reinterpret_cast<const char *>(current->name)).toLower(),
                              QString::fromUtf8(reinterpret_cast<const char *>(current->children->content)));
            }
            visit(current->children);
        }
    };
    visit(xmlDocGetRootElement(document));
    xmlFreeDoc(document);
#else
    Q_UNUSED(xml); if (error) *error = "Office support disabled";
#endif
    return values;
}

} // namespace purrfind
