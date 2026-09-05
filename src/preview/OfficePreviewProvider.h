#pragma once

#include "preview/PreviewProvider.h"

namespace purrfind {

class OfficePreviewProvider final : public PreviewProvider {
public:
    QString id() const override { return "office-text"; }
    bool supports(const FileRecord &file) const override;
    PreviewResult generate(const PreviewRequest &request, const CancellationToken &cancel) const override;
};

} // namespace purrfind
