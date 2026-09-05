#pragma once

#include "core/ContentExtractor.h"

namespace purrfind {

class OpenDocumentExtractor final : public ContentExtractor {
public:
    QString id() const override { return "open-document"; }
    bool supports(const FileRecord &file) const override;
    ExtractResult extract(const FileRecord &file, const ExtractionLimits &limits,
                          const CancellationToken &cancel) const override;
};

} // namespace purrfind

