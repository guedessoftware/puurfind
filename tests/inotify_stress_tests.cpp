#include "core/Config.h"
#include "core/Database.h"
#include "indexer/Crawler.h"
#include "indexer/InotifyWatcher.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir root;
    QTemporaryDir state;
    if (!root.isValid() || !state.isValid()) return 1;
    qputenv("XDG_DATA_HOME", state.path().toUtf8());

    purrfind::InotifyWatcher watcher;
    bool reconcileReceived = false;
    qint64 delivered = 0;
    QObject::connect(&watcher, &purrfind::InotifyWatcher::eventReceived,
                     [&](const purrfind::FsEvent &event) {
        ++delivered;
        if (event.kind == purrfind::EventKind::Reconcile) reconcileReceived = true;
    });
    QString error;
    if (!watcher.start({root.path()}, {}, &error)) {
        std::cerr << error.toStdString() << '\n'; return 1;
    }

    for (int index = 0; index < 20000; ++index) {
        QFile file(root.path() + QString("/file-%1.txt").arg(index));
        if (!file.open(QIODevice::WriteOnly) || file.write("x") != 1) return 1;
    }
    for (int index = 0; index < 10000; ++index) {
        const QString before = root.path() + QString("/file-%1.txt").arg(index);
        const QString after = root.path() + QString("/renamed-%1.txt").arg(index);
        if (!QFile::rename(before, after)) return 1;
    }
    for (int index = 0; index < 5000; ++index) {
        if (!QFile::remove(root.path() + QString("/renamed-%1.txt").arg(index))) return 1;
    }

    QElapsedTimer drain;
    drain.start();
    while (drain.elapsed() < 10000 && !reconcileReceived)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    watcher.stop();
    if (delivered == 0 || !reconcileReceived) {
        std::cerr << "real inotify burst did not report recoverable queue overflow\n";
        return 1;
    }

    const auto crawl = purrfind::Crawler::crawl(purrfind::Config::databasePath(),
                                                 root.path(), {}, 99);
    if (!crawl.error.isEmpty()) {
        std::cerr << crawl.error.toStdString() << '\n'; return 1;
    }
    purrfind::Database database;
    if (!database.open(purrfind::Config::databasePath(), true, &error)) return 1;
    const qint64 count = database.fileCount(&error);
    if (count != 15001) {
        std::cerr << "reconciliation mismatch: expected 15001 rows, got " << count << '\n';
        return 1;
    }
    std::cout << "Real inotify overflow recovered; " << delivered
              << " events delivered and final database is consistent\n";
    return 0;
}
