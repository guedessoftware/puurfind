#include "ocr/TesseractOcrEngine.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#ifdef PURRFIND_WITH_PDF
#include <poppler-qt6.h>
#endif

#include <sys/resource.h>
#ifdef Q_OS_LINUX
#include <linux/ioprio.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace {

void send(const QJsonObject &object)
{
    QTextStream output(stdout, QIODevice::WriteOnly);
    output << QJsonDocument(object).toJson(QJsonDocument::Compact) << '\n';
    output.flush();
}

bool usefulNativeText(const QString &text)
{
    const QString simplified = text.simplified();
    if (simplified.size() < 32) return false;
    int useful = 0;
    for (const QChar character : simplified)
        if (character.isLetterOrNumber()) ++useful;
    return useful >= 16 && useful * 2 >= simplified.size();
}

void applyResourceLimits(const QString &profile)
{
    const int priority = profile == "high" ? 10 : profile == "normal" ? 15 : 19;
    ::setpriority(PRIO_PROCESS, 0, priority);
    // RLIMIT_AS includes mapped Qt, Poppler, language data and allocator arenas,
    // so it must be above the much smaller expected resident set.
    struct rlimit addressSpace {1536ULL * 1024 * 1024, 1536ULL * 1024 * 1024};
    struct rlimit coreSize {0, 0};
    ::setrlimit(RLIMIT_AS, &addressSpace);
    ::setrlimit(RLIMIT_CORE, &coreSize);
#ifdef Q_OS_LINUX
    const int ioClass = IOPRIO_PRIO_VALUE(IOPRIO_CLASS_IDLE, 0);
    ::syscall(SYS_ioprio_set, IOPRIO_WHO_PROCESS, 0, ioClass);
#endif
}

QImage boundedImage(const QString &path, QString *error)
{
    if (QImageReader::allocationLimit() <= 0 || QImageReader::allocationLimit() > 256)
        QImageReader::setAllocationLimit(256);
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!reader.canRead() || !size.isValid()) { if (error) *error = reader.errorString(); return {}; }
    if (size.width() > 32768 || size.height() > 32768
        || static_cast<qint64>(size.width()) * size.height() > 100000000LL) {
        if (error) *error = "image dimensions exceed OCR safety limit";
        return {};
    }
    if (qMax(size.width(), size.height()) > 6000)
        reader.setScaledSize(size.scaled(6000, 6000, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (image.isNull() && error) *error = reader.errorString();
    return image;
}

int recognizePage(purrfind::TesseractOcrEngine &engine, const QImage &image,
                  const QStringList &languages, int page, int total)
{
    const auto result = engine.recognize(image, languages, purrfind::CancellationToken());
    if (!result.error.isEmpty()) {
        send({{"event", "error"}, {"message", result.error}, {"page", page}});
        return 1;
    }
    send({{"event", "page"}, {"page", page}, {"totalPages", total},
          {"text", result.text.left(256 * 1024)}, {"confidence", result.confidence},
          {"nativeText", false}});
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName("purrfind-ocr-worker");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"path", "Input file", "path"});
    parser.addOption({"kind", "pdf or image", "kind"});
    parser.addOption({"languages", "Tesseract language codes joined by +", "languages"});
    parser.addOption({"dpi", "PDF rendering DPI", "dpi", "200"});
    parser.addOption({"max-pages", "Maximum PDF pages", "pages", "100"});
    parser.addOption({"profile", "Resource profile", "profile", "low"});
    parser.process(application);

    const QString path = parser.value("path");
    const QString kind = parser.value("kind");
    const QStringList languages = parser.value("languages").split('+', Qt::SkipEmptyParts);
    const QString profile = parser.value("profile");
    applyResourceLimits(profile);
    if (path.isEmpty() || !QFileInfo::exists(path) || languages.isEmpty()) {
        send({{"event", "error"}, {"message", "invalid OCR worker arguments"}});
        return 2;
    }

    purrfind::TesseractOcrEngine engine;
    if (kind == "image") {
        send({{"event", "start"}, {"totalPages", 1}, {"limitedPages", 1}});
        QString error;
        const QImage image = boundedImage(path, &error);
        if (image.isNull()) { send({{"event", "error"}, {"message", error}}); return 3; }
        if (recognizePage(engine, image, languages, 1, 1)) return 4;
        send({{"event", "done"}});
        return 0;
    }

    if (kind != "pdf") {
        send({{"event", "error"}, {"message", "unsupported OCR kind"}});
        return 5;
    }
#ifdef PURRFIND_WITH_PDF
    auto document = Poppler::Document::load(path);
    if (!document) { send({{"event", "error"}, {"message", "invalid PDF"}}); return 6; }
    if (document->isLocked()) { send({{"event", "error"}, {"message", "encrypted PDF"}}); return 7; }
    const int total = document->numPages();
    if (total <= 0) {
        send({{"event", "start"}, {"totalPages", 0}, {"limitedPages", 0}, {"truncated", false}});
        send({{"event", "done"}});
        return 0;
    }
    const int maximum = qBound(1, parser.value("max-pages").toInt(), qMax(1, total));
    const int dpi = qBound(150, parser.value("dpi").toInt(), 300);
    send({{"event", "start"}, {"totalPages", total}, {"limitedPages", maximum},
          {"truncated", maximum < total}});
    for (int index = 0; index < maximum; ++index) {
        auto page = document->page(index);
        if (!page) { send({{"event", "error"}, {"message", "PDF page unavailable"}, {"page", index + 1}}); return 8; }
        const QString native = page->text(QRectF());
        if (usefulNativeText(native)) {
            send({{"event", "page"}, {"page", index + 1}, {"totalPages", total},
                  {"text", ""}, {"confidence", 100.0}, {"nativeText", true}});
            continue;
        }
        double renderDpi = dpi;
        const QSizeF points = page->pageSizeF();
        const double projected = points.width() * renderDpi / 72.0 * points.height() * renderDpi / 72.0;
        if (projected > 50000000.0) renderDpi *= qSqrt(50000000.0 / projected);
        const QImage image = page->renderToImage(renderDpi, renderDpi);
        if (image.isNull()) { send({{"event", "error"}, {"message", "PDF render failed"}, {"page", index + 1}}); return 9; }
        if (recognizePage(engine, image, languages, index + 1, total)) return 10;
    }
    send({{"event", "done"}});
    return 0;
#else
    send({{"event", "error"}, {"message", "PDF OCR support disabled at build time"}});
    return 11;
#endif
}
