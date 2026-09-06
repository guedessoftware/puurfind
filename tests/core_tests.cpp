#include "core/Config.h"
#include "core/Database.h"
#include "core/FileSystem.h"
#include "core/QueryParser.h"
#include "core/SearchEngine.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QRandomGenerator>
#include <iostream>
#include <unistd.h>

namespace {
int failures = 0;

void check(bool condition, const char *description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

purrfind::FileRecord record(const QString &path, qint64 size, qint64 mtime,
                            bool directory = false)
{
    QFileInfo info(path);
    purrfind::FileRecord value;
    value.name = info.fileName();
    value.path = path;
    value.parentPath = info.path();
    value.extension = info.suffix().toLower();
    value.type = directory ? "folder" : (value.extension == "png" ? "image/png" : "application/octet-stream");
    value.size = size;
    value.mtime = mtime;
    value.ctime = mtime;
    value.inode = qHash(path);
    value.device = 1;
    value.directory = directory;
    value.hidden = info.fileName().startsWith('.');
    value.root = "/home/test";
    value.scanGeneration = 1;
    return value;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    if (arguments.size() == 3 && arguments.at(1) == "--crash-uncommitted") {
        purrfind::Database child;
        QString childError;
        if (!child.open(arguments.at(2), false, &childError) || !child.migrate(&childError)
            || !child.begin(&childError)
            || !child.upsert(record("/home/test/crash-uncommitted.txt", 1,
                                    QDateTime::currentSecsSinceEpoch()), &childError))
            return 70;
        ::_exit(77); // Deliberately bypass destructors: simulate SIGKILL/power loss.
    }
    if (arguments.size() == 3 && arguments.at(1) == "--crash-committed") {
        purrfind::Database child;
        QString childError;
        if (!child.open(arguments.at(2), false, &childError) || !child.migrate(&childError)
            || !child.upsert(record("/home/test/crash-committed.txt", 1,
                                    QDateTime::currentSecsSinceEpoch()), &childError))
            return 71;
        ::_exit(78); // Leave the committed record in WAL without a clean close.
    }
    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary directory");
    const QString databasePath = temporary.path() + "/index.sqlite3";
    purrfind::Database database;
    QString error;
    check(database.open(databasePath, false, &error), "database opens");
    check(database.migrate(&error), "migration succeeds");
    check(database.schemaVersion(&error) == 6, "schema version is six");
    check(database.migrate(&error), "migration is idempotent");
    check(database.quickCheck(&error), "database quick integrity check");
    check(database.checkpointWal(&error), "WAL checkpoint succeeds");

    const QString failedMigrationPath = temporary.path() + "/migration-failure.sqlite3";
    purrfind::Database failedMigration;
    QString failedMigrationError;
    check(failedMigration.open(failedMigrationPath, false, &failedMigrationError)
              && failedMigration.migrate(&failedMigrationError, 1),
          "migration failure fixture starts at schema one");
    check(sqlite3_exec(failedMigration.handle(),
                       "ALTER TABLE files ADD COLUMN content_extractor TEXT NOT NULL DEFAULT '';",
                       nullptr, nullptr, nullptr) == SQLITE_OK,
          "migration failure fixture creates a conflicting second step");
    check(!failedMigration.migrate(&failedMigrationError),
          "migration failure is reported");
    sqlite3_stmt *contentStateColumn = nullptr;
    check(failedMigration.schemaVersion(nullptr) == 1
              && sqlite3_prepare_v2(failedMigration.handle(),
                                    "SELECT content_state FROM files LIMIT 1", -1,
                                    &contentStateColumn, nullptr) != SQLITE_OK,
          "failed migration rolls back earlier statements and schema version");
    sqlite3_finalize(contentStateColumn);
    failedMigration.close();
    const purrfind::ConfigData defaultConfig = purrfind::Config::defaults();
    const QString defaultHome = QDir::homePath();
    check(defaultConfig.includedPaths == QStringList{defaultHome}
              && defaultConfig.excludeHidden
              && !defaultConfig.showHidden
              && defaultConfig.excludedPaths.contains(defaultHome + "/.config")
              && defaultConfig.excludedPaths.contains(defaultHome + "/.local")
              && defaultConfig.excludedPaths.contains(defaultHome + "/.cache")
              && defaultConfig.excludedPaths.contains(purrfind::Config::dataDirectory())
              && defaultConfig.ocrImagesEnabled
              && defaultConfig.ocrLanguages == QStringList{"eng", "osd", "por"},
          "default scope keeps HOME, enables complete OCR, and excludes application state");
    for (int historical = 1; historical <= 5; ++historical) {
        const QString legacyPath = temporary.path() + QString("/schema-v%1.sqlite3").arg(historical);
        purrfind::Database legacy;
        QString migrationError;
        check(legacy.open(legacyPath, false, &migrationError)
              && legacy.migrate(&migrationError, historical)
              && legacy.schemaVersion(&migrationError) == historical,
              "historical schema materializes transactionally");
        const QByteArray insert = QString("INSERT INTO files(name,path,parent_path,extension,type,size,mtime,ctime,inode,device,flags,is_dir,is_symlink,root,scan_generation) "
                                          "VALUES('schema-%1.txt','/home/test/schema-%1.txt','/home/test','txt','text/plain',1,1,1,%1,1,0,0,0,'/home/test',1)")
                                      .arg(historical).toUtf8();
        check(sqlite3_exec(legacy.handle(), insert.constData(), nullptr, nullptr, nullptr) == SQLITE_OK,
              "historical schema accepts file row");
        legacy.close();
        check(legacy.open(legacyPath, false, &migrationError)
              && legacy.migrate(&migrationError)
              && legacy.schemaVersion(&migrationError) == 6
              && legacy.fileCount(&migrationError) == 1,
              "historical migration reaches current schema and preserves rows");
    }

    const QString ocrV5Path = temporary.path() + "/ocr-schema-v5.sqlite3";
    purrfind::Database ocrV5;
    QString ocrMigrationError;
    check(ocrV5.open(ocrV5Path, false, &ocrMigrationError)
              && ocrV5.migrate(&ocrMigrationError, 5),
          "schema v5 OCR fixture opens");
    auto oldOcrImage = record("/home/test/legacy-ocr.png", 42, 10);
    check(ocrV5.upsert(oldOcrImage, &ocrMigrationError)
              && sqlite3_exec(ocrV5.handle(),
                    "UPDATE files SET ocr_state=4 WHERE path='/home/test/legacy-ocr.png';"
                    "INSERT INTO ocr_pages(file_id,page_number,text,confidence) "
                    "SELECT id,1,'old pipeline text',90 FROM files WHERE path='/home/test/legacy-ocr.png';",
                    nullptr, nullptr, nullptr) == SQLITE_OK,
          "schema v5 contains an indexed image OCR result");
    ocrV5.close();
    check(ocrV5.open(ocrV5Path, false, &ocrMigrationError)
              && ocrV5.migrate(&ocrMigrationError),
          "schema v6 OCR migration succeeds");
    sqlite3_stmt *ocrMigrationState = nullptr;
    const bool ocrStateReadable = sqlite3_prepare_v2(
        ocrV5.handle(),
        "SELECT ocr_state,ocr_engine_version,(SELECT count(*) FROM ocr_pages) "
        "FROM files WHERE path='/home/test/legacy-ocr.png'", -1, &ocrMigrationState, nullptr) == SQLITE_OK;
    check(ocrStateReadable && sqlite3_step(ocrMigrationState) == SQLITE_ROW
              && sqlite3_column_int(ocrMigrationState, 0) == static_cast<int>(purrfind::OcrState::NotRequired)
              && sqlite3_column_int(ocrMigrationState, 1) == 0
              && sqlite3_column_int(ocrMigrationState, 2) == 0,
          "schema v6 invalidates only legacy image OCR for policy rescheduling");
    sqlite3_finalize(ocrMigrationState);
    ocrV5.close();

    const QString ocrPolicyPath = temporary.path() + "/ocr-policy.sqlite3";
    purrfind::Database ocrPolicy;
    check(ocrPolicy.open(ocrPolicyPath, false, &error)
              && ocrPolicy.migrate(&error)
              && ocrPolicy.upsert(record("/home/test/policy.png", 42, 10), &error)
              && sqlite3_exec(ocrPolicy.handle(),
                    "UPDATE files SET ocr_state=7,ocr_error='OCR language pack not found' "
                    "WHERE path='/home/test/policy.png';", nullptr, nullptr, nullptr) == SQLITE_OK
              && ocrPolicy.applyOcrPolicy(false, true, &error),
          "OCR policy requeues images after language packs become available");
    const auto requeuedOcr = ocrPolicy.pendingOcr(false, true, false, 10, &error);
    check(requeuedOcr.size() == 1, "OCR policy exposes requeued image");
    ocrPolicy.close();

    purrfind::ConfigData phase3Config = purrfind::Config::defaults();
    phase3Config.previewAutomatically = false;
    phase3Config.advancedImageMetadata = false;
    phase3Config.usageRankingEnabled = false;
    const QString phase3Json = purrfind::Config::toJson(phase3Config);
    purrfind::ConfigData restoredConfig;
    check(phase3Json.contains("\"version\":5")
              && purrfind::Config::fromJson(phase3Json, &restoredConfig, &error)
              && !restoredConfig.previewAutomatically
              && !restoredConfig.advancedImageMetadata
              && !restoredConfig.usageRankingEnabled,
          "phase four configuration preserves earlier preferences");
    for (const QString &mode : {QStringLiteral("system"), QStringLiteral("light"), QStringLiteral("dark")}) {
        auto themed = purrfind::Config::defaults();
        themed.themeMode = mode;
        purrfind::ConfigData restoredTheme;
        check(purrfind::Config::fromJson(purrfind::Config::toJson(themed), &restoredTheme, &error)
                  && restoredTheme.themeMode == mode,
              "theme preference round-trip");
    }
    purrfind::ConfigData invalidTheme;
    check(purrfind::Config::fromJson("{\"themeMode\":\"neon\"}", &invalidTheme, &error)
              && invalidTheme.themeMode == "system", "invalid theme falls back to system");

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QVector<purrfind::FileRecord> records{
        record("/home/test/Documents/Contrato-FIRENETWORK-2026.pdf", 15 * 1024 * 1024, now),
        record("/home/test/Documents/fire-notes.txt", 100, now - 10 * 86400),
        record("/home/test/Pictures/firework.png", 2 * 1024 * 1024, now),
        record("/home/test/.secret-fire", 30, now),
        record("/home/test/Documents", 0, now, true),
    };
    check(database.upsertBatch(records, &error), "batch insertion");
    check(database.fileCount(&error) == 5, "file count");
    check(sqlite3_exec(database.handle(),
          "INSERT INTO ocr_pages(file_id,page_number,text,confidence) "
          "SELECT id,1,'TriBrey',47 FROM files WHERE path='/home/test/Pictures/firework.png'",
          nullptr, nullptr, nullptr) == SQLITE_OK, "small-text OCR fixture");

    purrfind::SearchEngine search(database);
    auto result = search.search("fire", 100, true, &error);
    check(result.size() == 4, "trigram substring search");
    check(!result.isEmpty() && result.first().file.name.startsWith("fire", Qt::CaseInsensitive),
          "name-prefix ranking beats contained path matches");
    result = search.search("work", 100, true, &error);
    check(result.size() == 2, "substring inside a larger token");
    result = search.search("fi", 100, true, &error);
    check(result.size() == 2, "short input uses fast filename-prefix search");
    result = search.search("path:Pi", 100, true, &error);
    check(result.size() == 1 && result.first().file.name == "firework.png",
          "short explicit path scope searches paths");
    result = search.search("Pictures", 100, true, &error);
    check(result.isEmpty(), "unified search does not match descendant paths");
    result = search.search("path:Pictures", 100, true, &error);
    check(result.size() == 1 && result.first().file.name == "firework.png", "explicit path search");
    result = search.search("fire type:pdf size:>10MB modified:today", 100, true, &error);
    check(result.size() == 1 && result.first().file.extension == "pdf", "combined filters");
    result = search.search("folder:Pictures category:image", 100, true, &error);
    check(result.size() == 1 && result.first().file.name == "firework.png", "folder and category filters");
    result = search.search("fire", 100, false, &error);
    check(result.size() == 3, "hidden file policy");
    result = search.search("kind:folder", 100, true, &error);
    check(result.size() == 1 && result.first().file.directory, "folder filter");
    result = search.search("tribrer", 100, true, &error);
    check(result.size() == 1 && result.first().file.name == "firework.png"
              && result.first().matchOrigin == "ocr",
          "OCR search tolerates a misread final small glyph");

    auto parsed = purrfind::QueryParser::parse("contract type:PDF size:<1GB modified:7d in:Documents");
    check(parsed.text == "contract" && parsed.extension == "pdf", "query parser text and extension");
    check(parsed.maximumSize == 1024LL * 1024 * 1024, "query parser size");
    check(parsed.modified == purrfind::ModifiedFilter::Days7 && parsed.folder == "Documents", "query parser date and folder");
    parsed = purrfind::QueryParser::parse("camera:Canon pages:>20 width:>3000 height:<5000 type:image author:\"João\"");
    check(parsed.camera == "Canon" && parsed.author == "João" && parsed.minimumPages == 20
          && parsed.minimumWidth == 3000 && parsed.maximumHeight == 5000
          && parsed.category == "image", "metadata query filters");
    parsed = purrfind::QueryParser::parse("source:ocr FIRENETWORK");
    check(parsed.source == "ocr" && parsed.text == "FIRENETWORK", "OCR source query filter");
    purrfind::ConfigData ocrConfig = purrfind::Config::defaults();
    ocrConfig.ocrPdfEnabled = false; ocrConfig.ocrImagesEnabled = true;
    ocrConfig.ocrPaused = true; ocrConfig.ocrLanguages = {"eng"};
    ocrConfig.ocrResourceProfile = "normal"; ocrConfig.ocrMaximumPdfPages = 77;
    purrfind::ConfigData restoredOcrConfig;
    check(purrfind::Config::fromJson(purrfind::Config::toJson(ocrConfig), &restoredOcrConfig, &error)
          && !restoredOcrConfig.ocrPdfEnabled && restoredOcrConfig.ocrImagesEnabled
          && restoredOcrConfig.ocrPaused && restoredOcrConfig.ocrLanguages == QStringList{"eng"}
          && restoredOcrConfig.ocrResourceProfile == "normal" && restoredOcrConfig.ocrMaximumPdfPages == 77,
          "OCR preferences round-trip");
    bool sizeOk = false;
    check(purrfind::QueryParser::parseSize("1.5 MB", &sizeOk) == 1572864 && sizeOk, "decimal size parsing");

    check(purrfind::FileSystem::normalizePath("/tmp/a/../b") == "/tmp/b", "path normalization");
    check(purrfind::FileSystem::isWithin("/tmp/base/file", "/tmp/base"), "path containment");
    check(!purrfind::FileSystem::isWithin("/tmp/baseball", "/tmp/base"), "path containment boundary");
    check(purrfind::FileSystem::isHiddenWithin("/tmp/base/.cache/file", "/tmp/base")
              && !purrfind::FileSystem::isHiddenWithin("/tmp/base/.cache/file", "/tmp/base/.cache"),
          "hidden path policy permits explicit hidden roots");
    const QString portalHome = QDir::homePath();
    const QString portalPath = "/run/user/1000/doc/purrfind-test/" + QFileInfo(portalHome).fileName() + "/Pictures";
    purrfind::ConfigData portalConfig;
    check(purrfind::Config::fromJson(
              QString("{\"includedPaths\":[\"%1\"],\"version\":4}").arg(portalPath),
              &portalConfig, &error)
              && portalConfig.includedPaths.first() == portalHome + "/Pictures",
          "document portal paths resolve to the real home directory");

    const QStringList unusualNames{
        "space name.txt", "tab\tname.txt", "line\nname.txt", "aspas-'\".txt",
        "emoji-🐈.txt", "português-conexão.txt", "日本語.txt", "中文.txt",
        "Русский.txt", "العربية.txt", QString::fromUtf8("combining-e\u0301.txt"), "back\\slash.txt"
    };
    QVector<purrfind::FileRecord> unusualRecords;
    qint64 unusualInode = 10000;
    for (const auto &name : unusualNames) {
        auto value = record("/home/test/odd/" + name, 1, now);
        value.inode = unusualInode++;
        unusualRecords.append(value);
    }
    check(database.upsertBatch(unusualRecords, &error), "unusual UTF-8 paths are indexed");
    check(!search.search("日本語", 10, true, &error).isEmpty(), "Japanese filename search");
    check(!search.search("conexão", 10, true, &error).isEmpty(), "accented filename search");
    check(!search.search("emoji-🐈", 10, true, &error).isEmpty(), "emoji filename search");

    const QStringList hostileQueries{"\"", "'''", ":", "::", "()", "*", "-", "AND", "OR", "NEAR",
                                     "name:\"", "size:>>>>>>>>", "source:ocr *", "\\\\\\", "(((())))"};
    for (const auto &query : hostileQueries) {
        error.clear();
        search.search(query, 20, true, &error);
    }
    QRandomGenerator random(0x50555252);
    const QString alphabet = "abcXYZ012 :\"'()*-_\\/🐈ç日";
    for (int sample = 0; sample < 2000; ++sample) {
        QString query;
        const int length = random.bounded(96);
        for (int i = 0; i < length; ++i) query += alphabet.at(random.bounded(alphabet.size()));
        purrfind::QueryParser::parse(query);
    }
    check(true, "query parser fuzz corpus completed without crash");

    const QString linkTarget = temporary.path() + "/target";
    QFile target(linkTarget); check(target.open(QIODevice::WriteOnly), "open symlink target"); target.write("x"); target.close();
    const QString linkPath = temporary.path() + "/link";
    check(QFile::link(linkTarget, linkPath), "create symlink");
    auto link = purrfind::FileSystem::inspect(linkPath, temporary.path(), 2, &error);
    check(link && link->symlink, "symlink metadata without following recursively");

    const QString hardlinkPath = temporary.path() + "/hardlink";
    check(::link(QFile::encodeName(linkTarget).constData(), QFile::encodeName(hardlinkPath).constData()) == 0,
          "create hardlink");
    const auto hardlinkTarget = purrfind::FileSystem::inspect(linkTarget, temporary.path(), 2, &error);
    const auto hardlink = purrfind::FileSystem::inspect(hardlinkPath, temporary.path(), 2, &error);
    check(hardlinkTarget && hardlink && hardlinkTarget->inode == hardlink->inode
              && hardlinkTarget->device == hardlink->device && !hardlink->symlink,
          "hardlinks retain shared device and inode identity");

    const QString brokenLink = temporary.path() + "/broken-link";
    check(::symlink("missing-target", QFile::encodeName(brokenLink).constData()) == 0,
          "create broken symlink");
    const auto broken = purrfind::FileSystem::inspect(brokenLink, temporary.path(), 2, &error);
    check(broken && broken->symlink, "broken symlink is indexed without dereference");

    auto renamed = records.at(1);
    renamed.path = "/home/test/Documents/renamed.txt";
    renamed.name = "renamed.txt";
    check(database.upsert(renamed, &error), "rename target insert");
    check(database.removePath(records.at(1).path, false, &error), "rename source delete");
    check(search.search("renamed", 10, true, &error).size() == 1, "renamed entry searchable");
    check(database.removePath("/home/test/Pictures", true, &error), "recursive deletion");
    check(search.search("firework", 10, true, &error).isEmpty(), "deleted entry absent");

    database.close();
    check(database.open(databasePath, false, &error) && database.migrate(&error), "database reopens after migration");
    check(database.begin(&error), "interrupted transaction begins");
    auto uncommitted = record("/home/test/uncommitted.txt", 2, now);
    check(database.upsert(uncommitted, &error), "uncommitted row is staged");
    database.close();
    check(database.open(databasePath, false, &error) && database.migrate(&error)
          && search.search("uncommitted", 10, true, &error).isEmpty(),
          "shutdown during transaction rolls back safely");

    const QString crashDatabasePath = temporary.path() + "/crash.sqlite3";
    QProcess crashedWriter;
    crashedWriter.start(QCoreApplication::applicationFilePath(),
                        {"--crash-uncommitted", crashDatabasePath});
    check(crashedWriter.waitForFinished(10000) && crashedWriter.exitCode() == 77,
          "uncommitted writer is terminated without cleanup");
    purrfind::Database crashDatabase;
    check(crashDatabase.open(crashDatabasePath, false, &error)
              && crashDatabase.migrate(&error)
              && crashDatabase.quickCheck(&error),
          "database recovers after abrupt uncommitted writer exit");
    purrfind::SearchEngine crashSearch(crashDatabase);
    check(crashSearch.search("crash-uncommitted", 10, true, &error).isEmpty(),
          "abrupt uncommitted write is rolled back");
    crashDatabase.close();

    QProcess committedWriter;
    committedWriter.start(QCoreApplication::applicationFilePath(),
                          {"--crash-committed", crashDatabasePath});
    check(committedWriter.waitForFinished(10000) && committedWriter.exitCode() == 78,
          "committed writer exits with WAL pending");
    check(QFileInfo::exists(crashDatabasePath + "-wal"),
          "abrupt committed writer leaves a WAL to recover");
    check(crashDatabase.open(crashDatabasePath, false, &error)
              && crashDatabase.migrate(&error)
              && crashDatabase.quickCheck(&error),
          "database opens and validates a pending WAL");
    purrfind::SearchEngine walSearch(crashDatabase);
    check(walSearch.search("crash-committed", 10, true, &error).size() == 1,
          "committed WAL record survives abrupt exit");
    check(crashDatabase.checkpointWal(&error), "recovered WAL checkpoints safely");
    if (!error.isEmpty()) std::cerr << "Last database message: " << error.toStdString() << '\n';
    std::cout << (failures ? "Core tests failed" : "All core tests passed") << '\n';
    return failures ? 1 : 0;
}
