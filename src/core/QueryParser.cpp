#include "core/QueryParser.h"

#include <QRegularExpression>

namespace purrfind {

qint64 QueryParser::parseSize(const QString &value, bool *ok)
{
    static const QRegularExpression pattern(
        R"(^\s*(\d+(?:\.\d+)?)\s*(B|KB|KIB|MB|MIB|GB|GIB|TB|TIB)?\s*$)",
        QRegularExpression::CaseInsensitiveOption);
    const auto match = pattern.match(value);
    if (!match.hasMatch()) {
        if (ok) *ok = false;
        return 0;
    }
    double multiplier = 1.0;
    const auto unit = match.captured(2).toUpper();
    if (unit == "KB" || unit == "KIB") multiplier = 1024.0;
    else if (unit == "MB" || unit == "MIB") multiplier = 1024.0 * 1024.0;
    else if (unit == "GB" || unit == "GIB") multiplier = 1024.0 * 1024.0 * 1024.0;
    else if (unit == "TB" || unit == "TIB") multiplier = 1024.0 * 1024.0 * 1024.0 * 1024.0;
    if (ok) *ok = true;
    return static_cast<qint64>(match.captured(1).toDouble() * multiplier);
}

ParsedQuery QueryParser::parse(const QString &input)
{
    ParsedQuery query;
    QStringList freeText;
    QStringList freeFts;
    auto quoteFts = [](QString value) {
        value.replace('"', "\"\"");
        return '"' + value + '"';
    };
    auto parseIntegerFilter = [](QString value, int *minimum, int *maximum) {
        const bool greater = value.startsWith('>');
        const bool less = value.startsWith('<');
        if (greater || less) value.remove(0, 1);
        bool ok = false;
        const int number = value.toInt(&ok);
        if (!ok || number < 0) return false;
        if (greater) *minimum = number;
        else if (less) *maximum = number;
        else *minimum = *maximum = number;
        return true;
    };
    static const QRegularExpression tokenPattern(R"((?:[^\s"]+|"[^"]*")+)");
    auto iterator = tokenPattern.globalMatch(input.trimmed());
    while (iterator.hasNext()) {
        QString token = iterator.next().captured(0);
        const bool tokenWasQuoted = token.startsWith('"') && token.endsWith('"');
        if (token.startsWith('"') && token.endsWith('"')) token = token.mid(1, token.size() - 2);
        const qsizetype colon = token.indexOf(':');
        const QString key = colon > 0 ? token.left(colon).toLower() : QString();
        QString value = colon > 0 ? token.mid(colon + 1) : QString();
        const bool valueWasQuoted = value.startsWith('"') && value.endsWith('"');
        if (value.startsWith('"') && value.endsWith('"')) value = value.mid(1, value.size() - 2);

        if (key == "type" || key == "ext") {
            const QString type = value.toLower().remove(QRegularExpression("^\\."));
            if (key == "type" && QStringList{"image", "document", "video", "other"}.contains(type))
                query.category = type;
            else
                query.extension = type;
        } else if (key == "folder" || key == "in") {
            query.folder = value;
        } else if (key == "modified") {
            const auto lowered = value.toLower();
            if (lowered == "today" || lowered == "1d") query.modified = ModifiedFilter::Today;
            else if (lowered == "7d") query.modified = ModifiedFilter::Days7;
            else if (lowered == "30d") query.modified = ModifiedFilter::Days30;
            else freeText.append(token);
        } else if (key == "size") {
            const bool greater = value.startsWith('>');
            const bool less = value.startsWith('<');
            if (greater || less) value.remove(0, 1);
            bool valid = false;
            const qint64 bytes = parseSize(value, &valid);
            if (valid && greater) query.minimumSize = bytes;
            else if (valid && less) query.maximumSize = bytes;
            else freeText.append(token);
        } else if (key == "kind" && value.compare("folder", Qt::CaseInsensitive) == 0) {
            query.directoriesOnly = true;
        } else if (key == "kind" && value.compare("file", Qt::CaseInsensitive) == 0) {
            query.filesOnly = true;
        } else if (key == "category") {
            const auto category = value.toLower();
            if (QStringList{"image", "document", "video", "other", "content"}.contains(category))
                query.category = category;
            else
                freeText.append(token);
        } else if (key == "author") {
            query.author = value;
        } else if (key == "camera") {
            query.camera = value;
        } else if (key == "source" || key == "match") {
            const QString source = value.toLower();
            if (source == "ocr" || source == "native") query.source = source;
            else freeText.append(token);
        } else if (key == "pages") {
            if (!parseIntegerFilter(value, &query.minimumPages, &query.maximumPages)) freeText.append(token);
        } else if (key == "width") {
            if (!parseIntegerFilter(value, &query.minimumWidth, &query.maximumWidth)) freeText.append(token);
        } else if (key == "height") {
            if (!parseIntegerFilter(value, &query.minimumHeight, &query.maximumHeight)) freeText.append(token);
        } else if (key == "name" || key == "path" || key == "content") {
            query.scope = key == "name" ? SearchScope::Name
                        : key == "path" ? SearchScope::Path : SearchScope::Content;
            query.text = value;
            query.phrase = valueWasQuoted;
            query.ftsExpression = quoteFts(value);
        } else {
            freeText.append(token);
            freeFts.append(quoteFts(token));
            if (tokenWasQuoted) query.phrase = true;
        }
    }
    if (query.scope == SearchScope::Unified) {
        query.text = freeText.join(' ').trimmed();
        query.ftsExpression = freeFts.join(" AND ");
    }
    return query;
}

} // namespace purrfind
