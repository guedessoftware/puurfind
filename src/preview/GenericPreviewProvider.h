#pragma once

#include "preview/PreviewProvider.h"

namespace purrfind {

class GenericPreviewProvider final : public PreviewProvider {
public:
    QString id() const override { return "generic"; }
    bool supports(const FileRecord &) const override { return true; }
    PreviewResult generate(const PreviewRequest &request, const CancellationToken &cancel) const override;
};

} // namespace purrfind
