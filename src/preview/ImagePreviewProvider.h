#pragma once

#include "preview/PreviewProvider.h"

namespace purrfind {

class ImagePreviewProvider final : public PreviewProvider {
public:
    QString id() const override { return "image"; }
    bool supports(const FileRecord &file) const override;
    PreviewResult generate(const PreviewRequest &request, const CancellationToken &cancel) const override;
};

} // namespace purrfind
