#include "metadata/ImageMetadataProvider.h"

#include <QDateTime>
#include <QFile>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>

#ifdef PURRFIND_WITH_EXIV2
#include <exiv2/exiv2.hpp>
#endif

namespace purrfind {
namespace {

#ifdef PURRFIND_WITH_EXIV2
QString exifText(const Exiv2::ExifData &data, const char *key)
{
    const auto found = data.findKey(Exiv2::ExifKey(key));
    return found == data.end() ? QString() : QString::fromStdString(found->toString()).trimmed();
}

int exifInteger(const Exiv2::ExifData &data, const char *key, int fallback = 0)
{
    const auto found = data.findKey(Exiv2::ExifKey(key));
    if (found == data.end() || found->count() == 0) return fallback;
    try { return static_cast<int>(found->toInt64()); } catch (...) { return fallback; }
}

bool gpsCoordinate(const Exiv2::ExifData &data, const char *coordinateKey,
                   const char *referenceKey, double *coordinate)
{
    const auto value = data.findKey(Exiv2::ExifKey(coordinateKey));
    const auto reference = data.findKey(Exiv2::ExifKey(referenceKey));
    if (value == data.end() || reference == data.end() || value->count() < 3) return false;
    try {
        double result = value->toFloat(0) + value->toFloat(1) / 60.0 + value->toFloat(2) / 3600.0;
        const QString direction = QString::fromStdString(reference->toString()).trimmed().toUpper();
        if (direction == "S" || direction == "W") result = -result;
        *coordinate = result;
        return true;
    } catch (...) {
        return false;
    }
}
#endif

} // namespace

bool ImageMetadataProvider::supports(const FileRecord &file) const
{
    return file.type.startsWith("image/")
        || QStringList{"jpg","jpeg","png","webp","bmp","gif","tif","tiff","avif","heif","heic"}
               .contains(file.extension);
}

RichMetadata ImageMetadataProvider::extract(const FileRecord &file, const CancellationToken &cancel) const
{
    RichMetadata result;
    if (cancel.isCancelled()) { result.error = "cancelled"; return result; }
    QImageReader reader(file.path);
    const QSize dimensions = reader.size();
    if (dimensions.isValid()) { result.width = dimensions.width(); result.height = dimensions.height(); }
    if (!reader.canRead()) { result.error = reader.errorString(); return result; }

#ifdef PURRFIND_WITH_EXIV2
    try {
        const QByteArray path = QFile::encodeName(file.path);
        auto image = Exiv2::ImageFactory::open(path.toStdString(), false);
        if (image && !cancel.isCancelled()) {
            image->readMetadata();
            const auto &exif = image->exifData();
            result.cameraMake = exifText(exif, "Exif.Image.Make");
            result.cameraModel = exifText(exif, "Exif.Image.Model");
            result.lens = exifText(exif, "Exif.Photo.LensModel");
            result.software = exifText(exif, "Exif.Image.Software");
            result.copyright = exifText(exif, "Exif.Image.Copyright");
            result.iso = exifInteger(exif, "Exif.Photo.ISOSpeedRatings");
            result.orientation = exifInteger(exif, "Exif.Image.Orientation", 1);
            result.aperture = exifText(exif, "Exif.Photo.FNumber");
            result.exposureTime = exifText(exif, "Exif.Photo.ExposureTime");
            result.focalLength = exifText(exif, "Exif.Photo.FocalLength");
            result.dateTakenText = exifText(exif, "Exif.Photo.DateTimeOriginal");
            if (result.dateTakenText.isEmpty()) result.dateTakenText = exifText(exif, "Exif.Image.DateTime");
            const QDateTime captured = QDateTime::fromString(result.dateTakenText, "yyyy:MM:dd HH:mm:ss");
            if (captured.isValid()) result.dateTaken = captured.toSecsSinceEpoch();
            double latitude = 0.0, longitude = 0.0;
            if (gpsCoordinate(exif, "Exif.GPSInfo.GPSLatitude", "Exif.GPSInfo.GPSLatitudeRef", &latitude)
                && gpsCoordinate(exif, "Exif.GPSInfo.GPSLongitude", "Exif.GPSInfo.GPSLongitudeRef", &longitude)) {
                result.hasGps = true; result.latitude = latitude; result.longitude = longitude;
            }
        }
    } catch (const Exiv2::Error &exception) {
        // Dimensions remain useful when a codec works but EXIF is malformed.
        result.error = QString::fromUtf8(exception.what());
    }
#endif
    result.detailsJson = QString::fromUtf8(QJsonDocument(QJsonObject{
        {"cameraMake", result.cameraMake}, {"cameraModel", result.cameraModel},
        {"lens", result.lens}, {"iso", result.iso}, {"aperture", result.aperture},
        {"exposureTime", result.exposureTime}, {"focalLength", result.focalLength},
        {"dateTaken", result.dateTakenText}, {"orientation", result.orientation},
        {"width", result.width}, {"height", result.height}, {"software", result.software},
        {"copyright", result.copyright}, {"hasGps", result.hasGps},
        {"latitude", result.latitude}, {"longitude", result.longitude},
    }).toJson(QJsonDocument::Compact));
    return result;
}

} // namespace purrfind
