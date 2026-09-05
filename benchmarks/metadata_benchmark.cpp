#include "core/Database.h"
#include "core/SearchEngine.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>
#include <sys/resource.h>

namespace {
qint64 rssBytes()
{
    rusage usage{};
    return getrusage(RUSAGE_SELF, &usage) == 0 ? static_cast<qint64>(usage.ru_maxrss) * 1024 : -1;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    qint64 count = 100000;
    const int option = application.arguments().indexOf("--images");
    if (option >= 0 && option + 1 < application.arguments().size())
        count = application.arguments().at(option + 1).toLongLong();
    QTemporaryDir temporary;
    purrfind::Database database;
    QString error;
    const QString path = temporary.path() + "/metadata.sqlite3";
    if (!database.open(path, false, &error) || !database.migrate(&error)) return 1;
    QVector<purrfind::FileRecord> batch;
    for (qint64 index = 0; index < count; ++index) {
        purrfind::FileRecord file;
        file.name = QString("photo-%1.jpg").arg(index); file.path = "/photos/" + file.name;
        file.parentPath = "/photos"; file.extension = "jpg"; file.type = "image/jpeg";
        file.size = 4 * 1024 * 1024; file.mtime = 1788470400; file.ctime = file.mtime;
        file.inode = index + 1; file.device = 1; file.root = "/photos"; file.scanGeneration = 1;
        batch.append(std::move(file));
        if (batch.size() == 1000) { if (!database.upsertBatch(batch, &error)) return 1; batch.clear(); }
    }
    if (!database.upsertBatch(batch, &error)) return 1;
    sqlite3_wal_checkpoint_v2(database.handle(), nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr);
    const qint64 baselineBytes = QFileInfo(path).size();
    QElapsedTimer timer; timer.start();
    qint64 stored = 0;
    while (stored < count) {
        const auto pending = database.pendingMetadata(500, &error);
        if (pending.isEmpty()) break;
        QVector<purrfind::MetadataUpdate> updates;
        for (const auto &file : pending) {
            purrfind::RichMetadata metadata;
            metadata.cameraMake = file.id % 2 ? "Canon" : "Nikon";
            metadata.cameraModel = QString("Model-%1").arg(file.id % 20);
            metadata.width = 2000 + file.id % 4000; metadata.height = 1200 + file.id % 2400;
            metadata.dateTaken = 1788470400 - file.id % 31536000;
            metadata.orientation = 1; metadata.iso = 100 + file.id % 800;
            updates.append({file, std::move(metadata)});
        }
        if (!database.storeRichMetadataBatch(updates, &error)) return 1;
        stored += updates.size();
    }
    const qint64 indexingMs = timer.elapsed();
    sqlite3_wal_checkpoint_v2(database.handle(), nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr);
    const qint64 enrichedBytes = QFileInfo(path).size();
    purrfind::SearchEngine search(database);
    QVector<double> timings;
    for (int round = 0; round < 100; ++round) {
        QElapsedTimer queryTimer; queryTimer.start();
        const auto results = search.search("camera:canon width:>3000 height:>2000 type:image", 100, true, &error);
        timings.append(queryTimer.nsecsElapsed() / 1.0e6);
        if (results.isEmpty() || !error.isEmpty()) return 1;
    }
    std::sort(timings.begin(), timings.end());
    double sum = 0; for (double value : timings) sum += value;
    std::cout.setf(std::ios::fixed); std::cout.precision(2);
    std::cout << "images=" << count << "\nmetadata_insert_ms=" << indexingMs
              << "\nmetadata_rows_per_second=" << count * 1000.0 / indexingMs
              << "\nmetadata_query_average_ms=" << sum / timings.size()
              << "\nmetadata_query_p95_ms=" << timings.at(94)
              << "\nbaseline_database_bytes=" << baselineBytes
              << "\nenriched_database_bytes=" << enrichedBytes
              << "\nmetadata_growth_bytes=" << enrichedBytes - baselineBytes
              << "\npeak_rss_bytes=" << rssBytes() << '\n';
    return 0;
}
