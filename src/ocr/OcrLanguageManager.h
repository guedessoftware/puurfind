#pragma once

#include <QStringList>

namespace purrfind {

class OcrLanguageManager {
public:
    // Returns the first installed tessdata directory, honoring TESSDATA_PREFIX.
    // An empty result lets callers retain Tesseract's platform default.
    static QString dataDirectory();
    static QStringList availableLanguages();
    static QStringList usableLanguages(const QStringList &requested);
};

} // namespace purrfind
