#include "core/Database.h"
#include "ocr/TesseractOcrEngine.h"

#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>
#include <sys/resource.h>
#include <sys/times.h>
#include <unistd.h>

namespace {
qint64 ioBytes()
{
    QFile file("/proc/self/io");
    if (!file.open(QIODevice::ReadOnly)) return -1;
    qint64 total = 0;
    for (const auto &line : file.readAll().split('\n')) {
        if (line.startsWith("read_bytes:") || line.startsWith("write_bytes:"))
            total += line.mid(line.indexOf(':') + 1).trimmed().toLongLong();
    }
    return total;
}

qint64 rssBytes()
{
    struct rusage usage {};
    return getrusage(RUSAGE_SELF, &usage) == 0 ? usage.ru_maxrss * 1024LL : -1;
}

QImage fixture(int dpi)
{
    const double scale = dpi / 200.0;
    QImage image(QSize(qRound(1500 * scale), qRound(360 * scale)), QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setPen(Qt::black);
    painter.setFont(QFont("DejaVu Sans", qRound(48 * scale), QFont::Bold));
    painter.drawText(image.rect().adjusted(24, 16, -24, -16), Qt::AlignCenter | Qt::TextWordWrap,
                     "FIRENETWORK prestação conexão agreement service AS26615 PPPoE 192.168.1.1");
    return image;
}
}

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"pages", "Number of OCR pages", "count", "100"});
    parser.addOption({"languages", "Languages joined by +", "languages", "por+eng"});
    parser.addOption({"dpi", "Controlled fixture DPI", "dpi", "200"});
    parser.process(application);
    const int pages = qBound(1, parser.value("pages").toInt(), 10000);
    const int dpi = qBound(150, parser.value("dpi").toInt(), 300);
    const QStringList languages = parser.value("languages").split('+', Qt::SkipEmptyParts);
    qputenv("OMP_THREAD_LIMIT", "1");

    QTemporaryDir temporary;
    purrfind::Database database;
    QString error;
    if (!temporary.isValid() || !database.open(temporary.path() + "/ocr.sqlite3", false, &error)
        || !database.migrate(&error)) {
        std::cerr << error.toStdString() << '\n'; return 1;
    }
    QVector<purrfind::FileRecord> records;
    records.reserve(pages);
    for (int index = 0; index < pages; ++index) {
        purrfind::FileRecord file;
        file.name = QString("scan-%1.png").arg(index); file.path = "/benchmark/" + file.name;
        file.parentPath = "/benchmark"; file.extension = "png"; file.type = "image/png";
        file.size = 100000; file.mtime = 2000000000 + index; file.ctime = file.mtime;
        file.inode = index + 1; file.device = 1; file.root = "/benchmark"; file.scanGeneration = 1;
        records.append(file);
    }
    if (!database.upsertBatch(records, &error)) { std::cerr << error.toStdString(); return 1; }
    sqlite3_wal_checkpoint_v2(database.handle(), nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr);
    const qint64 baselineBytes = database.databaseSize();
    sqlite3_stmt *ids = nullptr;
    if (sqlite3_prepare_v2(database.handle(), "SELECT id FROM files ORDER BY id", -1, &ids, nullptr) != SQLITE_OK)
        return 1;
    int recordIndex = 0;
    while (recordIndex < records.size() && sqlite3_step(ids) == SQLITE_ROW)
        records[recordIndex++].id = sqlite3_column_int64(ids, 0);
    sqlite3_finalize(ids);
    if (recordIndex != records.size()) return 1;
    const QImage image = fixture(dpi);
    purrfind::TesseractOcrEngine engine;
    QVector<qint64> samples;
    samples.reserve(pages);
    int failures = 0, correct = 0;
    struct tms cpuStart {}, cpuEnd {};
    times(&cpuStart);
    const qint64 ioStart = ioBytes();
    QElapsedTimer total; total.start();
    for (int index = 0; index < pages; ++index) {
        QElapsedTimer pageTimer; pageTimer.start();
        const auto recognized = engine.recognize(image, languages, purrfind::CancellationToken());
        if (!recognized.error.isEmpty()) { samples.append(pageTimer.elapsed()); ++failures; continue; }
        if (recognized.text.contains("FIRENETWORK", Qt::CaseInsensitive)
            && recognized.text.contains("AS26615", Qt::CaseInsensitive)) ++correct;
        auto file = records.at(index);
        database.beginOcr(file, languages.join('+'), nullptr);
        purrfind::OcrPageResult page{1, 1, recognized.text, recognized.confidence, false};
        database.storeOcrPage(file, page, languages.join('+'), nullptr);
        database.finishOcr(file, recognized.text.isEmpty() ? purrfind::OcrState::NoText : purrfind::OcrState::Indexed,
                           1, {}, nullptr);
        samples.append(pageTimer.elapsed());
    }
    const qint64 elapsed = total.elapsed();
    times(&cpuEnd);
    const long ticks = sysconf(_SC_CLK_TCK);
    const double cpuSeconds = static_cast<double>((cpuEnd.tms_utime + cpuEnd.tms_stime)
        - (cpuStart.tms_utime + cpuStart.tms_stime)) / ticks;
    std::sort(samples.begin(), samples.end());
    const qint64 p95 = samples.at(qMin(samples.size() - 1, (samples.size() * 95 + 99) / 100 - 1));
    database.optimize(nullptr);
    sqlite3_wal_checkpoint_v2(database.handle(), nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr);
    std::cout << "pages=" << pages
              << "\nlanguages=" << languages.join('+').toStdString()
              << "\ndpi=" << dpi
              << "\npages_per_minute=" << (elapsed ? pages * 60000.0 / elapsed : 0.0)
              << "\naverage_ms_per_page=" << (pages ? static_cast<double>(elapsed) / pages : 0.0)
              << "\np95_ms_per_page=" << p95
              << "\naccuracy_percent=" << (pages ? correct * 100.0 / pages : 0.0)
              << "\nerror_percent=" << (pages ? failures * 100.0 / pages : 0.0)
              << "\ncpu_seconds=" << cpuSeconds
              << "\ncpu_percent=" << (elapsed ? cpuSeconds * 100000.0 / elapsed : 0.0)
              << "\npeak_rss_bytes=" << rssBytes()
              << "\nio_bytes=" << (ioStart >= 0 ? ioBytes() - ioStart : -1)
              << "\nindex_growth_bytes=" << database.databaseSize() - baselineBytes << '\n';
    return failures ? 1 : 0;
}
