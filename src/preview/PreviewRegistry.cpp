#include "preview/PreviewRegistry.h"

#include <QCryptographicHash>
#include <QElapsedTimer>

namespace purrfind {

const PreviewProvider *PreviewRegistry::providerFor(const FileRecord &file) const
{
    if (file.directory) return &folder_;
    if (pdf_.supports(file)) return &pdf_;
    if (image_.supports(file)) return &image_;
    if (text_.supports(file)) return &text_;
    if (office_.supports(file)) return &office_;
    return &generic_;
}

PreviewResult PreviewRegistry::generate(const PreviewRequest &request,
                                        const CancellationToken &cancel) const
{
    const PreviewProvider *provider = providerFor(request.file);
    QString key = cache_->keyFor(request);
    // Textual excerpts depend on the query, unlike rendered image/PDF pixels.
    // Keep those RAM entries distinct without duplicating persistent thumbnails.
    if (provider->id() == "text" || provider->id() == "office") {
        key += ':' + QString::fromLatin1(QCryptographicHash::hash(
            request.query.toCaseFolded().toUtf8(), QCryptographicHash::Sha256).toHex());
    }
    PreviewResult result;
    if (cache_->lookup(key, &result)) {
        if (result.provider.isEmpty()) result.provider = provider->id();
        if (result.title.isEmpty()) result.title = request.file.name;
        if (result.page <= 0) result.page = request.page;
        if (result.pageCount <= 0) result.pageCount = request.pageCount;
        // Snippets are supplied by the current search result and are deliberately
        // excluded from the persistent thumbnail sidecar.
        if (provider->id() == "pdf" || provider->id() == "image") result.text = request.snippet;
        return result;
    }
    QElapsedTimer timer; timer.start();
    result = provider->generate(request, cancel);
    result.generationMilliseconds = timer.elapsed();
    if (!cancel.isCancelled() && result.error != "cancelled") cache_->store(key, result);
    return result;
}

} // namespace purrfind
