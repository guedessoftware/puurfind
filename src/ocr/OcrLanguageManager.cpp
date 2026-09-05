#include "ocr/OcrLanguageManager.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>

#ifdef PURRFIND_WITH_OCR
#include <tesseract/baseapi.h>
#endif

#include <algorithm>

namespace purrfind {

QStringList OcrLanguageManager::availableLanguages()
{
    static const QStringList cached = [] {
        QStringList result;
#ifdef PURRFIND_WITH_OCR
        QStringList directories{
            "/usr/share/tessdata", "/usr/local/share/tessdata",
            "/usr/share/tesseract/tessdata",
            "/usr/share/tesseract-ocr/5/tessdata", "/usr/share/tesseract-ocr/4.00/tessdata",
        };
        const QString prefix = qEnvironmentVariable("TESSDATA_PREFIX");
        if (!prefix.isEmpty()) directories.prepend(prefix);
        directories += QStandardPaths::locateAll(QStandardPaths::GenericDataLocation,
                                                  "tessdata", QStandardPaths::LocateDirectory);
        QSet<QString> seen;
        for (const auto &directory : directories) {
            QDir data(directory);
            if (!data.exists() && QFileInfo(directory + "/tessdata").isDir()) data.setPath(directory + "/tessdata");
            for (const auto &file : data.entryList({"*.traineddata"}, QDir::Files)) {
                const QString language = file.left(file.size() - QString(".traineddata").size());
                if (!seen.contains(language)) { seen.insert(language); result.append(language); }
            }
        }
        std::sort(result.begin(), result.end());
#endif
        return result;
    }();
    return cached;
}

QStringList OcrLanguageManager::usableLanguages(const QStringList &requested)
{
    const auto available = availableLanguages();
    QStringList result;
    for (const auto &language : requested)
        if (available.contains(language) && !result.contains(language)) result.append(language);
    return result;
}

} // namespace purrfind
