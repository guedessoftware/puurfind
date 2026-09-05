#pragma once

#include "metadata/ImageMetadataProvider.h"

namespace purrfind {

class MetadataRegistry {
public:
    const MetadataProvider *providerFor(const FileRecord &file) const;
private:
    ImageMetadataProvider images_;
};

} // namespace purrfind
