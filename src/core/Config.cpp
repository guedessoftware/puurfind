#include "core/Config.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace purrfind {
namespace {

QString clean(const QString &path)
{
    return QDir::cleanPath(QDir(path).absolutePath());
}

QJsonObject objectFor(const ConfigData &config)
{
    QJsonArray included;
    for (const auto &path : config.includedPaths) included.append(path);
    QJsonArray excluded;
    for (const auto &path : config.excludedPaths) excluded.append(path);
    return {
        {"version", 4},
        {"includedPaths", included},
        {"excludedPaths", excluded},
        {"showHidden", config.showHidden},
        {"maxResults", config.maxResults},
        {"globalShortcut", config.globalShortcut},
        {"themeMode", config.themeMode},
        {"contentIndexingEnabled", config.contentIndexingEnabled},
        {"contentIndexingPaused", config.contentIndexingPaused},
        {"contentTypes", QJsonArray::fromStringList(config.contentTypes)},
        {"contentExcludedPaths", QJsonArray::fromStringList(config.contentExcludedPaths)},
        {"maximumContentFileBytes", config.maximumContentFileBytes},
        {"maximumExtractedTextBytes", config.maximumExtractedTextBytes},
        {"previewAutomatically", config.previewAutomatically},
        {"advancedImageMetadata", config.advancedImageMetadata},
        {"usageRankingEnabled", config.usageRankingEnabled},
        {"ocrPdfEnabled", config.ocrPdfEnabled},
        {"ocrImagesEnabled", config.ocrImagesEnabled},
        {"ocrPaused", config.ocrPaused},
        {"ocrLanguages", QJsonArray::fromStringList(config.ocrLanguages)},
        {"ocrResourceProfile", config.ocrResourceProfile},
        {"ocrReduceOnBattery", config.ocrReduceOnBattery},
        {"ocrPauseBelowThirtyPercent", config.ocrPauseBelowThirtyPercent},
        {"ocrMaximumPdfPages", config.ocrMaximumPdfPages},
        {"ocrMaximumPdfBytes", config.ocrMaximumPdfBytes},
        {"ocrDpi", config.ocrDpi},
        {"ocrPageTimeoutSeconds", config.ocrPageTimeoutSeconds},
        {"ocrExcludedPaths", QJsonArray::fromStringList(config.ocrExcludedPaths)},
    };
}

} // namespace

QString Config::configDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/purrfind";
}

QString Config::configPath() { return configDirectory() + "/config.json"; }

QString Config::dataDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/purrfind";
}

QString Config::databasePath() { return dataDirectory() + "/index.sqlite3"; }

ConfigData Config::defaults()
{
    const QString home = clean(QDir::homePath());
    ConfigData result;
    result.includedPaths = {home};
    // Keep HOME as the zero-configuration root, but avoid application state,
    // package caches and generated tool data that routinely contain hundreds
    // of thousands of files. These are explicit, editable paths: dotfiles in
    // ordinary user folders remain indexable.
    result.excludedPaths = {
        home + "/.cache",
        home + "/.config",
        home + "/.local",
        home + "/.var",
        home + "/.android",
        home + "/.cargo/git",
        home + "/.cargo/registry",
        home + "/.gradle",
        home + "/.npm",
        home + "/.ollama",
        home + "/.pub-cache",
        home + "/.rustup",
        home + "/.steam",
        home + "/.vscode",
        clean(dataDirectory()),
    };
    // Ship a complete OCR default. The settings UI still lists only packs
    // installed on the system, and explicit user selections are preserved.
    result.ocrLanguages = {"eng", "osd", "por"};
    return result;
}

ConfigData Config::load(QString *error)
{
    QFile file(configPath());
    if (!file.exists()) {
        auto value = defaults();
        save(value, error);
        return value;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return defaults();
    }
    ConfigData value;
    if (!fromJson(QString::fromUtf8(file.readAll()), &value, error)) return defaults();
    return value;
}

bool Config::save(const ConfigData &config, QString *error)
{
    if (!QDir().mkpath(configDirectory())) {
        if (error) *error = "Unable to create configuration directory";
        return false;
    }
    QFile::setPermissions(configDirectory(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile file(configPath());
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(objectFor(config)).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

QString Config::toJson(const ConfigData &config)
{
    return QString::fromUtf8(QJsonDocument(objectFor(config)).toJson(QJsonDocument::Compact));
}

bool Config::fromJson(const QString &json, ConfigData *config, QString *error)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = parseError.errorString();
        return false;
    }
    const auto object = document.object();
    ConfigData result = defaults();
    auto readPaths = [](const QJsonValue &value) {
        QStringList paths;
        for (const auto &entry : value.toArray()) {
            const QString path = clean(entry.toString());
            if (!path.isEmpty() && !paths.contains(path)) paths.append(path);
        }
        return paths;
    };
    if (object.contains("includedPaths")) result.includedPaths = readPaths(object["includedPaths"]);
    if (object.contains("excludedPaths")) result.excludedPaths = readPaths(object["excludedPaths"]);
    const QString internalData = clean(dataDirectory());
    if (!result.excludedPaths.contains(internalData)) result.excludedPaths.append(internalData);
    result.showHidden = object.value("showHidden").toBool(result.showHidden);
    result.maxResults = qBound(10, object.value("maxResults").toInt(result.maxResults), 1000);
    result.globalShortcut = object.value("globalShortcut").toString(result.globalShortcut).trimmed();
    if (result.globalShortcut.isEmpty()) result.globalShortcut = "Super+F";
    const QString themeMode = object.value("themeMode").toString(result.themeMode).toLower().trimmed();
    result.themeMode = QStringList{"system", "light", "dark"}.contains(themeMode) ? themeMode : "system";
    result.contentIndexingEnabled = object.value("contentIndexingEnabled").toBool(result.contentIndexingEnabled);
    result.contentIndexingPaused = object.value("contentIndexingPaused").toBool(result.contentIndexingPaused);
    if (object.contains("contentTypes")) {
        result.contentTypes.clear();
        for (const auto &entry : object.value("contentTypes").toArray()) {
            const QString type = entry.toString().toLower();
            if (!type.isEmpty() && !result.contentTypes.contains(type)) result.contentTypes.append(type);
        }
    }
    if (object.contains("contentExcludedPaths"))
        result.contentExcludedPaths = readPaths(object.value("contentExcludedPaths"));
    result.maximumContentFileBytes = qBound<qint64>(1024 * 1024,
        object.value("maximumContentFileBytes").toInteger(result.maximumContentFileBytes), 1024LL * 1024 * 1024);
    result.maximumExtractedTextBytes = qBound<qint64>(256 * 1024,
        object.value("maximumExtractedTextBytes").toInteger(result.maximumExtractedTextBytes), 64LL * 1024 * 1024);
    result.previewAutomatically = object.value("previewAutomatically").toBool(result.previewAutomatically);
    result.advancedImageMetadata = object.value("advancedImageMetadata").toBool(result.advancedImageMetadata);
    result.usageRankingEnabled = object.value("usageRankingEnabled").toBool(result.usageRankingEnabled);
    result.ocrPdfEnabled = object.value("ocrPdfEnabled").toBool(result.ocrPdfEnabled);
    result.ocrImagesEnabled = object.value("ocrImagesEnabled").toBool(result.ocrImagesEnabled);
    result.ocrPaused = object.value("ocrPaused").toBool(result.ocrPaused);
    if (object.contains("ocrLanguages")) {
        result.ocrLanguages.clear();
        for (const auto &entry : object.value("ocrLanguages").toArray()) {
            const QString language = entry.toString().trimmed();
            if (!language.isEmpty() && !result.ocrLanguages.contains(language)) result.ocrLanguages.append(language);
        }
    }
    const QString profile = object.value("ocrResourceProfile").toString(result.ocrResourceProfile).toLower();
    result.ocrResourceProfile = QStringList{"low", "normal", "high"}.contains(profile) ? profile : "low";
    result.ocrReduceOnBattery = object.value("ocrReduceOnBattery").toBool(result.ocrReduceOnBattery);
    result.ocrPauseBelowThirtyPercent = object.value("ocrPauseBelowThirtyPercent").toBool(result.ocrPauseBelowThirtyPercent);
    result.ocrMaximumPdfPages = qBound(1, object.value("ocrMaximumPdfPages").toInt(result.ocrMaximumPdfPages), 10000);
    result.ocrMaximumPdfBytes = qBound<qint64>(1024 * 1024,
        object.value("ocrMaximumPdfBytes").toInteger(result.ocrMaximumPdfBytes), 4LL * 1024 * 1024 * 1024);
    result.ocrDpi = qBound(150, object.value("ocrDpi").toInt(result.ocrDpi), 300);
    result.ocrPageTimeoutSeconds = qBound(10, object.value("ocrPageTimeoutSeconds").toInt(result.ocrPageTimeoutSeconds), 600);
    if (object.contains("ocrExcludedPaths")) result.ocrExcludedPaths = readPaths(object.value("ocrExcludedPaths"));
    *config = result;
    return true;
}

} // namespace purrfind
