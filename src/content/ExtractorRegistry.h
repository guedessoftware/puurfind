#pragma once

#include "core/ContentExtractor.h"

#include <memory>
#include <vector>

namespace purrfind {

class ExtractorRegistry {
public:
    ExtractorRegistry();
    const ContentExtractor *extractorFor(const FileRecord &file, const QStringList &enabledTypes) const;
    QStringList availableExtensions() const;
private:
    std::vector<std::unique_ptr<ContentExtractor>> extractors_;
};

} // namespace purrfind

