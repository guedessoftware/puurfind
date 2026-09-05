#pragma once

#include "core/ContentExtractor.h"

#include <QImage>

namespace purrfind {

struct OcrRecognition {
    QString text;
    double confidence{0.0};
    QString error;
};

class OcrEngine {
public:
    virtual ~OcrEngine() = default;
    virtual OcrRecognition recognize(const QImage &image, const QStringList &languages,
                                     const CancellationToken &cancel) = 0;
};

} // namespace purrfind
