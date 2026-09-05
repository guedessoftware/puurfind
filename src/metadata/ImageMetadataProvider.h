#pragma once

#include "metadata/MetadataProvider.h"

namespace purrfind {

class ImageMetadataProvider final : public MetadataProvider {
public:
    QString id() const override { return "image-metadata"; }
    bool supports(const FileRecord &file) const override;
    RichMetadata extract(const FileRecord &file, const CancellationToken &cancel) const override;
};

} // namespace purrfind
