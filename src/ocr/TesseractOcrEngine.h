#pragma once

#include "ocr/OcrEngine.h"

#include <memory>

namespace purrfind {

class TesseractOcrEngine final : public OcrEngine {
public:
    TesseractOcrEngine();
    ~TesseractOcrEngine() override;
    OcrRecognition recognize(const QImage &image, const QStringList &languages,
                             const CancellationToken &cancel) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace purrfind
