#pragma once

#include "preview/PreviewProvider.h"

namespace purrfind {

class TextPreviewProvider final : public PreviewProvider {
public:
    QString id() const override { return "text"; }
    bool supports(const FileRecord &file) const override;
    PreviewResult generate(const PreviewRequest &request, const CancellationToken &cancel) const override;
};

QString previewExcerpt(const QString &text, const QString &query, int maximumCharacters = 6000);

} // namespace purrfind
