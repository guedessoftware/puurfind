#pragma once

#include "core/ContentExtractor.h"

namespace purrfind {

class MetadataProvider {
public:
    virtual ~MetadataProvider() = default;
    virtual QString id() const = 0;
    virtual bool supports(const FileRecord &file) const = 0;
    virtual RichMetadata extract(const FileRecord &file, const CancellationToken &cancel) const = 0;
};

} // namespace purrfind
