#include "core/FileSystem.h"
#include "preview/PreviewRegistry.h"

#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>
#include <sys/resource.h>

namespace {
QByteArray simplePdf()
{
    QList<QByteArray> objects;
    objects << "<< /Type /Catalog /Pages 2 0 R >>"
            << "<< /Type /Pages /Kids [3 0 R] /Count 1 >>"
            << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>";
    const QByteArray stream = "BT /F1 12 Tf 30 350 Td (PurrFind benchmark PDF) Tj ET";
    objects << ("<< /Length " + QByteArray::number(stream.size()) + " >>\nstream\n" + stream + "\nendstream")
            << "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";
    QByteArray pdf = "%PDF-1.4\n";
    QList<int> offsets{0};
    for (int index = 0; index < objects.size(); ++index) {
        offsets << pdf.size(); pdf += QByteArray::number(index + 1) + " 0 obj\n" + objects.at(index) + "\nendobj\n";
    }
    const int xref = pdf.size();
    pdf += "xref\n0 6\n0000000000 65535 f \n";
    for (int index = 1; index < offsets.size(); ++index)
        pdf += QByteArray::number(offsets.at(index)).rightJustified(10, '0') + " 00000 n \n";
    pdf += "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
    return pdf;
}

qint64 rssBytes()
{
    QFile status("/proc/self/status");
    if (!status.open(QIODevice::ReadOnly)) return -1;
    while (!status.atEnd()) {
        const QByteArray line = status.readLine();
        if (line.startsWith("VmRSS:")) return line.simplified().split(' ').value(1).toLongLong() * 1024;
    }
    rusage usage{};
    return getrusage(RUSAGE_SELF, &usage) == 0 ? static_cast<qint64>(usage.ru_maxrss) * 1024 : -1;
}

void run(const char *label, const QVector<purrfind::PreviewRequest> &requests,
         const purrfind::PreviewRegistry &registry, const purrfind::CancellationToken &token)
{
    QVector<double> timings;
    int hits = 0;
    for (const auto &request : requests) {
        QElapsedTimer timer; timer.start();
        const auto result = registry.generate(request, token);
        timings.append(timer.nsecsElapsed() / 1.0e6);
        if (result.cacheHit) ++hits;
        if (!result.error.isEmpty()) std::cerr << label << ": " << result.error.toStdString() << '\n';
    }
    std::sort(timings.begin(), timings.end());
    double total = 0; for (double value : timings) total += value;
    std::cout << label << "_average_ms=" << total / timings.size()
              << " " << label << "_p95_ms=" << timings.at(94)
              << " " << label << "_cache_hits=" << hits << '\n';
}
}

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    const QString imagePath = temporary.path() + "/image.png";
    const QString pdfPath = temporary.path() + "/document.pdf";
    const QString textPath = temporary.path() + "/document.txt";
    QImage image(800, 600, QImage::Format_RGB32); image.fill(QColor("#7950c4"));
    QFile pdf(pdfPath);
    QFile text(textPath);
    if (!image.save(imagePath, "PNG") || !pdf.open(QIODevice::WriteOnly)
        || pdf.write(simplePdf()) < 0 || !text.open(QIODevice::WriteOnly)
        || text.write(QByteArray(64 * 1024, 'A')) < 0) return 1;
    pdf.close(); text.close();
    auto cache = std::make_shared<purrfind::PreviewCache>(96, 256, temporary.path() + "/cache");
    purrfind::PreviewRegistry registry(cache);
    const purrfind::CancellationToken token;
    QVector<purrfind::PreviewRequest> images, pdfs, documents;
    auto prepare = [&temporary](const QString &path, qint64 id) {
        purrfind::PreviewRequest request;
        request.file = *purrfind::FileSystem::inspect(path, temporary.path(), 1);
        request.file.id = id;
        return request;
    };
    for (int index = 0; index < 100; ++index) {
        images.append(prepare(imagePath, 1000 + index));
        pdfs.append(prepare(pdfPath, 2000 + index));
        documents.append(prepare(textPath, 3000 + index));
    }
    std::cout.setf(std::ios::fixed); std::cout.precision(2);
    run("image_cold", images, registry, token); run("image_warm", images, registry, token);
    run("pdf_cold", pdfs, registry, token); run("pdf_warm", pdfs, registry, token);
    run("document_cold", documents, registry, token); run("document_warm", documents, registry, token);
    std::cout << "peak_rss_bytes=" << rssBytes() << '\n';
    return 0;
}
