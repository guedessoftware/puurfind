#include "content/PlainTextExtractor.h"

#include "content/TextNormalizer.h"

#include <QFile>
#include <QStringDecoder>

namespace purrfind {

bool PlainTextExtractor::supports(const FileRecord &file) const
{
    return QStringList{"txt", "md", "markdown"}.contains(file.extension)
        || file.type.startsWith("text/");
}

ExtractResult PlainTextExtractor::extract(const FileRecord &file, const ExtractionLimits &limits,
                                          const CancellationToken &cancel) const
{
    ExtractResult result;
    QFile input(file.path);
    if (!input.open(QIODevice::ReadOnly)) {
        result.error = input.errorString();
        return result;
    }
    QByteArray bytes;
    bytes.reserve(static_cast<qsizetype>(qMin(limits.maximumTextBytes, file.size)));
    while (!input.atEnd() && bytes.size() <= limits.maximumTextBytes) {
        if (cancel.isCancelled()) { result.error = "cancelled"; return result; }
        bytes += input.read(qMin<qint64>(64 * 1024, limits.maximumTextBytes + 1 - bytes.size()));
    }
    result.bytesRead = bytes.size();
    result.truncated = !input.atEnd() || bytes.size() > limits.maximumTextBytes;
    if (bytes.size() > limits.maximumTextBytes) bytes.truncate(limits.maximumTextBytes);
    if (TextNormalizer::appearsBinary(bytes.left(8192))) {
        result.state = ContentState::Unsupported;
        result.error = "binary data detected";
        return result;
    }
    if (bytes.startsWith("\xef\xbb\xbf")) bytes.remove(0, 3);
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder.decode(bytes);
    if (decoder.hasError()) {
        result.state = ContentState::Failed;
        result.error = "invalid UTF-8";
        return result;
    }
    result.text = TextNormalizer::normalize(std::move(text), limits.maximumTextBytes, &result.truncated);
    result.state = result.text.isEmpty() ? ContentState::NoText : ContentState::Indexed;
    return result;
}

} // namespace purrfind

