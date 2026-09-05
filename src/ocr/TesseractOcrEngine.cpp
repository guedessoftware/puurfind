#include "ocr/TesseractOcrEngine.h"

#include <cmath>
#include <limits>
#include <vector>

#ifdef PURRFIND_WITH_OCR
#include <tesseract/baseapi.h>
#endif

namespace purrfind {

namespace {

int usefulCharacters(const QString &text)
{
    int count = 0;
    for (const QChar character : text)
        if (character.isLetterOrNumber()) ++count;
    return count;
}

QImage enhanceSmallText(const QImage &source)
{
    const QImage grayscale = source.convertToFormat(QImage::Format_Grayscale8);
    const int shortestSide = qMin(grayscale.width(), grayscale.height());
    double scale = shortestSide > 0 ? qMin(2.0, 1800.0 / shortestSide) : 1.0;
    const double projectedPixels = grayscale.width() * scale * grayscale.height() * scale;
    if (projectedPixels > 12000000.0) scale *= std::sqrt(12000000.0 / projectedPixels);
    const int width = qMax(1, qRound(grayscale.width() * scale));
    const int height = qMax(1, qRound(grayscale.height() * scale));

    auto cubic = [](double value) {
        constexpr double a = -0.5; // Catmull-Rom: preserves fine character strokes.
        value = std::abs(value);
        if (value <= 1.0) return (a + 2.0) * value * value * value
            - (a + 3.0) * value * value + 1.0;
        if (value < 2.0) return a * value * value * value - 5.0 * a * value * value
            + 8.0 * a * value - 4.0 * a;
        return 0.0;
    };

    std::vector<float> horizontal(static_cast<size_t>(width) * grayscale.height());
    for (int y = 0; y < grayscale.height(); ++y) {
        const uchar *line = grayscale.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            const double sourceX = (x + 0.5) / scale - 0.5;
            const int origin = static_cast<int>(std::floor(sourceX));
            double value = 0.0;
            for (int offset = -1; offset <= 2; ++offset)
                value += line[qBound(0, origin + offset, grayscale.width() - 1)]
                    * cubic(sourceX - origin - offset);
            horizontal[static_cast<size_t>(y) * width + x] = static_cast<float>(value);
        }
    }

    std::vector<float> pixels(static_cast<size_t>(width) * height);
    float minimum = std::numeric_limits<float>::max();
    float maximum = std::numeric_limits<float>::lowest();
    for (int y = 0; y < height; ++y) {
        const double sourceY = (y + 0.5) / scale - 0.5;
        const int origin = static_cast<int>(std::floor(sourceY));
        for (int x = 0; x < width; ++x) {
            double value = 0.0;
            for (int offset = -1; offset <= 2; ++offset)
                value += horizontal[static_cast<size_t>(qBound(0, origin + offset,
                    grayscale.height() - 1)) * width + x] * cubic(sourceY - origin - offset);
            const float converted = static_cast<float>(value);
            pixels[static_cast<size_t>(y) * width + x] = converted;
            minimum = qMin(minimum, converted);
            maximum = qMax(maximum, converted);
        }
    }

    QImage enhanced(width, height, QImage::Format_Grayscale8);
    const float range = maximum - minimum;
    for (int y = 0; y < height; ++y) {
        uchar *line = enhanced.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const float value = pixels[static_cast<size_t>(y) * width + x];
            line[x] = range > 1.0f
                ? static_cast<uchar>(qBound(0, qRound((value - minimum) * 255.0f / range), 255))
                : static_cast<uchar>(qBound(0, qRound(value), 255));
        }
    }
    return enhanced;
}

} // namespace

struct TesseractOcrEngine::Impl {
#ifdef PURRFIND_WITH_OCR
    std::unique_ptr<tesseract::TessBaseAPI> api;
    QString languages;
#endif
};

TesseractOcrEngine::TesseractOcrEngine() : impl_(std::make_unique<Impl>()) {}
TesseractOcrEngine::~TesseractOcrEngine() = default;

OcrRecognition TesseractOcrEngine::recognize(const QImage &source, const QStringList &languages,
                                             const CancellationToken &cancel)
{
    OcrRecognition result;
    if (cancel.isCancelled()) { result.error = "cancelled"; return result; }
    if (source.isNull()) { result.error = "empty image"; return result; }
    if (languages.isEmpty()) { result.error = "no OCR language pack selected"; return result; }
#ifdef PURRFIND_WITH_OCR
    const QImage image = source.convertToFormat(QImage::Format_Grayscale8);
    const QByteArray language = languages.join('+').toUtf8();
    if (!impl_->api || impl_->languages != QString::fromUtf8(language)) {
        auto api = std::make_unique<tesseract::TessBaseAPI>();
        if (api->Init(nullptr, language.constData(), tesseract::OEM_LSTM_ONLY) != 0) {
            result.error = "OCR language pack not found: " + languages.join('+');
            return result;
        }
        impl_->languages = QString::fromUtf8(language);
        impl_->api = std::move(api);
    }
    auto &api = *impl_->api;
    auto recognizePass = [&api, &cancel](const QImage &passImage, tesseract::PageSegMode mode,
                                         bool filterWeakWords = false) {
        OcrRecognition pass;
        api.SetPageSegMode(mode);
        api.SetVariable("user_defined_dpi", "200");
        api.SetImage(passImage.constBits(), passImage.width(), passImage.height(), 1,
                     passImage.bytesPerLine());
        if (cancel.isCancelled()) { api.Clear(); pass.error = "cancelled"; return pass; }
        if (api.Recognize(nullptr) != 0) {
            api.Clear(); pass.error = "Tesseract recognition failed"; return pass;
        }
        if (filterWeakWords) {
            std::unique_ptr<tesseract::ResultIterator> iterator(api.GetIterator());
            QStringList words;
            if (iterator) {
                do {
                    char *utf8 = iterator->GetUTF8Text(tesseract::RIL_WORD);
                    const QString word = utf8 ? QString::fromUtf8(utf8).trimmed() : QString();
                    delete[] utf8;
                    if (iterator->Confidence(tesseract::RIL_WORD) >= 20.0f
                        && usefulCharacters(word) >= 2) words.append(word);
                } while (iterator->Next(tesseract::RIL_WORD));
            }
            pass.text = words.join(' ');
        } else {
            char *utf8 = api.GetUTF8Text();
            if (utf8) {
                pass.text = QString::fromUtf8(utf8).simplified();
                delete[] utf8;
            }
        }
        pass.confidence = qBound(0.0, static_cast<double>(api.MeanTextConf()), 100.0);
        api.Clear();
        return pass;
    };

    result = recognizePass(image, tesseract::PSM_AUTO);
    if (result.error.isEmpty() && usefulCharacters(result.text) < 8 && !cancel.isCancelled()) {
        const QImage enhanced = enhanceSmallText(source);
        OcrRecognition retry = recognizePass(enhanced, tesseract::PSM_SINGLE_BLOCK, true);
        if (retry.error.isEmpty() && usefulCharacters(retry.text) > usefulCharacters(result.text))
            result = std::move(retry);
    }
    if (cancel.isCancelled()) { result.text.clear(); result.error = "cancelled"; }
#else
    result.error = "OCR support disabled at build time";
#endif
    return result;
}

} // namespace purrfind
