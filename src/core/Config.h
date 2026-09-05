#pragma once

#include <QString>
#include <QStringList>

namespace purrfind {

struct ConfigData {
    QStringList includedPaths;
    QStringList excludedPaths;
    bool showHidden{true};
    int maxResults{100};
    QString globalShortcut{"Super+F"};
    // Visual theme preference: "system", "light", or "dark".
    QString themeMode{"system"};
    bool contentIndexingEnabled{true};
    bool contentIndexingPaused{false};
    QStringList contentTypes{"txt", "md", "markdown", "pdf", "docx", "xlsx", "pptx", "odt", "ods", "odp"};
    QStringList contentExcludedPaths;
    qint64 maximumContentFileBytes{100LL * 1024 * 1024};
    qint64 maximumExtractedTextBytes{8LL * 1024 * 1024};
    bool previewAutomatically{true};
    bool advancedImageMetadata{true};
    bool usageRankingEnabled{true};
    bool ocrPdfEnabled{true};
    // Enable image OCR by default so a fresh installation is fully indexed.
    // An explicit user choice to disable it is still persisted in config.json.
    bool ocrImagesEnabled{true};
    bool ocrPaused{false};
    QStringList ocrLanguages{"eng", "osd", "por"};
    QString ocrResourceProfile{"low"};
    bool ocrReduceOnBattery{true};
    bool ocrPauseBelowThirtyPercent{true};
    int ocrMaximumPdfPages{100};
    qint64 ocrMaximumPdfBytes{500LL * 1024 * 1024};
    int ocrDpi{200};
    int ocrPageTimeoutSeconds{90};
    QStringList ocrExcludedPaths;
};

class Config {
public:
    static QString configDirectory();
    static QString configPath();
    static QString dataDirectory();
    static QString databasePath();
    static ConfigData defaults();
    static ConfigData load(QString *error = nullptr);
    static bool save(const ConfigData &config, QString *error = nullptr);
    static QString toJson(const ConfigData &config);
    static bool fromJson(const QString &json, ConfigData *config, QString *error = nullptr);
};

} // namespace purrfind
