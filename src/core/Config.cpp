#include "core/Config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace purrfind {
namespace {

QString clean(const QString &path)
{
    return QDir::cleanPath(QDir(path).absolutePath());
}

QString canonicalizeDocumentPortalPath(const QString &path)
{
    const QString normalized = clean(path);
    // Qt's sandboxed FolderDialog can return a document-portal mount such as
    // /run/user/1000/doc/<token>/<user>/Pictures. Persist the real home path
    // instead, otherwise the mount token leaks into the index and changes on
    // every portal grant.
    static const QRegularExpression portalPath(
        QStringLiteral("^/run/user/[0-9]+/doc/[^/]+/(.+)$"));
    const auto match = portalPath.match(normalized);
    if (!match.hasMatch()) return normalized;
    const QString home = clean(QDir::homePath());
    const QString homeName = QFileInfo(home).fileName();
    const QString remainder = match.captured(1);
    if (remainder == homeName || remainder.startsWith(homeName + '/'))
        return clean(home + remainder.mid(homeName.size()));
    return normalized;
}

QJsonObject objectFor(const ConfigData &config)
{
    QJsonArray included;
    for (const auto &path : config.includedPaths) included.append(path);
    QJsonArray excluded;
    for (const auto &path : config.excludedPaths) excluded.append(path);
    return {
        {"version", 5},
        {"includedPaths", included},
        {"excludedPaths", excluded},
        {"excludeHidden", config.excludeHidden},
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
    // Keep HOME as the zero-configuration root, but make the system and
    // application state directories visible in the settings as blocked
    // defaults. Hidden components are also filtered structurally by the
    // crawler, while an explicitly added hidden root remains an exception.
    result.excludedPaths = {
        home + "/.cache",
        home + "/.config",
        home + "/.local",
        home + "/.var",
        home + "/.android",
        home + "/.gnupg",
        home + "/.mozilla",
        home + "/.pki",
        home + "/.ssh",
        home + "/.cargo/git",
        home + "/.cargo/registry",
        home + "/.gradle",
        home + "/.npm",
        home + "/.ollama",
        home + "/.pub-cache",
        home + "/.rustup",
        home + "/.steam",
        home + "/.vscode",
        "/dev",
        "/proc",
        "/run",
        "/sys",
        "/tmp",
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
    const QByteArray raw = file.readAll();
    if (!fromJson(QString::fromUtf8(raw), &value, error)) return defaults();
    QJsonParseError migrationError;
    const auto document = QJsonDocument::fromJson(raw, &migrationError);
    if (migrationError.error == QJsonParseError::NoError && document.isObject()
        && document.object().value("version").toInt(1) < 5) {
        file.close();
        save(value, nullptr);
    }
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
    const int storedVersion = object.value("version").toInt(1);
    ConfigData result = defaults();
    auto readPaths = [](const QJsonValue &value) {
        QStringList paths;
        for (const auto &entry : value.toArray()) {
            const QString path = canonicalizeDocumentPortalPath(entry.toString());
            if (!path.isEmpty() && !paths.contains(path)) paths.append(path);
        }
        return paths;
    };
    if (object.contains("includedPaths")) result.includedPaths = readPaths(object["includedPaths"]);
    if (object.contains("excludedPaths")) {
        result.excludedPaths = readPaths(object["excludedPaths"]);
        // Preserve mandatory defaults when upgrading a configuration written
        // by an older release that stored only the user's custom exclusions.
        for (const auto &required : defaults().excludedPaths)
            if (!result.excludedPaths.contains(required)) result.excludedPaths.append(required);
    }
    const QString internalData = clean(dataDirectory());
    if (!result.excludedPaths.contains(internalData)) result.excludedPaths.append(internalData);
    result.excludeHidden = object.value("excludeHidden").toBool(result.excludeHidden);
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
    // rc2 could persist an unchecked image-OCR box even though a fresh
    // installation is intended to process images. Migrate that old schema to
    // the complete default once; later explicit choices remain persistent.
    if (storedVersion < 5) result.ocrImagesEnabled = true;
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
