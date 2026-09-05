#include "content/TextNormalizer.h"

#include <QRegularExpression>

namespace purrfind {

bool TextNormalizer::appearsBinary(const QByteArray &sample)
{
    if (sample.contains('\0')) return true;
    if (sample.isEmpty()) return false;
    int controls = 0;
    for (const unsigned char byte : sample) {
        if (byte < 0x09 || (byte > 0x0d && byte < 0x20)) ++controls;
    }
    return controls * 20 > sample.size();
}

QString TextNormalizer::normalize(QString text, qint64 maximumUtf8Bytes, bool *truncated)
{
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    text.replace(QChar(0), QChar(' '));
    static const QRegularExpression horizontal("[\\t\\x{00a0} ]+");
    static const QRegularExpression vertical("\\n{3,}");
    text.replace(horizontal, " ");
    text.replace(vertical, "\n\n");
    text = text.trimmed();
    bool cut = false;
    if (maximumUtf8Bytes > 0 && text.toUtf8().size() > maximumUtf8Bytes) {
        int low = 0;
        int high = text.size();
        while (low < high) {
            const int middle = (low + high + 1) / 2;
            if (text.left(middle).toUtf8().size() <= maximumUtf8Bytes) low = middle;
            else high = middle - 1;
        }
        text.truncate(low);
        cut = true;
    }
    if (truncated) *truncated = cut;
    return text;
}

} // namespace purrfind

