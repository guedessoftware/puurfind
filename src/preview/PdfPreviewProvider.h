#pragma once

#include "preview/PreviewProvider.h"

namespace purrfind {

class PdfPreviewProvider final : public PreviewProvider {
public:
    QString id() const override { return "pdf"; }
    bool supports(const FileRecord &file) const override { return file.extension == "pdf"; }
    PreviewResult generate(const PreviewRequest &request, const CancellationToken &cancel) const override;
};

} // namespace purrfind
