#include "core/FileSystem.h"
#include "metadata/ImageMetadataProvider.h"
#include "preview/PreviewRegistry.h"

#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QTemporaryDir>
#ifdef PURRFIND_WITH_EXIV2
#include <exiv2/exiv2.hpp>
#endif
#ifdef PURRFIND_WITH_OFFICE
#include <zip.h>
#endif
#include <iostream>

namespace {
int failures = 0;
void check(bool value, const QString &message)
{
    if (!value) { std::cerr << "FAIL: " << message.toStdString() << '\n'; ++failures; }
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

purrfind::FileRecord record(const QString &path, const QString &root)
{
    const auto inspected = purrfind::FileSystem::inspect(path, root, 1);
    return inspected ? *inspected : purrfind::FileRecord{};
}

QByteArray twoPagePdf(const QByteArray &firstText, const QByteArray &secondText)
{
    QList<QByteArray> objects;
    objects << "<< /Type /Catalog /Pages 2 0 R >>"
            << "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>"
            << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] /Resources << /Font << /F1 7 0 R >> >> /Contents 5 0 R >>"
            << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] /Resources << /Font << /F1 7 0 R >> >> /Contents 6 0 R >>";
    const QByteArray firstStream = "BT /F1 12 Tf 30 350 Td (" + firstText + ") Tj ET";
    const QByteArray secondStream = "BT /F1 12 Tf 30 350 Td (" + secondText + ") Tj ET";
    objects << ("<< /Length " + QByteArray::number(firstStream.size()) + " >>\nstream\n" + firstStream + "\nendstream")
            << ("<< /Length " + QByteArray::number(secondStream.size()) + " >>\nstream\n" + secondStream + "\nendstream")
            << "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";
    QByteArray pdf = "%PDF-1.4\n";
    QList<int> offsets{0};
    for (int index = 0; index < objects.size(); ++index) {
        offsets << pdf.size();
        pdf += QByteArray::number(index + 1) + " 0 obj\n" + objects.at(index) + "\nendobj\n";
    }
    const int xref = pdf.size();
    pdf += "xref\n0 " + QByteArray::number(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (int index = 1; index < offsets.size(); ++index)
        pdf += QByteArray::number(offsets.at(index)).rightJustified(10, '0') + " 00000 n \n";
    pdf += "trailer\n<< /Size " + QByteArray::number(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n"
        + QByteArray::number(xref) + "\n%%EOF\n";
    return pdf;
}

#ifdef PURRFIND_WITH_OFFICE
bool createZip(const QString &path, const QHash<QString, QByteArray> &entries)
{
    int error = 0;
    const QByteArray encoded = QFile::encodeName(path);
    zip_t *archive = zip_open(encoded.constData(), ZIP_CREATE | ZIP_TRUNCATE, &error);
    if (!archive) return false;
    for (auto iterator = entries.cbegin(); iterator != entries.cend(); ++iterator) {
        zip_source_t *source = zip_source_buffer(archive, iterator.value().constData(), iterator.value().size(), 0);
        if (!source || zip_file_add(archive, iterator.key().toUtf8().constData(), source,
                                    ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) < 0) {
            if (source) zip_source_free(source);
            zip_discard(archive); return false;
        }
    }
    return zip_close(archive) == 0;
}
#endif
}

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary directory");
    auto cache = std::make_shared<purrfind::PreviewCache>(8, 16, temporary.path() + "/cache");
    purrfind::PreviewRegistry registry(cache);
    const purrfind::CancellationToken active;

    const auto supportedFormats = QImageReader::supportedImageFormats();
    const QList<QByteArray> requiredFormats{"jpeg", "png", "tiff", "webp", "bmp", "gif"};
    for (const auto &format : requiredFormats)
        if (!supportedFormats.contains(format))
            std::cout << "SKIP: " << format.constData() << " decoder is not installed\n";

    QImage source(40, 20, QImage::Format_RGB32);
    source.fill(QColor("#8c62d8"));
    const QString jpeg = temporary.path() + "/oriented.jpg";
    const QString png = temporary.path() + "/sample.png";
    const QString tiff = temporary.path() + "/sample.tiff";
    const QString webp = temporary.path() + "/sample.webp";
    const QString bmp = temporary.path() + "/sample.bmp";
    const QString gif = temporary.path() + "/sample.gif";
    const QByteArray minimalGif = QByteArray::fromBase64("R0lGODlhAQABAIAAAAAAAP///ywAAAAAAQABAAACAUwAOw==");
    const bool jpegAvailable = supportedFormats.contains("jpeg");
    const bool pngAvailable = supportedFormats.contains("png");
    const bool tiffAvailable = supportedFormats.contains("tiff");
    const bool webpAvailable = supportedFormats.contains("webp");
    const bool bmpAvailable = supportedFormats.contains("bmp");
    const bool gifAvailable = supportedFormats.contains("gif");
    check(jpegAvailable && pngAvailable, "baseline image decoders available");
    check((!jpegAvailable || source.save(jpeg, "JPEG"))
          && (!pngAvailable || source.save(png, "PNG"))
          && (!tiffAvailable || source.save(tiff, "TIFF"))
          && (!webpAvailable || source.save(webp, "WEBP"))
          && (!bmpAvailable || source.save(bmp, "BMP"))
          && (!gifAvailable || writeFile(gif, minimalGif)), "image fixtures created");

#ifdef PURRFIND_WITH_EXIV2
    try {
        auto image = Exiv2::ImageFactory::open(QFile::encodeName(jpeg).toStdString(), false);
        image->readMetadata();
        auto &exif = image->exifData();
        exif["Exif.Image.Make"] = "Canon";
        exif["Exif.Image.Model"] = "PurrCam";
        exif["Exif.Photo.LensModel"] = "50mm Test Lens";
        exif["Exif.Photo.DateTimeOriginal"] = "2026:09:04 10:11:12";
        exif["Exif.Image.Orientation"] = uint16_t{6};
        exif["Exif.Photo.ISOSpeedRatings"] = uint16_t{400};
        exif["Exif.GPSInfo.GPSLatitudeRef"] = "S";
        exif["Exif.GPSInfo.GPSLatitude"].setValue("3/1 8/1 0/1");
        exif["Exif.GPSInfo.GPSLongitudeRef"] = "W";
        exif["Exif.GPSInfo.GPSLongitude"].setValue("60/1 1/1 0/1");
        image->setExifData(exif);
        image->writeMetadata();
    } catch (const Exiv2::Error &error) {
        check(false, "EXIF fixture: " + QString::fromUtf8(error.what()));
    }
#endif

    purrfind::ImageMetadataProvider metadataProvider;
    const auto jpegRecord = record(jpeg, temporary.path());
    const auto metadata = metadataProvider.extract(jpegRecord, active);
#ifdef PURRFIND_WITH_EXIV2
    check(metadata.cameraMake == "Canon" && metadata.cameraModel == "PurrCam", "camera EXIF extracted");
    check(metadata.iso == 400 && metadata.orientation == 6, "ISO and orientation extracted");
    check(metadata.hasGps && metadata.latitude < 0 && metadata.longitude < 0, "private local GPS extracted");
    check(metadata.dateTaken > 0, "capture date extracted");
#endif

    QList<QString> availableImagePaths;
    if (jpegAvailable) availableImagePaths.append(jpeg);
    if (pngAvailable) availableImagePaths.append(png);
    if (tiffAvailable) availableImagePaths.append(tiff);
    if (webpAvailable) availableImagePaths.append(webp);
    if (bmpAvailable) availableImagePaths.append(bmp);
    if (gifAvailable) availableImagePaths.append(gif);
    for (const QString &path : availableImagePaths) {
        purrfind::PreviewRequest request; request.file = record(path, temporary.path());
        const auto preview = registry.generate(request, active);
        check(preview.provider == "image" && !preview.image.isNull(), QFileInfo(path).suffix() + " preview");
    }
    const QString corruptImagePath = temporary.path() + "/corrupt.png";
    writeFile(corruptImagePath, "not an image");
    purrfind::PreviewRequest corruptImage; corruptImage.file = record(corruptImagePath, temporary.path());
    check(!registry.generate(corruptImage, active).error.isEmpty(), "corrupted image fails safely");
    purrfind::PreviewRequest orientedRequest; orientedRequest.file = jpegRecord;
    auto orientedPreview = registry.generate(orientedRequest, active);
#ifdef PURRFIND_WITH_EXIV2
    check(orientedPreview.image.height() > orientedPreview.image.width(), "EXIF orientation applied to preview");
#endif

    purrfind::PreviewRequest cachedRequest; cachedRequest.file = record(png, temporary.path()); cachedRequest.page = 99;
    auto first = registry.generate(cachedRequest, active);
    auto second = registry.generate(cachedRequest, active);
    check(!first.cacheHit && second.cacheHit, "second preview is served from cache");
    auto restartedCache = std::make_shared<purrfind::PreviewCache>(8, 16, temporary.path() + "/cache");
    purrfind::PreviewRegistry restartedRegistry(restartedCache);
    const auto diskHit = restartedRegistry.generate(cachedRequest, active);
    check(diskHit.cacheHit && !diskHit.image.isNull(), "disk cache survives UI restart");
    cachedRequest.file.mtime += 1;
    auto invalidated = registry.generate(cachedRequest, active);
    check(!invalidated.cacheHit, "file revision invalidates preview cache key");

    std::atomic_bool cancelled{true};
    purrfind::PreviewRequest cancelledRequest; cancelledRequest.file = jpegRecord;
    cancelledRequest.page = 7;
    const auto cancelledPreview = registry.generate(cancelledRequest, purrfind::CancellationToken(&cancelled));
    check(cancelledPreview.error == "cancelled", "cancelled selection does not render");

    const QString textPath = temporary.path() + "/notes.txt";
    writeFile(textPath, "FIRST_NEEDLE\n" + QByteArray(12000, 'x')
        + "\nPreview_FIRENETWORK near the useful section\nlast lines");
    purrfind::PreviewRequest textRequest; textRequest.file = record(textPath, temporary.path()); textRequest.query = "FIRENETWORK";
    const auto firstTextPreview = registry.generate(textRequest, active);
    check(firstTextPreview.text.contains("FIRENETWORK"), "TXT relevant preview");
    textRequest.query = "FIRST_NEEDLE";
    const auto otherTextPreview = registry.generate(textRequest, active);
    check(otherTextPreview.text.contains("FIRST_NEEDLE") && !otherTextPreview.text.contains("FIRENETWORK"),
          "TXT cache keeps query-dependent excerpts distinct");

#ifdef PURRFIND_WITH_PDF
    const QString pdfPath = temporary.path() + "/document.pdf";
    writeFile(pdfPath, twoPagePdf("PDF preview page one", "PDF preview page two"));
    purrfind::PreviewRequest pdfRequest; pdfRequest.file = record(pdfPath, temporary.path()); pdfRequest.page = 1;
    pdfRequest.snippet = "current PDF snippet";
    const auto pdfPreview = registry.generate(pdfRequest, active);
    check(!pdfPreview.image.isNull() && pdfPreview.page == 1 && pdfPreview.pageCount == 2, "PDF page preview and navigation metadata");
    pdfRequest.snippet = "updated PDF snippet";
    const auto cachedPdfPreview = registry.generate(pdfRequest, active);
    check(cachedPdfPreview.cacheHit && cachedPdfPreview.text == pdfRequest.snippet,
          "PDF thumbnail cache uses the current search snippet");
    pdfRequest.page = 2;
    const auto secondPdfPage = registry.generate(pdfRequest, active);
    check(!secondPdfPage.image.isNull() && secondPdfPage.page == 2 && !secondPdfPage.cacheHit,
          "PDF navigation renders and caches pages independently");
    const QString invalidPdfPath = temporary.path() + "/invalid.pdf";
    writeFile(invalidPdfPath, "%PDF truncated");
    purrfind::PreviewRequest invalidPdf; invalidPdf.file = record(invalidPdfPath, temporary.path());
    check(!registry.generate(invalidPdf, active).error.isEmpty(), "corrupted PDF preview fails safely");
#endif

#ifdef PURRFIND_WITH_OFFICE
    struct Fixture { QString extension; QHash<QString, QByteArray> entries; QString text; };
    const QList<Fixture> officeFixtures{
        {"docx", {{"word/document.xml", "<document><p>DOCX_PREVIEW</p></document>"}}, "DOCX_PREVIEW"},
        {"xlsx", {{"xl/workbook.xml", "<workbook><sheet name='Clients'/></workbook>"}, {"xl/worksheets/sheet1.xml", "<row><c><v>XLSX_PREVIEW</v></c></row>"}}, "XLSX_PREVIEW"},
        {"pptx", {{"ppt/slides/slide1.xml", "<slide><p>PPTX_PREVIEW</p></slide>"}}, "PPTX_PREVIEW"},
        {"odt", {{"content.xml", "<document><p>ODT_PREVIEW</p></document>"}}, "ODT_PREVIEW"},
        {"ods", {{"content.xml", "<document><table name='Clients'><p>ODS_PREVIEW</p></table></document>"}}, "ODS_PREVIEW"},
        {"odp", {{"content.xml", "<document><slide><p>ODP_PREVIEW</p></slide></document>"}}, "ODP_PREVIEW"},
    };
    for (const auto &fixture : officeFixtures) {
        const QString path = temporary.path() + "/preview." + fixture.extension;
        check(createZip(path, fixture.entries), fixture.extension + " preview fixture");
        purrfind::PreviewRequest request; request.file = record(path, temporary.path());
        check(registry.generate(request, active).text.contains(fixture.text), fixture.extension + " textual preview");
    }
#endif

    purrfind::PreviewRequest folderRequest; folderRequest.file = record(temporary.path(), temporary.path());
    const auto folderPreview = registry.generate(folderRequest, active);
    check(folderPreview.provider == "folder" && folderPreview.text.contains("Conteúdo imediato")
              && folderPreview.text.contains("[[PFIMAGE]]")
              && folderPreview.text.contains(QFileInfo(jpeg).fileName()), "folder content preview");
    const QString unknown = temporary.path() + "/unknown.bin";
    writeFile(unknown, "unknown");
    purrfind::PreviewRequest genericRequest; genericRequest.file = record(unknown, temporary.path());
    const auto generic = registry.generate(genericRequest, active);
    check(generic.provider == "generic" && generic.details.contains("Permissions"), "generic fallback preview");

    std::cout << (failures ? "Preview tests failed" : "All preview tests passed") << '\n';
    return failures ? 1 : 0;
}
