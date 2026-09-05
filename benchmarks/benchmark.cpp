#include "core/Database.h"
#include "core/SearchEngine.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>

namespace {
qint64 rssBytes()
{
    QFile status("/proc/self/status");
    if (!status.open(QIODevice::ReadOnly)) return -1;
    while (true) {
        const QByteArray line = status.readLine();
        if (line.isEmpty()) break;
        if (line.startsWith("VmRSS:")) {
            const auto parts = line.simplified().split(' ');
            if (parts.size() >= 2) return parts.at(1).toLongLong() * 1024;
        }
    }
    return -1;
}

qint64 ioBytes(const QByteArray &key)
{
    QFile file("/proc/self/io");
    if (!file.open(QIODevice::ReadOnly)) return -1;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        if (line.startsWith(key + ':')) return line.mid(line.indexOf(':') + 1).trimmed().toLongLong();
    }
    return -1;
}

purrfind::FileRecord synthetic(qint64 index)
{
    const QString group = QString::number(index % 1000).rightJustified(3, '0');
    const bool special = index % 997 == 0;
    purrfind::FileRecord record;
    record.name = special ? QString("Contrato-FIRENETWORK-%1.pdf").arg(index)
                          : QString("document-%1-%2.txt").arg(group).arg(index);
    record.parentPath = QString("/synthetic/home/Documents/group-%1").arg(group);
    record.path = record.parentPath + '/' + record.name;
    record.extension = special ? "pdf" : "txt";
    record.type = special ? "application/pdf" : "text/plain";
    record.size = 1000 + (index % (50 * 1024 * 1024));
    record.mtime = 1788470400 - (index % (365 * 86400));
    record.ctime = record.mtime;
    record.inode = index + 1;
    record.device = 1;
    record.root = "/synthetic/home";
    record.scanGeneration = 1;
    return record;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    qint64 count = 100000;
    const auto arguments = application.arguments();
    const int position = arguments.indexOf("--records");
    if (position >= 0 && position + 1 < arguments.size()) count = arguments.at(position + 1).toLongLong();
    qint64 ocrPageCount = 0;
    const int ocrPosition = arguments.indexOf("--ocr-pages");
    if (ocrPosition >= 0 && ocrPosition + 1 < arguments.size())
        ocrPageCount = qBound<qint64>(0, arguments.at(ocrPosition + 1).toLongLong(), count);
    if (count < 1) return 2;

    QTemporaryDir temporary;
    const QString path = temporary.path() + "/benchmark.sqlite3";
    purrfind::Database database;
    QString error;
    if (!database.open(path, false, &error) || !database.migrate(&error)) {
        std::cerr << error.toStdString() << '\n'; return 1;
    }

    QElapsedTimer insertion;
    const qint64 writeBytesBefore = ioBytes("write_bytes");
    insertion.start();
    QVector<purrfind::FileRecord> batch;
    batch.reserve(2000);
    for (qint64 i = 0; i < count; ++i) {
        batch.append(synthetic(i));
        if (batch.size() == 2000) {
            if (!database.upsertBatch(batch, &error)) { std::cerr << error.toStdString(); return 1; }
            batch.clear();
        }
    }
    if (!database.upsertBatch(batch, &error)) { std::cerr << error.toStdString(); return 1; }
    const qint64 insertionMs = insertion.elapsed();
    sqlite3_wal_checkpoint_v2(database.handle(), nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr);
    const qint64 baselineBytes = QFileInfo(path).size();
    qint64 ocrInsertionMs = 0;
    if (ocrPageCount > 0) {
        QElapsedTimer ocrInsertion; ocrInsertion.start();
        sqlite3_exec(database.handle(), "BEGIN IMMEDIATE", nullptr, nullptr, nullptr);
        sqlite3_stmt *insert = nullptr;
        sqlite3_prepare_v2(database.handle(),
            "INSERT INTO ocr_pages(file_id,page_number,text,confidence) VALUES(?,1,?,?)", -1, &insert, nullptr);
        const QByteArray text = "OCR FIRENETWORK agreement prestação conexão AS26615 PPPoE searchable scanned page";
        for (qint64 id = 1; id <= ocrPageCount; ++id) {
            sqlite3_bind_int64(insert, 1, id);
            sqlite3_bind_text(insert, 2, text.constData(), text.size(), SQLITE_STATIC);
            sqlite3_bind_double(insert, 3, 82.0);
            if (sqlite3_step(insert) != SQLITE_DONE) { std::cerr << sqlite3_errmsg(database.handle()); return 1; }
            sqlite3_reset(insert); sqlite3_clear_bindings(insert);
        }
        sqlite3_finalize(insert);
        const QByteArray updateOcr = QString("UPDATE files SET ocr_state=4,ocr_processed_pages=1,ocr_total_pages=1 WHERE id<=%1")
            .arg(ocrPageCount).toUtf8();
        sqlite3_exec(database.handle(), updateOcr.constData(), nullptr, nullptr, nullptr);
        sqlite3_exec(database.handle(), "COMMIT", nullptr, nullptr, nullptr);
        sqlite3_wal_checkpoint_v2(database.handle(), nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr);
        ocrInsertionMs = ocrInsertion.elapsed();
    }
    const qint64 ocrGrowthBytes = QFileInfo(path).size() - baselineBytes;

    database.close();
    QElapsedTimer startup;
    startup.start();
    if (!database.open(path, true, &error)) { std::cerr << error.toStdString(); return 1; }
    const double startupMs = startup.nsecsElapsed() / 1.0e6;
    purrfind::SearchEngine search(database);
    const QStringList queries{"fi", "fire", "work", "document-042", "type:pdf", "group-731", "fire size:>1MB"};
    QVector<double> timings;
    QVector<double> scopedTimings;
    QVector<double> ocrTimings;
    QVector<double> typingTimings;
    constexpr int rounds = 30;
    for (int round = 0; round < rounds; ++round) {
        for (const auto &query : queries) {
            QElapsedTimer timer; timer.start();
            const auto results = search.search(query, 100, true, &error);
            timings.append(timer.nsecsElapsed() / 1.0e6);
            if (!error.isEmpty()) { std::cerr << error.toStdString(); return 1; }
            Q_UNUSED(results);
        }
    }
    const QStringList typingQueries{"p", "pu", "pur", "purr", "purrf", "purrfi", "purrfind"};
    for (int round = 0; round < rounds; ++round) {
        for (const auto &query : typingQueries) {
            QElapsedTimer timer; timer.start();
            search.search(query, 100, true, &error);
            typingTimings.append(timer.nsecsElapsed() / 1.0e6);
        }
    }
    if (ocrPageCount > 0) {
        for (int round = 0; round < rounds; ++round) {
            QElapsedTimer timer; timer.start();
            const auto results = search.search("source:ocr FIRENETWORK", 100, true, &error);
            ocrTimings.append(timer.nsecsElapsed() / 1.0e6);
            if (results.isEmpty() || !error.isEmpty()) { std::cerr << error.toStdString(); return 1; }
        }
        std::sort(ocrTimings.begin(), ocrTimings.end());
    }
    const QStringList scopedQueries{"name:fire", "name:document-042", "path:group-731"};
    for (int round = 0; round < rounds; ++round) {
        for (const auto &query : scopedQueries) {
            QElapsedTimer timer; timer.start();
            const auto results = search.search(query, 100, true, &error);
            scopedTimings.append(timer.nsecsElapsed() / 1.0e6);
            Q_UNUSED(results);
        }
    }
    std::sort(timings.begin(), timings.end());
    std::sort(scopedTimings.begin(), scopedTimings.end());
    std::sort(typingTimings.begin(), typingTimings.end());
    double sum = 0;
    for (double value : timings) sum += value;
    const double average = sum / timings.size();
    const double p95 = timings.at(static_cast<int>(timings.size() * 0.95) - 1);
    double scopedSum = 0; for (double value : scopedTimings) scopedSum += value;

    std::cout.setf(std::ios::fixed); std::cout.precision(2);
    std::cout << "PurrFind synthetic benchmark\n"
              << "records: " << count << '\n'
              << "insert_ms: " << insertionMs << '\n'
              << "insert_records_per_second: " << (count * 1000.0 / insertionMs) << '\n'
              << "startup_ms: " << startupMs << '\n'
              << "query_samples: " << timings.size() << '\n'
              << "query_average_ms: " << average << '\n'
              << "query_p50_ms: " << timings.at(timings.size() / 2) << '\n'
              << "query_p95_ms: " << p95 << '\n'
              << "query_p99_ms: " << timings.at(static_cast<int>(timings.size() * 0.99) - 1) << '\n'
              << "query_max_ms: " << timings.last() << '\n'
              << "typing_p50_ms: " << typingTimings.at(typingTimings.size() / 2) << '\n'
              << "typing_p95_ms: " << typingTimings.at(static_cast<int>(typingTimings.size() * 0.95) - 1) << '\n'
              << "typing_p99_ms: " << typingTimings.at(static_cast<int>(typingTimings.size() * 0.99) - 1) << '\n'
              << "scoped_filename_average_ms: " << scopedSum / scopedTimings.size() << '\n'
              << "scoped_filename_p95_ms: " << scopedTimings.at(static_cast<int>(scopedTimings.size() * 0.95) - 1) << '\n'
              << "ocr_pages: " << ocrPageCount << '\n'
              << "ocr_insert_ms: " << ocrInsertionMs << '\n'
              << "ocr_index_growth_bytes: " << ocrGrowthBytes << '\n';
    if (!ocrTimings.isEmpty()) {
        double ocrSum = 0; for (double value : ocrTimings) ocrSum += value;
        std::cout << "ocr_query_average_ms: " << ocrSum / ocrTimings.size() << '\n'
                  << "ocr_query_p95_ms: " << ocrTimings.at(static_cast<int>(ocrTimings.size() * 0.95) - 1) << '\n';
    }
    std::cout
              << "rss_bytes: " << rssBytes() << '\n'
              << "physical_write_bytes: " << (writeBytesBefore >= 0 ? ioBytes("write_bytes") - writeBytesBefore : -1) << '\n'
              << "database_bytes: " << QFileInfo(path).size() << '\n';
    return 0;
}
