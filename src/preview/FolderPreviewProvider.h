#pragma once

#include "preview/PreviewProvider.h"

namespace purrfind {

class FolderPreviewProvider final : public PreviewProvider {
public:
    QString id() const override { return "folder"; }
    bool supports(const FileRecord &file) const override { return file.directory; }
    PreviewResult generate(const PreviewRequest &request, const CancellationToken &cancel) const override;
};

} // namespace purrfind
