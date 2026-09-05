#include "content/ExtractorRegistry.h"
#include "content/PlainTextExtractor.h"
#include "core/Database.h"
#include "core/FileSystem.h"
#include "core/QueryParser.h"
#include "core/SearchEngine.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#ifdef PURRFIND_WITH_OFFICE
#include <zip.h>
#endif
#include <iostream>

namespace {
int failures = 0;
void check(bool value, const QString &message)
{
    if (!value) { std::cerr << "FAIL: " << message.toStdString() << '\n'; ++failures; }
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray simplePdf(const QByteArray &text)
{
    QList<QByteArray> objects;
    objects << "<< /Type /Catalog /Pages 2 0 R >>"
            << "<< /Type /Pages /Kids [3 0 R] /Count 1 >>"
            << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>";
    const QByteArray stream = "BT /F1 12 Tf 72 720 Td (" + text + ") Tj ET";
    objects << ("<< /Length " + QByteArray::number(stream.size()) + " >>\nstream\n" + stream + "\nendstream")
            << "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";
    QByteArray pdf = "%PDF-1.4\n";
    QList<int> offsets{0};
    for (int i = 0; i < objects.size(); ++i) {
        offsets << pdf.size();
        pdf += QByteArray::number(i + 1) + " 0 obj\n" + objects.at(i) + "\nendobj\n";
    }
    const int xref = pdf.size();
    pdf += "xref\n0 " + QByteArray::number(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (int i = 1; i < offsets.size(); ++i)
        pdf += QByteArray::number(offsets.at(i)).rightJustified(10, '0') + " 00000 n \n";
    pdf += "trailer\n<< /Size " + QByteArray::number(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n"
        + QByteArray::number(xref) + "\n%%EOF\n";
    return pdf;
}

#ifdef PURRFIND_WITH_OFFICE
bool createZip(const QString &path, const QHash<QString, QByteArray> &entries)
{
    int error = 0;
    const QByteArray encoded = QFile::encodeName(path);
    zip_t *archive = zip_open(encoded.constData(), ZIP_CREATE | ZIP_TRUNCATE, &error);
    if (!archive) return false;
    for (auto iterator = entries.cbegin(); iterator != entries.cend(); ++iterator) {
        zip_source_t *source = zip_source_buffer(archive, iterator.value().constData(), iterator.value().size(), 0);
        if (!source || zip_file_add(archive, iterator.key().toUtf8().constData(), source,
                                    ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) < 0) {
            if (source) zip_source_free(source);
            zip_discard(archive); return false;
        }
    }
    return zip_close(archive) == 0;
}
#endif

purrfind::FileRecord inspect(const QString &path, const QString &root)
{
    auto result = purrfind::FileSystem::inspect(path, root, 1);
    return result ? *result : purrfind::FileRecord{};
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary directory");
    purrfind::ExtractorRegistry registry;
    const QStringList types = registry.availableExtensions();

    const QString legacyPath = temporary.path() + "/legacy-v1.sqlite3";
    sqlite3 *legacy = nullptr;
    check(sqlite3_open(QFile::encodeName(legacyPath).constData(), &legacy) == SQLITE_OK, "legacy database created");
    const char *legacySchema =
        "CREATE TABLE files(id INTEGER PRIMARY KEY,name TEXT NOT NULL,path TEXT NOT NULL UNIQUE,parent_path TEXT NOT NULL,"
        "extension TEXT NOT NULL DEFAULT '',type TEXT NOT NULL DEFAULT '',size INTEGER NOT NULL DEFAULT 0,mtime INTEGER NOT NULL DEFAULT 0,"
        "ctime INTEGER NOT NULL DEFAULT 0,inode INTEGER NOT NULL DEFAULT 0,device INTEGER NOT NULL DEFAULT 0,flags INTEGER NOT NULL DEFAULT 0,"
        "is_dir INTEGER NOT NULL DEFAULT 0,is_symlink INTEGER NOT NULL DEFAULT 0,root TEXT NOT NULL,scan_generation INTEGER NOT NULL DEFAULT 0,updated_at INTEGER NOT NULL DEFAULT 0);"
        "INSERT INTO files(name,path,parent_path,root) VALUES('legacy.txt','/legacy.txt','/','/'); PRAGMA user_version=1;";
    check(sqlite3_exec(legacy, legacySchema, nullptr, nullptr, nullptr) == SQLITE_OK, "legacy schema populated");
    sqlite3_close(legacy);
    purrfind::Database migrated;
    QString migrationError;
    check(migrated.open(legacyPath, false, &migrationError) && migrated.migrate(&migrationError)
          && migrated.schemaVersion() == 6 && migrated.fileCount() == 1, "automatic v1 through v6 migration preserves files");
    migrated.close();

    const QString txt = temporary.path() + "/plain.txt";
    const QString md = temporary.path() + "/notes.markdown";
    check(writeFile(txt, "\xef\xbb\xbf" "FIRENETWORK conex\xc3\xa3o OLT-C650 192.168.1.1 ZTEG-d81f94be"), "TXT fixture");
    check(writeFile(md, "# Neutral network\nMarkdown_F2"), "Markdown fixture");
    for (const QString &path : {txt, md}) {
        const auto file = inspect(path, temporary.path());
        const auto *extractor = registry.extractorFor(file, types);
        check(extractor != nullptr, "text extractor selected");
        if (extractor) check(extractor->extract(file, {}, purrfind::CancellationToken()).state == purrfind::ContentState::Indexed,
                             "text extracted");
    }
    const QString empty = temporary.path() + "/empty.txt";
    const QString binary = temporary.path() + "/binary.txt";
    writeFile(empty, {}); writeFile(binary, QByteArray("a\0b", 3));
    purrfind::PlainTextExtractor plain;
    check(plain.extract(inspect(empty, temporary.path()), {}, purrfind::CancellationToken()).state == purrfind::ContentState::NoText,
          "empty text is NO_TEXT");
    check(plain.extract(inspect(binary, temporary.path()), {}, purrfind::CancellationToken()).state == purrfind::ContentState::Unsupported,
          "binary text is rejected");

#ifdef PURRFIND_WITH_PDF
    const QString pdf = temporary.path() + "/document.pdf";
    check(writeFile(pdf, simplePdf("PDF_FIRENETWORK searchable text")), "PDF fixture");
    auto pdfFile = inspect(pdf, temporary.path());
    const auto *pdfExtractor = registry.extractorFor(pdfFile, types);
    check(pdfExtractor && pdfExtractor->extract(pdfFile, {}, purrfind::CancellationToken()).text.contains("PDF_FIRENETWORK"),
          "PDF text extraction");
    const QString brokenPdf = temporary.path() + "/broken.pdf";
    writeFile(brokenPdf, "%PDF-1.4 truncated");
    check(pdfExtractor && pdfExtractor->extract(inspect(brokenPdf, temporary.path()), {}, purrfind::CancellationToken()).state == purrfind::ContentState::Failed,
          "truncated PDF fails safely");
    const QString protectedPdf = temporary.path() + "/protected.pdf";
    const QByteArray protectedFixture = QByteArray::fromBase64(
        "JVBERi0xLjcKJb/3ov4KMSAwIG9iago8PCAvRXh0ZW5zaW9ucyA8PCAvQURCRSA8PCAvQmFzZVZlcnNpb24gLzEuNyAvRXh0ZW5zaW9uTGV2ZWwgOCA+PiA+PiAvUGFnZXMgMiAwIFIgL1R5cGUgL0NhdGFsb2cgPj4KZW5kb2JqCjIgMCBvYmoKPDwgL0NvdW50IDAgL0tpZHMgWyBdIC9UeXBlIC9QYWdlcyA+PgplbmRvYmoKMyAwIG9iago8PCAvQ0YgPDwgL1N0ZENGIDw8IC9BdXRoRXZlbnQgL0RvY09wZW4gL0NGTSAvQUVTVjMgL0xlbmd0aCAzMiA+PiA+PiAvRmlsdGVyIC9TdGFuZGFyZCAvTGVuZ3RoIDI1NiAvTyA8MTBlMWEzZDZiODNjNTA1YTYzYjhmNWJkMDEwM2UyYWFlOGFlZjNkZDczY2Y2N2Q5NzFhMGY4Y2QyMTM1YjhhZjhlNTU2NTA0Y2FhYjE2YzAyMWE4MzEyMTU4MjMxOTE0PiAvT0UgPDJlZjZjZjVlNDZiZmY3ZWViNzFlMDc2MzMzNmU4NjFmNGY3MTM3OWVkMDE5NDU0ZTViMTFkNmU1YzIyYTFjYmU+IC9QIC00IC9QZXJtcyA8YTQ2N2QyNTNjZGYxOTg1YThlYjdiMGI2ZWM5ZjZhNTM+IC9SIDYgL1N0bUYgL1N0ZENGIC9TdHJGIC9TdGRDRiAvVSA8NjZkNTg4M2FlZGIxYjVhYjhiNmE4NjQ0NmRlNDZiNDg3YjJkMDk3MDkyMTk0ZDI3ZTAwYmQ0MGUzMDBhOTk1NWY3YzY1ZGU3OWJlZTY3OTVjYTlkMWVmMjQ2ZGQ5NjNjPiAvVUUgPDZmZGEzYWQ0NzQ3OTNhMTZlZjk3NmNmNDU5YmJiOGZhZjU2OGM1ZDI5MDYzZTRjOWUxMDg2YTliMjY3M2NlZjQ+IC9WIDUgPj4KZW5kb2JqCnhyZWYKMCA0CjAwMDAwMDAwMDAgNjU1MzUgZiAKMDAwMDAwMDAxNSAwMDAwMCBuIAowMDAwMDAwMTMwIDAwMDAwIG4gCjAwMDAwMDAxODMgMDAwMDAgbiAKdHJhaWxlciA8PCAvUm9vdCAxIDAgUiAvU2l6ZSA0IC9JRCBbPDM4OTgzZWUwMTQyNWQ3ODhhYTk3YzZhMTc1NTk2ZGRlPjwzODk4M2VlMDE0MjVkNzg4YWE5N2M2YTE3NTU5NmRkZT5dIC9FbmNyeXB0IDMgMCBSID4+CnN0YXJ0eHJlZgo3MzAKJSVFT0YK");
    check(writeFile(protectedPdf, protectedFixture), "password-protected PDF fixture");
    check(pdfExtractor && pdfExtractor->extract(inspect(protectedPdf, temporary.path()), {}, purrfind::CancellationToken()).state == purrfind::ContentState::Encrypted,
          "password-protected PDF is classified as ENCRYPTED");
#endif

#ifdef PURRFIND_WITH_OFFICE
    struct Fixture { QString extension; QHash<QString, QByteArray> entries; QString term; };
    const QList<Fixture> fixtures{
        {"docx", {{"word/document.xml", "<document><p><t>DOCX_FIRENETWORK</t></p></document>"}, {"docProps/core.xml", "<core><title>Contract</title><creator>Alice</creator></core>"}}, "DOCX_FIRENETWORK"},
        {"xlsx", {{"xl/sharedStrings.xml", "<sst><si><t>XLSX_FIRENETWORK</t></si></sst>"}, {"xl/workbook.xml", "<workbook><sheet name='Clients'/></workbook>"}, {"xl/worksheets/sheet1.xml", "<worksheet><row><c><v>42</v></c></row></worksheet>"}}, "XLSX_FIRENETWORK"},
        {"pptx", {{"ppt/slides/slide1.xml", "<slide><p><t>PPTX_FIRENETWORK</t></p></slide>"}, {"ppt/notesSlides/notesSlide1.xml", "<notes><t>speaker note</t></notes>"}}, "PPTX_FIRENETWORK"},
        {"odt", {{"content.xml", "<document><p>ODT_FIRENETWORK</p></document>"}, {"meta.xml", "<meta><title>ODT title</title></meta>"}}, "ODT_FIRENETWORK"},
        {"ods", {{"content.xml", "<document><table><p>ODS_FIRENETWORK</p></table></document>"}}, "ODS_FIRENETWORK"},
        {"odp", {{"content.xml", "<document><slide><p>ODP_FIRENETWORK</p></slide></document>"}}, "ODP_FIRENETWORK"},
    };
    for (const auto &fixture : fixtures) {
        const QString path = temporary.path() + "/fixture." + fixture.extension;
        check(createZip(path, fixture.entries), fixture.extension + " fixture created");
        const auto file = inspect(path, temporary.path());
        const auto *extractor = registry.extractorFor(file, types);
        const auto output = extractor ? extractor->extract(file, {}, purrfind::CancellationToken()) : purrfind::ExtractResult{};
        check(output.state == purrfind::ContentState::Indexed && output.text.contains(fixture.term), fixture.extension + " extraction");
    }
    const QString corrupt = temporary.path() + "/corrupt.docx";
    writeFile(corrupt, "not a zip");
    const auto *office = registry.extractorFor(inspect(corrupt, temporary.path()), types);
    check(office && office->extract(inspect(corrupt, temporary.path()), {}, purrfind::CancellationToken()).state == purrfind::ContentState::Failed,
          "corrupt ZIP fails safely");
    const QString invalidXml = temporary.path() + "/invalid.odt";
    createZip(invalidXml, {{"content.xml", "<broken>"}});
    const auto *odf = registry.extractorFor(inspect(invalidXml, temporary.path()), types);
    check(odf && odf->extract(inspect(invalidXml, temporary.path()), {}, purrfind::CancellationToken()).state == purrfind::ContentState::Failed,
          "invalid XML fails safely");
    const QString xxe = temporary.path() + "/xxe.odt";
    createZip(xxe, {{"content.xml", "<!DOCTYPE x [<!ENTITY leak SYSTEM 'file:///etc/passwd'>]><x>&leak;<p>XXE_SAFE</p></x>"}});
    const auto xxeResult = odf->extract(inspect(xxe, temporary.path()), {}, purrfind::CancellationToken());
    check(xxeResult.text.contains("XXE_SAFE") && !xxeResult.text.contains("root:"), "external entity not expanded");
    const QString traversal = temporary.path() + "/traversal.odt";
    createZip(traversal, {{"../../outside", "bad"}, {"content.xml", "<p>safe</p>"}});
    check(odf->extract(inspect(traversal, temporary.path()), {}, purrfind::CancellationToken()).state == purrfind::ContentState::Failed,
          "archive traversal rejected");
    const QString bomb = temporary.path() + "/bomb.odt";
    createZip(bomb, {{"content.xml", QByteArray(1024 * 1024, 'A')}});
    check(odf->extract(inspect(bomb, temporary.path()), {}, purrfind::CancellationToken()).state == purrfind::ContentState::Failed,
          "compression-ratio bomb rejected");
    const QString hugeEntry = temporary.path() + "/huge-entry.odt";
    createZip(hugeEntry, {{"content.xml", QByteArray(128, 'x')}});
    purrfind::ExtractionLimits tinyEntryLimit;
    tinyEntryLimit.maximumArchiveEntryBytes = 64;
    check(odf->extract(inspect(hugeEntry, temporary.path()), tinyEntryLimit, purrfind::CancellationToken()).state == purrfind::ContentState::Failed,
          "oversized archive entry rejected");
#endif

    const QString databasePath = temporary.path() + "/index.sqlite3";
    purrfind::Database database;
    QString error;
    check(database.open(databasePath, false, &error) && database.migrate(&error), "content database migration");
    auto document = inspect(txt, temporary.path());
    check(database.upsert(document, &error), "content file metadata inserted");
    auto byName = database.search(purrfind::QueryParser::parse("plain.txt"), 1, true, &error);
    check(!byName.isEmpty(), "metadata available before content");
    document.id = byName.first().file.id;
    auto extraction = plain.extract(document, {}, purrfind::CancellationToken());
    extraction.extractionMs = 12;
    check(database.storeContent(document, extraction, plain.id(), &error), "content stored");
    const auto metrics = database.contentExtractorMetrics(&error);
    check(metrics.size() == 1 && metrics.first().extractor == plain.id()
          && metrics.first().documents == 1 && metrics.first().p95Milliseconds == 12
          && metrics.first().bytesProcessed == extraction.bytesRead,
          "per-extractor development metrics");
    purrfind::SearchEngine search(database);
    auto found = search.search("content:FIRENETWORK", 10, true, &error);
    check(found.size() == 1 && found.first().matchOrigin == "content", "content scope search");
    check(!found.isEmpty() && found.first().snippet.contains("[[PFH]]"), "content snippet markers");
    check(!search.search("conexao", 10, true, &error).isEmpty(), "diacritic-insensitive search");
    check(!search.search("\"OLT-C650 192.168.1.1\"", 10, true, &error).isEmpty(), "technical phrase search");
    check(!search.search("content:d81f94be", 10, true, &error).isEmpty(),
          "technical identifier component after hyphen is searchable");

    auto nameWinner = document;
    nameWinner.id = -1; nameWinner.name = "FIRENETWORK-overview.txt";
    nameWinner.path = temporary.path() + "/FIRENETWORK-overview.txt"; nameWinner.inode = 9876;
    check(database.upsert(nameWinner, &error), "name winner inserted");
    found = search.search("FIRENETWORK", 10, true, &error);
    check(!found.isEmpty() && found.first().file.name == nameWinner.name, "filename ranks above content");

    document.size += 1; document.mtime += 1; document.inode += 1;
    check(database.upsert(document, &error), "modified metadata invalidates content");
    check(search.search("content:FIRENETWORK", 10, true, &error).isEmpty(), "stale content removed");
    purrfind::ExtractResult beta; beta.state = purrfind::ContentState::Indexed; beta.text = "beta replacement";
    check(database.storeContent(document, beta, "test", &error), "modified content reindexed");
    check(!search.search("content:beta", 10, true, &error).isEmpty(), "replacement content searchable");
    const QString oldPath = document.path;
    document.path = temporary.path() + "/renamed.txt"; document.name = "renamed.txt";
    check(database.movePathPreservingContent(oldPath, document, false, &error), "rename preserves content row");
    check(!search.search("content:beta", 10, true, &error).isEmpty(), "content survives rename");
    check(database.removePath(document.path, false, &error), "content document deleted");
    check(search.search("content:beta", 10, true, &error).isEmpty(), "content cascade deletion");

    const auto parsed = purrfind::QueryParser::parse("content:\"rede neutra\" type:pdf modified:30d");
    check(parsed.scope == purrfind::SearchScope::Content && parsed.phrase && parsed.extension == "pdf"
          && parsed.modified == purrfind::ModifiedFilter::Days30, "scoped phrase parser");

    auto richPdf = document;
    richPdf.id = -1; richPdf.path = temporary.path() + "/PurrFind-roadmap.pdf";
    richPdf.name = "PurrFind-roadmap.pdf"; richPdf.parentPath = temporary.path(); richPdf.extension = "pdf";
    richPdf.type = "application/pdf"; richPdf.size = 500; richPdf.mtime += 10; richPdf.inode = 20001;
    check(database.upsert(richPdf, &error), "rich PDF metadata row");
    auto richPdfRows = database.search(purrfind::QueryParser::parse("name:PurrFind-roadmap"), 1, true, &error);
    check(!richPdfRows.isEmpty(), "rich PDF row lookup");
    richPdf.id = richPdfRows.first().file.id;
    purrfind::ExtractResult richExtraction;
    richExtraction.state = purrfind::ContentState::Indexed;
    richExtraction.text = "first native page with sufficient searchable text PageEight target on another native page";
    richExtraction.pages = {"first native page with sufficient searchable text",
                            "PageEight target on another native page with sufficient text"};
    richExtraction.pageCount = 2; richExtraction.author = "João Example";
    check(database.storeContent(richPdf, richExtraction, "pdf-test", &error), "page-aware PDF content stored");
    auto pendingPdfOcr = database.pendingOcr(true, false, false, 100, &error);
    bool nativePdfQueued = false;
    for (const auto &candidate : pendingPdfOcr) if (candidate.id == richPdf.id) nativePdfQueued = true;
    check(!nativePdfQueued, "PDF with sufficient native text does not enter OCR queue");
    auto pageMatch = search.search("content:PageEight", 10, true, &error);
    check(pageMatch.size() == 1 && pageMatch.first().matchPage == 2, "PDF content match resolves page");
    check(search.search("pages:>1 author:João type:pdf", 10, true, &error).size() == 1,
          "author and page metadata filters");

    auto hybridCandidate = richPdf;
    hybridCandidate.id = -1; hybridCandidate.path = temporary.path() + "/hybrid-candidate.pdf";
    hybridCandidate.name = "hybrid-candidate.pdf"; hybridCandidate.inode = 20006;
    check(database.upsert(hybridCandidate, &error), "hybrid PDF row inserted");
    auto hybridRows = database.search(purrfind::QueryParser::parse("name:hybrid-candidate"), 1, true, &error);
    hybridCandidate.id = hybridRows.first().file.id;
    purrfind::ExtractResult hybridExtraction;
    hybridExtraction.state = purrfind::ContentState::Indexed;
    hybridExtraction.text = "native page with enough useful searchable document text";
    hybridExtraction.pages = {hybridExtraction.text, ""}; hybridExtraction.pageCount = 2;
    check(database.storeContent(hybridCandidate, hybridExtraction, "pdf-test", &error), "hybrid native content stored");
    pendingPdfOcr = database.pendingOcr(true, false, false, 100, &error);
    bool hybridQueued = false;
    for (const auto &candidate : pendingPdfOcr) if (candidate.id == hybridCandidate.id) hybridQueued = true;
    check(hybridQueued, "hybrid PDF queues only because a page lacks useful native text");

    auto image = richPdf;
    image.id = -1; image.path = temporary.path() + "/camera.jpg"; image.name = "camera.jpg";
    image.extension = "jpg"; image.type = "image/jpeg"; image.size = 100; image.inode = 20002;
    check(database.upsert(image, &error), "image metadata row");
    auto imageRows = database.search(purrfind::QueryParser::parse("name:camera"), 1, true, &error);
    image.id = imageRows.first().file.id;
    purrfind::RichMetadata richImage;
    richImage.cameraMake = "Canon"; richImage.cameraModel = "PurrCam";
    richImage.width = 4096; richImage.height = 2160; richImage.orientation = 1;
    check(database.storeRichMetadata(image, richImage, &error), "rich image metadata stored");
    auto cameraMatch = search.search("camera:canon width:>3000 height:>2000 type:image", 10, true, &error);
    check(cameraMatch.size() == 1 && cameraMatch.first().cameraModel == "PurrCam",
          "camera and dimension metadata filters");

    auto rankingFolder = richPdf;
    rankingFolder.id = -1; rankingFolder.path = temporary.path() + "/PurrFind";
    rankingFolder.name = "PurrFind"; rankingFolder.extension.clear(); rankingFolder.type = "folder";
    rankingFolder.directory = true; rankingFolder.size = 0; rankingFolder.inode = 20003;
    auto rankingProject = richPdf;
    rankingProject.id = -1; rankingProject.path = temporary.path() + "/project-purrfind.md";
    rankingProject.name = "project-purrfind.md"; rankingProject.extension = "md";
    rankingProject.type = "text/markdown"; rankingProject.directory = false; rankingProject.inode = 20004;
    auto contentOnly = richPdf;
    contentOnly.id = -1; contentOnly.path = temporary.path() + "/random-document.pdf";
    contentOnly.name = "random-document.pdf"; contentOnly.inode = 20005;
    check(database.upsertBatch({rankingFolder, rankingProject, contentOnly}, &error), "ranking fixtures inserted");
    auto contentOnlyRows = database.search(purrfind::QueryParser::parse("name:random-document"), 1, true, &error);
    contentOnly.id = contentOnlyRows.first().file.id;
    purrfind::ExtractResult weakMention; weakMention.state = purrfind::ContentState::Indexed;
    weakMention.text = "a weak purrfind mention";
    check(database.storeContent(contentOnly, weakMention, "test", &error), "weak content ranking fixture");
    auto ranked = search.search("purrfind", 10, true, &error);
    if (ranked.size() < 4 || ranked.at(0).file.name != "PurrFind-roadmap.pdf"
        || ranked.at(1).file.name != "PurrFind" || ranked.at(2).file.name != "project-purrfind.md") {
        std::cerr << "Ranking order:";
        for (const auto &entry : ranked) std::cerr << ' ' << entry.file.name.toStdString() << '(' << entry.score << ')';
        std::cerr << '\n';
    }
    auto positionOf = [](const QVector<purrfind::SearchResult> &items, const QString &name) {
        for (int index = 0; index < items.size(); ++index) if (items.at(index).file.name == name) return index;
        return -1;
    };
    check(ranked.size() >= 4 && ranked.at(0).file.name == "PurrFind-roadmap.pdf"
          && ranked.at(1).file.name == "PurrFind" && ranked.at(2).file.name == "project-purrfind.md"
          && positionOf(ranked, "random-document.pdf") > 2, "deterministic ranking order");
    for (int index = 0; index < 20; ++index) database.recordOpen(contentOnly.id, &error);
    ranked = search.search("purrfind", 10, true, &error);
    check(ranked.first().file.name == "PurrFind-roadmap.pdf" && positionOf(ranked, "random-document.pdf") > 2,
          "bounded local usage never overtakes stronger matches");
    check(database.clearUsageHistory(&error), "local usage history can be cleared");

    auto ocrDocument = richPdf;
    ocrDocument.id = -1; ocrDocument.path = temporary.path() + "/scan-result.pdf";
    ocrDocument.name = "scan-result.pdf"; ocrDocument.inode = 30001; ocrDocument.size = 700;
    check(database.upsert(ocrDocument, &error), "OCR file metadata inserted immediately");
    auto ocrRows = database.search(purrfind::QueryParser::parse("name:scan-result"), 1, true, &error);
    check(!ocrRows.isEmpty(), "OCR candidate searchable by name before OCR");
    ocrDocument.id = ocrRows.first().file.id;
    check(database.beginOcr(ocrDocument, "por+eng", &error), "OCR processing state begins");
    purrfind::OcrPageResult ocrPage;
    ocrPage.pageNumber = 7; ocrPage.totalPages = 12;
    ocrPage.text = "Contrato OCR_ONLY_AS26615 FIRENETWORK prestação conexão";
    ocrPage.confidence = 42.0;
    check(database.storeOcrPage(ocrDocument, ocrPage, "por+eng", &error), "OCR page committed atomically");
    auto ocrFound = search.search("source:ocr OCR_ONLY_AS26615", 10, true, &error);
    check(ocrFound.size() == 1 && ocrFound.first().matchOrigin == "ocr"
          && ocrFound.first().matchPage == 7 && ocrFound.first().snippet.contains("[[PFH]]"),
          "OCR FTS result includes source, page and snippet");
    check(database.finishOcr(ocrDocument, purrfind::OcrState::Indexed, 12, {}, &error), "OCR document completes");

    auto strongerName = ocrDocument;
    strongerName.id = -1; strongerName.path = temporary.path() + "/OCR_ONLY_AS26615-guide.pdf";
    strongerName.name = "OCR_ONLY_AS26615-guide.pdf"; strongerName.inode = 30002;
    check(database.upsert(strongerName, &error), "strong OCR ranking fixture inserted");
    auto ocrRanked = search.search("OCR_ONLY_AS26615", 10, true, &error);
    check(!ocrRanked.isEmpty() && ocrRanked.first().file.name == strongerName.name,
          "low-confidence OCR never outranks a filename match");

    const QString oldOcrPath = ocrDocument.path;
    ocrDocument.path = temporary.path() + "/renamed-scan.pdf"; ocrDocument.name = "renamed-scan.pdf";
    check(database.movePathPreservingContent(oldOcrPath, ocrDocument, false, &error), "rename preserves OCR by identity");
    check(!search.search("source:ocr OCR_ONLY_AS26615", 10, true, &error).isEmpty(), "OCR survives rename");
    const auto staleOcrRevision = ocrDocument;
    ocrDocument.size += 1; ocrDocument.mtime += 1; ocrDocument.inode += 1;
    check(database.upsert(ocrDocument, &error), "changed OCR file metadata updated");
    check(search.search("source:ocr OCR_ONLY_AS26615", 10, true, &error).isEmpty(), "file change invalidates stale OCR");
    error.clear();
    check(!database.storeOcrPage(staleOcrRevision, ocrPage, "por+eng", &error)
          && error.contains("stale"), "stale in-flight OCR page cannot be committed");
    check(database.setOcrState(ocrDocument.id, purrfind::OcrState::Processing, {}, &error)
          && database.resetInterruptedOcr(&error)
          && database.ocrStateCounts(&error).value(purrfind::OcrState::Pending) > 0,
          "interrupted OCR queue persists and safely returns to pending");
    check(database.failOcr(ocrDocument, "problem", &error)
          && database.failOcr(ocrDocument, "problem", &error)
          && database.ocrStateCounts(&error).value(purrfind::OcrState::Failed) > 0,
          "OCR retries are bounded before FAILED state");
    check(database.removePath(ocrDocument.path, false, &error), "OCR file deletion");
    check(database.ocrPageCount(&error) == 0, "OCR rows cascade on delete or invalidation");

    std::cout << (failures ? "Content tests failed" : "All content tests passed") << '\n';
    return failures ? 1 : 0;
}
