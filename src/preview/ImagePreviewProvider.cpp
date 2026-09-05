#include "preview/ImagePreviewProvider.h"

#include "metadata/ImageMetadataProvider.h"

#include <QImageReader>
#include <QLocale>

namespace purrfind {

bool ImagePreviewProvider::supports(const FileRecord &file) const
{
    const QByteArray suffix = file.extension.toLatin1().toLower();
    return QImageReader::supportedImageFormats().contains(suffix);
}

PreviewResult ImagePreviewProvider::generate(const PreviewRequest &request,
                                              const CancellationToken &cancel) const
{
    PreviewResult result;
    result.provider = id(); result.title = request.file.name;
    result.text = request.snippet;
    if (QImageReader::allocationLimit() <= 0 || QImageReader::allocationLimit() > 128)
        QImageReader::setAllocationLimit(128);
    QImageReader reader(request.file.path);
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    if (!reader.canRead() || !sourceSize.isValid()) { result.error = reader.errorString(); return result; }
    if (sourceSize.width() > 32768 || sourceSize.height() > 32768
        || static_cast<qint64>(sourceSize.width()) * sourceSize.height() > 100000000LL) {
        result.error = "image dimensions exceed the preview safety limit";
        return result;
    }
    if (cancel.isCancelled()) { result.error = "cancelled"; return result; }
    const QSize scaled = sourceSize.scaled(request.targetSize * 2, Qt::KeepAspectRatio);
    if (scaled.width() < sourceSize.width() || scaled.height() < sourceSize.height())
        reader.setScaledSize(scaled);
    result.image = reader.read();
    if (result.image.isNull()) { result.error = reader.errorString(); return result; }
    if (cancel.isCancelled()) { result.image = {}; result.error = "cancelled"; return result; }
    ImageMetadataProvider metadataProvider;
    const RichMetadata metadata = metadataProvider.extract(request.file, cancel);
    QStringList details{QString("%1 × %2").arg(sourceSize.width()).arg(sourceSize.height()),
                        QLocale().formattedDataSize(request.file.size)};
    const QString camera = (metadata.cameraMake + ' ' + metadata.cameraModel).simplified();
    if (!camera.isEmpty()) details << "Camera: " + camera;
    if (!metadata.lens.isEmpty()) details << "Lens: " + metadata.lens;
    if (metadata.iso > 0) details << QString("ISO %1").arg(metadata.iso);
    if (!metadata.aperture.isEmpty()) details << "Aperture: " + metadata.aperture;
    if (!metadata.exposureTime.isEmpty()) details << "Exposure: " + metadata.exposureTime;
    if (!metadata.focalLength.isEmpty()) details << "Focal length: " + metadata.focalLength;
    if (!metadata.dateTakenText.isEmpty()) details << "Captured: " + metadata.dateTakenText;
    if (metadata.hasGps) details << QString("GPS: %1, %2").arg(metadata.latitude, 0, 'f', 6).arg(metadata.longitude, 0, 'f', 6);
    result.details = details.join('\n');
    return result;
}

} // namespace purrfind
