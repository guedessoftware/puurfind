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
    QFile status("/proc/self/status");
    if (!status.open(QIODevice::ReadOnly)) return -1;
    while (true) {
        const auto line = status.readLine();
        if (line.isEmpty()) return -1;
        if (line.startsWith("VmRSS:")) return line.simplified().split(' ').value(1).toLongLong() * 1024;
    }
}

double cpuMilliseconds(const rusage &start, const rusage &end)
{
    const double startSeconds = start.ru_utime.tv_sec + start.ru_stime.tv_sec
        + (start.ru_utime.tv_usec + start.ru_stime.tv_usec) / 1.0e6;
    const double endSeconds = end.ru_utime.tv_sec + end.ru_stime.tv_sec
        + (end.ru_utime.tv_usec + end.ru_stime.tv_usec) / 1.0e6;
    return (endSeconds - startSeconds) * 1000.0;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    qint64 count = 10000;
    const int option = application.arguments().indexOf("--documents");
    if (option >= 0 && option + 1 < application.arguments().size())
        count = application.arguments().at(option + 1).toLongLong();
    QTemporaryDir temporary;
    const QString path = temporary.path() + "/content.sqlite3";
    purrfind::Database database;
    QString error;
    if (!database.open(path, false, &error) || !database.migrate(&error)) {
        std::cerr << error.toStdString(); return 1;
    }
    QElapsedTimer insertion; insertion.start();
    rusage cpuStart{}; getrusage(RUSAGE_SELF, &cpuStart);
    QVector<purrfind::FileRecord> metadata;
    metadata.reserve(1000);
    for (qint64 index = 0; index < count; ++index) {
        purrfind::FileRecord file;
        file.name = QString("report-%1.txt").arg(index);
        file.path = "/synthetic/content/" + file.name;
        file.parentPath = "/synthetic/content"; file.extension = "txt"; file.type = "text/plain";
        file.size = 2048; file.mtime = 1788470400; file.ctime = file.mtime;
        file.inode = index + 1; file.device = 1; file.root = "/synthetic"; file.scanGeneration = 1;
        metadata.append(std::move(file));
        if (metadata.size() == 1000) {
            if (!database.upsertBatch(metadata, &error)) { std::cerr << error.toStdString(); return 1; }
            metadata.clear();
        }
    }
    if (!database.upsertBatch(metadata, &error)) { std::cerr << error.toStdString(); return 1; }
    qint64 stored = 0;
    while (stored < count) {
        const auto pending = database.pendingContent({"txt"}, 500, &error);
        if (pending.isEmpty()) break;
        QVector<purrfind::ContentUpdate> updates;
        for (const auto &file : pending) {
            purrfind::ExtractResult extracted;
            extracted.state = purrfind::ContentState::Indexed;
            extracted.text = QString("Customer FIRENETWORK neutral network contract AS26615 OLT-C650 document %1").arg(file.id);
            updates.append({file, std::move(extracted), "synthetic"});
        }
        if (!database.storeContentBatch(updates, &error)) { std::cerr << error.toStdString(); return 1; }
        stored += updates.size();
    }
    const qint64 indexMs = insertion.elapsed();
    rusage cpuEnd{}; getrusage(RUSAGE_SELF, &cpuEnd);
    purrfind::SearchEngine engine(database);
    const QStringList queries{"FIRENETWORK", "\"neutral network\"", "AS26615", "OLT-C650", "content:contract"};
    QVector<double> times;
    QVector<double> filenameTimes;
    for (int round = 0; round < 20; ++round) for (const auto &query : queries) {
        QElapsedTimer timer; timer.start();
        const auto result = engine.search(query, 100, true, &error);
        times.append(timer.nsecsElapsed() / 1.0e6);
        if (result.isEmpty() || !error.isEmpty()) { std::cerr << error.toStdString(); return 1; }
    }
    for (int round = 0; round < 100; ++round) {
        QElapsedTimer timer; timer.start();
        const auto result = engine.search("name:report-9999", 100, true, &error);
        filenameTimes.append(timer.nsecsElapsed() / 1.0e6);
        Q_UNUSED(result);
    }
    std::sort(times.begin(), times.end());
    std::sort(filenameTimes.begin(), filenameTimes.end());
    double sum = 0; for (double value : times) sum += value;
    double filenameSum = 0; for (double value : filenameTimes) filenameSum += value;
    std::cout.setf(std::ios::fixed); std::cout.precision(2);
    std::cout << "documents: " << count << "\nindex_ms: " << indexMs
              << "\ndocuments_per_second: " << count * 1000.0 / indexMs
              << "\nquery_average_ms: " << sum / times.size()
              << "\nquery_p95_ms: " << times.at(static_cast<int>(times.size() * .95) - 1)
              << "\nfilename_query_average_ms: " << filenameSum / filenameTimes.size()
              << "\nfilename_query_p95_ms: " << filenameTimes.at(static_cast<int>(filenameTimes.size() * .95) - 1)
              << "\nindex_cpu_ms: " << cpuMilliseconds(cpuStart, cpuEnd)
              << "\nrss_bytes: " << rssBytes()
              << "\ndatabase_bytes: " << QFileInfo(path).size() << '\n';
    return 0;
}
