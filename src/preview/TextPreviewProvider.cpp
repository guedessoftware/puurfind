#include "preview/TextPreviewProvider.h"

#include <QFile>
#include <QRegularExpression>

namespace purrfind {

QString previewExcerpt(const QString &text, const QString &query, int maximumCharacters)
{
    QString needle;
    const auto words = query.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    for (QString word : words) {
        if (word.contains(':')) word = word.mid(word.indexOf(':') + 1);
        word.remove('"');
        if (word.size() >= 2) { needle = word; break; }
    }
    int position = needle.isEmpty() ? -1 : text.indexOf(needle, 0, Qt::CaseInsensitive);
    const int start = position < 0 ? 0 : qMax(0, position - maximumCharacters / 3);
    QString excerpt = text.mid(start, maximumCharacters).trimmed();
    if (start > 0) excerpt.prepend("…\n");
    if (start + maximumCharacters < text.size()) excerpt.append("\n…");
    return excerpt;
}

bool TextPreviewProvider::supports(const FileRecord &file) const
{
    return file.type.startsWith("text/") || QStringList{"txt","md","markdown"}.contains(file.extension);
}

PreviewResult TextPreviewProvider::generate(const PreviewRequest &request,
                                             const CancellationToken &cancel) const
{
    PreviewResult result; result.provider = id(); result.title = request.file.name;
    QFile file(request.file.path);
    if (!file.open(QIODevice::ReadOnly)) { result.error = file.errorString(); return result; }
    QByteArray bytes;
    while (!file.atEnd() && bytes.size() < 256 * 1024) {
        if (cancel.isCancelled()) { result.error = "cancelled"; return result; }
        bytes += file.read(qMin(32 * 1024, 256 * 1024 - bytes.size()));
    }
    if (bytes.startsWith("\xef\xbb\xbf")) bytes.remove(0, 3);
    result.text = previewExcerpt(QString::fromUtf8(bytes), request.query);
    if (result.text.isEmpty()) result.error = "no previewable text";
    result.details = request.file.extension == "md" || request.file.extension == "markdown"
        ? "Markdown · plain structured preview" : "Plain text preview";
    return result;
}

} // namespace purrfind
