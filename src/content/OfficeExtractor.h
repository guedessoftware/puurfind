#pragma once

#include "core/ContentExtractor.h"

namespace purrfind {

class OfficeExtractor final : public ContentExtractor {
public:
    QString id() const override { return "office-open-xml"; }
    bool supports(const FileRecord &file) const override;
    ExtractResult extract(const FileRecord &file, const ExtractionLimits &limits,
                          const CancellationToken &cancel) const override;
};

} // namespace purrfind

