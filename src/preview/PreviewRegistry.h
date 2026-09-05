#pragma once

#include "preview/FolderPreviewProvider.h"
#include "preview/GenericPreviewProvider.h"
#include "preview/ImagePreviewProvider.h"
#include "preview/OfficePreviewProvider.h"
#include "preview/PdfPreviewProvider.h"
#include "preview/PreviewCache.h"
#include "preview/TextPreviewProvider.h"

#include <memory>

namespace purrfind {

class PreviewRegistry {
public:
    explicit PreviewRegistry(std::shared_ptr<PreviewCache> cache) : cache_(std::move(cache)) {}
    PreviewResult generate(const PreviewRequest &request, const CancellationToken &cancel) const;

private:
    const PreviewProvider *providerFor(const FileRecord &file) const;
    ImagePreviewProvider image_;
    PdfPreviewProvider pdf_;
    TextPreviewProvider text_;
    OfficePreviewProvider office_;
    FolderPreviewProvider folder_;
    GenericPreviewProvider generic_;
    std::shared_ptr<PreviewCache> cache_;
};

} // namespace purrfind
