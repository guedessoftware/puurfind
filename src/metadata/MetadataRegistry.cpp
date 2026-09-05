#include "metadata/MetadataRegistry.h"

namespace purrfind {

const MetadataProvider *MetadataRegistry::providerFor(const FileRecord &file) const
{
    return images_.supports(file) ? &images_ : nullptr;
}

} // namespace purrfind
