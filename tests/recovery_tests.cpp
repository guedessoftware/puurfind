#include "core/Config.h"
#include "core/Database.h"
#include "indexer/IndexerService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfoList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>

namespace {
int failures = 0;

void check(bool condition, const char *description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir state;
    QTemporaryDir indexedRoot;
    check(state.isValid() && indexedRoot.isValid(), "temporary recovery state");
    qputenv("XDG_DATA_HOME", (state.path() + "/data").toUtf8());
    qputenv("XDG_CONFIG_HOME", (state.path() + "/config").toUtf8());

    purrfind::ConfigData config = purrfind::Config::defaults();
    config.includedPaths = {indexedRoot.path()};
    config.excludedPaths.clear();
    config.contentIndexingPaused = true;
    config.ocrPaused = true;
    QString error;
    check(purrfind::Config::save(config, &error), "write isolated recovery configuration");

    for (int attempt = 0; attempt < 5; ++attempt) {
        QDir().mkpath(purrfind::Config::dataDirectory());
        QFile corrupt(purrfind::Config::databasePath());
        check(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate), "create corrupt database");
        corrupt.write("this is deliberately not a SQLite database\n");
        corrupt.close();

        {
            purrfind::IndexerService service;
            error.clear();
            check(service.initialize(&error), "indexer automatically rebuilds a corrupt database");
            const QJsonObject status = QJsonDocument::fromJson(service.Status().toUtf8()).object();
            check(!status.value("recoveryBackup").toString().isEmpty(),
                  "recovery status exposes preserved corrupt index");
        }

        purrfind::Database rebuilt;
        check(rebuilt.open(purrfind::Config::databasePath(), false, &error)
                  && rebuilt.migrate(&error) && rebuilt.quickCheck(&error),
              "automatically rebuilt database passes integrity check");
        rebuilt.close();
    }

    QDir recovery(purrfind::Config::dataDirectory() + "/recovery");
    const QFileInfoList backups = recovery.entryInfoList(
        {"index-*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    check(backups.size() == 3, "automatic recovery retains only the three newest backups");
    std::cout << (failures ? "Recovery tests failed" : "Corruption recovery and retention passed") << '\n';
    return failures ? 1 : 0;
}
