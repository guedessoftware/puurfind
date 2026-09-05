#include "core/Database.h"
#include "core/Config.h"
#include "core/FileSystem.h"
#include "core/SearchEngine.h"
#include "indexer/Crawler.h"
#include "indexer/ContentIndexQueue.h"
#include "indexer/EventQueue.h"
#include "indexer/InotifyWatcher.h"
#include "indexer/MetadataQueue.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QDir>
#include <QTemporaryDir>
#include <QThread>
#include <functional>
#include <iostream>
#include <unistd.h>

namespace {
bool waitFor(const std::function<bool()> &predicate, int timeout = 3000)
{
    QElapsedTimer elapsed; elapsed.start();
    while (elapsed.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (predicate()) return true;
        QThread::msleep(15);
    }
    return predicate();
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir root;
    QTemporaryDir data;
    if (!root.isValid() || !data.isValid()) return 1;
    qputenv("XDG_DATA_HOME", data.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (data.path() + "/config").toUtf8());
    const QString databasePath = purrfind::Config::databasePath();
    const qint64 generation = 42;
    auto crawl = purrfind::Crawler::crawl(databasePath, root.path(), {}, generation);
    if (!crawl.error.isEmpty()) { std::cerr << crawl.error.toStdString(); return 1; }

    purrfind::Database database;
    QString error;
    if (!database.open(databasePath, false, &error) || !database.migrate(&error)) return 1;
    purrfind::SearchEngine search(database);
    purrfind::InotifyWatcher watcher;
    purrfind::EventQueue queue(nullptr, 40);
    purrfind::ConfigData contentConfig = purrfind::Config::defaults();
    contentConfig.includedPaths = {root.path()};
    contentConfig.contentTypes = {"txt"};
    purrfind::ContentIndexQueue contentQueue;
    contentQueue.start(contentConfig);
    purrfind::MetadataQueue metadataQueue;
    metadataQueue.start(contentConfig);
    QObject::connect(&watcher, &purrfind::InotifyWatcher::eventReceived, &queue, &purrfind::EventQueue::enqueue);
    QObject::connect(&queue, &purrfind::EventQueue::ready, [&](const QVector<purrfind::FsEvent> &events) {
        database.begin(nullptr);
        for (const auto &event : events) {
            contentQueue.invalidate(event.kind == purrfind::EventKind::Rename ? event.oldPath : event.path);
            metadataQueue.invalidate(event.kind == purrfind::EventKind::Rename ? event.oldPath : event.path);
            if (event.kind == purrfind::EventKind::Rename) {
                if (auto item = purrfind::FileSystem::inspect(event.path, root.path(), generation))
                    database.movePathPreservingContent(event.oldPath, *item, event.directory, nullptr);
            } else if (event.kind == purrfind::EventKind::Remove) database.removePath(event.path, event.directory, nullptr);
            else if (event.kind == purrfind::EventKind::Upsert) {
                if (auto item = purrfind::FileSystem::inspect(event.path, root.path(), generation)) database.upsert(*item, nullptr);
                else database.removePath(event.path, event.directory, nullptr);
            }
        }
        database.commit(nullptr);
        contentQueue.notifyWork();
        metadataQueue.notifyWork();
    });
    if (!watcher.start({root.path()}, {}, &error)) { std::cerr << error.toStdString(); return 1; }

    const QString original = root.path() + "/instant-contract.txt";
    QFile file(original); if (!file.open(QIODevice::WriteOnly)) return 1; file.write("alpha FIRENETWORK"); file.close();
    if (!waitFor([&] { return search.search("contract", 10, true).size() == 1; })) {
        std::cerr << "create event was not indexed\n"; return 1;
    }
    contentQueue.notifyWork();
    if (!waitFor([&] { return search.search("content:alpha", 10, true).size() == 1; }, 5000)) {
        std::cerr << "created file content was not indexed\n"; return 1;
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 1;
    file.write("beta replacement content"); file.close();
    if (!waitFor([&] { return search.search("content:beta", 10, true).size() == 1
                            && search.search("content:alpha", 10, true).isEmpty(); }, 5000)) {
        std::cerr << "modified content was not refreshed\n"; return 1;
    }

    const QString renamed = root.path() + "/instant-agreement.txt";
    if (!QFile::rename(original, renamed)) return 1;
    if (!waitFor([&] { return search.search("agreement", 10, true).size() == 1
                            && search.search("contract", 10, true).isEmpty(); })) {
        std::cerr << "rename event was not indexed\n"; return 1;
    }
    if (search.search("content:beta", 10, true).isEmpty()) {
        std::cerr << "rename did not preserve content\n"; return 1;
    }

    if (!QFile::remove(renamed)) return 1;
    if (!waitFor([&] { return search.search("agreement", 10, true).isEmpty(); })) {
        std::cerr << "delete event was not indexed\n"; return 1;
    }
    if (!search.search("content:beta", 10, true).isEmpty()) {
        std::cerr << "delete did not remove content\n"; return 1;
    }

    const QString symlinkTree = root.path() + "/symlink-tree";
    if (!QDir().mkpath(symlinkTree + "/real/sub")) return 1;
    QFile cycleFile(symlinkTree + "/real/sub/only-once.txt");
    if (!cycleFile.open(QIODevice::WriteOnly)) return 1;
    cycleFile.write("cycle guard"); cycleFile.close();
    if (::symlink("..", QFile::encodeName(symlinkTree + "/real/sub/back").constData()) != 0
        || ::symlink("real", QFile::encodeName(symlinkTree + "/directory-link").constData()) != 0) {
        std::cerr << "could not create directory symlink fixtures\n"; return 1;
    }
    const auto symlinkCrawl = purrfind::Crawler::crawl(databasePath, root.path(), {}, generation + 1);
    if (!symlinkCrawl.error.isEmpty() || symlinkCrawl.indexed > 20
        || search.search("only-once", 10, true).size() != 1) {
        std::cerr << "directory symlink cycle was followed recursively\n"; return 1;
    }

    const QString imagePath = root.path() + "/metadata.png";
    QImage image(64, 32, QImage::Format_RGB32); image.fill(Qt::magenta);
    if (!image.save(imagePath, "PNG")) return 1;
    if (!waitFor([&] { return search.search("width:>60 height:>30 type:image", 10, true).size() == 1; }, 5000)) {
        std::cerr << "progressive image metadata was not indexed\n"; return 1;
    }

    queue.enqueue({purrfind::EventKind::Upsert, "/tmp/coalesce", false});
    queue.enqueue({purrfind::EventKind::Upsert, "/tmp/coalesce", false});
    if (queue.pendingCount() != 1 || queue.takeNow().size() != 1) {
        std::cerr << "event coalescing failed\n"; return 1;
    }
    purrfind::EventQueue burstQueue(nullptr, 60000);
    for (int i = 0; i < 20000; ++i)
        burstQueue.enqueue({purrfind::EventKind::Upsert, QString("/burst/file-%1").arg(i), false});
    for (int i = 0; i < 10000; ++i)
        burstQueue.enqueue({purrfind::EventKind::Rename, QString("/burst/renamed-%1").arg(i), false,
                            QString("/burst/file-%1").arg(i)});
    for (int i = 0; i < 5000; ++i)
        burstQueue.enqueue({purrfind::EventKind::Remove, QString("/burst/renamed-%1").arg(i), false});
    if (burstQueue.pendingCount() != 20000 || burstQueue.takeNow().size() != 20000) {
        std::cerr << "large inotify-style burst did not coalesce deterministically\n"; return 1;
    }
    burstQueue.enqueue({purrfind::EventKind::Reconcile, {}, false});
    const auto overflowRecovery = burstQueue.takeNow();
    if (overflowRecovery.size() != 1 || overflowRecovery.first().kind != purrfind::EventKind::Reconcile) {
        std::cerr << "inotify overflow did not schedule reconciliation\n"; return 1;
    }
    contentQueue.stop();
    metadataQueue.stop();
    std::cout << "Create → content → modify → rename → delete → metadata integration passed\n";
    return 0;
}
