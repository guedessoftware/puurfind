#include "ocr/OcrLanguageManager.h"
#include "ocr/OcrScheduler.h"
#include "ocr/TesseractOcrEngine.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

namespace {
int failures = 0;
void check(bool value, const QString &message)
{
    if (!value) { std::cerr << "FAIL: " << message.toStdString() << '\n'; ++failures; }
}

QImage textImage(const QString &text, QSize size = {1800, 320}, int pixels = 58)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setPen(Qt::black);
    QFont font("DejaVu Sans", pixels, QFont::Bold);
    painter.setFont(font);
    painter.drawText(image.rect().adjusted(35, 20, -35, -20), Qt::AlignCenter | Qt::TextWordWrap, text);
    return image;
}

QList<QJsonObject> runWorker(const QString &worker, const QStringList &arguments, int *exitCode = nullptr)
{
    QProcess process;
    process.start(worker, arguments);
    if (!process.waitForStarted(5000) || !process.waitForFinished(60000)) {
        process.kill(); process.waitForFinished();
        return {};
    }
    if (exitCode) *exitCode = process.exitCode();
    QList<QJsonObject> events;
    for (const auto &line : process.readAllStandardOutput().split('\n')) {
        if (line.trimmed().isEmpty()) continue;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(line, &error);
        if (error.error == QJsonParseError::NoError && document.isObject()) events.append(document.object());
    }
    const QByteArray stderrOutput = process.readAllStandardError().trimmed();
    if (!stderrOutput.isEmpty()) events.append({{"event", "stderr"}, {"message", QString::fromUtf8(stderrOutput.left(1024))}});
    return events;
}

void dumpEvents(const QList<QJsonObject> &events)
{
    for (const auto &event : events)
        std::cerr << QJsonDocument(event).toJson(QJsonDocument::Compact).toStdString() << '\n';
}

bool hasPage(const QList<QJsonObject> &events, int page, bool native, const QString &needle = {})
{
    for (const auto &event : events) {
        if (event.value("event") == "page" && event.value("page").toInt() == page
            && event.value("nativeText").toBool() == native
            && (needle.isEmpty() || event.value("text").toString().contains(needle, Qt::CaseInsensitive))) return true;
    }
    return false;
}

bool createPdf(const QString &path, const QImage &scan, bool hybrid)
{
    QPdfWriter writer(path);
    writer.setResolution(150);
    QPainter painter(&writer);
    if (!painter.isActive()) return false;
    if (hybrid) {
        painter.setPen(Qt::black);
        painter.setFont(QFont("DejaVu Sans", 28));
        painter.drawText(writer.pageLayout().paintRectPixels(writer.resolution()), Qt::AlignCenter,
                         "Native searchable agreement customer network service layer");
        writer.newPage();
    }
    const QRect pageRect = writer.pageLayout().paintRectPixels(writer.resolution());
    const QSize imageSize = scan.size().scaled(pageRect.size() * 0.9, Qt::KeepAspectRatio);
    QRect target(QPoint(), imageSize);
    target.moveCenter(pageRect.center());
    painter.drawImage(target, scan);
    painter.end();
    return QFileInfo(path).size() > 0;
}
}

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    const QString worker = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary OCR directory");
    const QStringList available = purrfind::OcrLanguageManager::availableLanguages();
    check(available.contains("eng"), "English language pack detected");
    check(available.contains("por"), "Portuguese language pack detected");
    purrfind::ConfigData pausedConfig = purrfind::Config::defaults();
    pausedConfig.ocrPaused = true;
    QString waitReason;
    check(!purrfind::OcrScheduler::mayRun(pausedConfig, &waitReason) && waitReason == "paused",
          "scheduler obeys persisted pause state");

    purrfind::TesseractOcrEngine engine;
    const purrfind::CancellationToken active;
    const QImage technical = textImage("FIRENETWORK AS26615 OLT C650 PPPoE 192.168.1.1");
    const auto english = engine.recognize(technical, {"eng"}, active);
    check(english.error.isEmpty() && english.text.contains("FIRENETWORK", Qt::CaseInsensitive)
          && english.text.contains("AS26615", Qt::CaseInsensitive), "English and technical OCR");
    const QImage portuguese = textImage("conexão prestação contratação razão social", {1900, 360}, 52);
    const auto multilingual = engine.recognize(portuguese, {"por", "eng"}, active);
    check(multilingual.error.isEmpty() && multilingual.text.contains("conex", Qt::CaseInsensitive)
          && multilingual.text.contains("presta", Qt::CaseInsensitive), "Portuguese plus English OCR");
    check(!engine.recognize(technical, {"purrfind_missing_language"}, active).error.isEmpty(),
          "missing language pack is reported");

    const QList<QByteArray> formats{"PNG", "JPEG", "TIFF", "WEBP"};
    QString png;
    for (const auto &format : formats) {
        const QString path = temporary.path() + "/ocr." + QString::fromLatin1(format).toLower();
        check(technical.save(path, format.constData()), QString::fromLatin1(format) + " OCR fixture");
        const auto recognition = engine.recognize(QImage(path), {"eng"}, active);
        check(recognition.error.isEmpty() && recognition.text.contains("FIRENETWORK", Qt::CaseInsensitive),
              QString::fromLatin1(format) + " OCR recognition");
        if (format == "PNG") png = path;
    }

    int exitCode = -1;
    auto events = runWorker(worker, {"--path", png, "--kind", "image", "--languages", "por+eng",
                                      "--profile", "low"}, &exitCode);
    check(exitCode == 0 && hasPage(events, 1, false, "FIRENETWORK"), "isolated image worker protocol");

    const QString scanPdf = temporary.path() + "/scan.pdf";
    check(createPdf(scanPdf, technical, false), "scanned PDF fixture");
    events = runWorker(worker, {"--path", scanPdf, "--kind", "pdf", "--languages", "eng",
                                "--dpi", "200", "--max-pages", "100", "--profile", "low"}, &exitCode);
    if (!(exitCode == 0 && hasPage(events, 1, false, "FIRENETWORK"))) dumpEvents(events);
    check(exitCode == 0 && hasPage(events, 1, false, "FIRENETWORK"), "scanned PDF receives OCR");

    const QString hybridPdf = temporary.path() + "/hybrid.pdf";
    check(createPdf(hybridPdf, technical, true), "hybrid PDF fixture");
    events = runWorker(worker, {"--path", hybridPdf, "--kind", "pdf", "--languages", "eng",
                                "--dpi", "200", "--max-pages", "100", "--profile", "low"}, &exitCode);
    if (!(exitCode == 0 && hasPage(events, 1, true) && hasPage(events, 2, false, "FIRENETWORK"))) dumpEvents(events);
    check(exitCode == 0 && hasPage(events, 1, true) && hasPage(events, 2, false, "FIRENETWORK"),
          "hybrid PDF OCRs only the scanned page");

    const QImage rotated = technical.transformed(QTransform().rotate(90));
    const auto rotatedResult = engine.recognize(rotated, {"eng"}, active);
    check(rotatedResult.error.isEmpty(), "rotated fixture fails safely without mandatory OSD");
    const auto lowResolution = engine.recognize(technical.scaledToWidth(700, Qt::SmoothTransformation), {"eng"}, active);
    check(lowResolution.error.isEmpty(), "low-resolution fixture is processed");

    std::cout << (failures ? "OCR tests failed" : "All OCR tests passed") << '\n';
    return failures ? 1 : 0;
}
