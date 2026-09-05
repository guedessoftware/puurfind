#pragma once

#include <QStringList>

namespace purrfind {

class OcrLanguageManager {
public:
    static QStringList availableLanguages();
    static QStringList usableLanguages(const QStringList &requested);
};

} // namespace purrfind
