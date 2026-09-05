#include "core/Database.h"

#include "core/Logging.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include <cmath>

namespace purrfind {
namespace {

class Statement {
public:
    Statement(sqlite3 *db, const QByteArray &sql)
    {
        if (sqlite3_prepare_v2(db, sql.constData(), sql.size(), &statement_, nullptr) != SQLITE_OK)
            statement_ = nullptr;
    }
    ~Statement() { if (statement_) sqlite3_finalize(statement_); }
    sqlite3_stmt *get() const { return statement_; }
    bool valid() const { return statement_ != nullptr; }
private:
    sqlite3_stmt *statement_{nullptr};
};

void bindText(sqlite3_stmt *statement, int index, const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    sqlite3_bind_text(statement, index, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
}

QString columnText(sqlite3_stmt *statement, int column)
{
    const auto *text = sqlite3_column_text(statement, column);
    return text ? QString::fromUtf8(reinterpret_cast<const char *>(text)) : QString();
}

FileRecord recordFrom(sqlite3_stmt *statement)
{
    FileRecord value;
    value.id = sqlite3_column_int64(statement, 0);
    value.name = columnText(statement, 1);
    value.path = columnText(statement, 2);
    value.parentPath = columnText(statement, 3);
    value.extension = columnText(statement, 4);
    value.type = columnText(statement, 5);
    value.size = sqlite3_column_int64(statement, 6);
    value.mtime = sqlite3_column_int64(statement, 7);
    value.ctime = sqlite3_column_int64(statement, 8);
    value.inode = static_cast<quint64>(sqlite3_column_int64(statement, 9));
    value.device = static_cast<quint64>(sqlite3_column_int64(statement, 10));
    const auto flags = static_cast<uint32_t>(sqlite3_column_int(statement, 11));
    value.hidden = flags & Hidden;
    value.directory = sqlite3_column_int(statement, 12) != 0;
    value.symlink = sqlite3_column_int(statement, 13) != 0;
    value.root = columnText(statement, 14);
    value.scanGeneration = sqlite3_column_int64(statement, 15);
    return value;
}

QString escapedLike(QString value)
{
    value.replace("\\", "\\\\");
    value.replace("%", "\\%");
    value.replace("_", "\\_");
    return value;
}

bool hasUsageHistory(sqlite3 *database)
{
    Statement statement(database, "SELECT 1 FROM usage_history LIMIT 1");
    return statement.valid() && sqlite3_step(statement.get()) == SQLITE_ROW;
}

bool sufficientNativeText(const QString &text)
{
    const QString simplified = text.simplified();
    if (simplified.size() < 32) return false;
    int useful = 0;
    for (const QChar character : simplified) if (character.isLetterOrNumber()) ++useful;
    return useful >= 16 && useful * 2 >= simplified.size();
}

constexpr const char *upsertSql = R"SQL(
INSERT INTO files(name,path,parent_path,extension,type,size,mtime,ctime,inode,device,flags,is_dir,is_symlink,root,scan_generation,updated_at)
VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,unixepoch())
ON CONFLICT(path) DO UPDATE SET
 name=excluded.name,parent_path=excluded.parent_path,extension=excluded.extension,type=excluded.type,
 size=excluded.size,mtime=excluded.mtime,ctime=excluded.ctime,inode=excluded.inode,device=excluded.device,
 flags=(files.flags & 2) | excluded.flags,is_dir=excluded.is_dir,is_symlink=excluded.is_symlink,
 root=excluded.root,scan_generation=excluded.scan_generation,
 content_state=CASE WHEN files.size<>excluded.size OR files.mtime<>excluded.mtime OR files.inode<>excluded.inode THEN 0 ELSE files.content_state END,
 content_error=CASE WHEN files.size<>excluded.size OR files.mtime<>excluded.mtime OR files.inode<>excluded.inode THEN '' ELSE files.content_error END,
 updated_at=unixepoch()
)SQL";

void bindRecord(sqlite3_stmt *statement, const FileRecord &record)
{
    int column = 1;
    bindText(statement, column++, record.name);
    bindText(statement, column++, record.path);
    bindText(statement, column++, record.parentPath);
    bindText(statement, column++, record.extension);
    bindText(statement, column++, record.type);
    sqlite3_bind_int64(statement, column++, record.size);
    sqlite3_bind_int64(statement, column++, record.mtime);
    sqlite3_bind_int64(statement, column++, record.ctime);
    sqlite3_bind_int64(statement, column++, static_cast<sqlite3_int64>(record.inode));
    sqlite3_bind_int64(statement, column++, static_cast<sqlite3_int64>(record.device));
    sqlite3_bind_int(statement, column++, record.hidden ? static_cast<int>(Hidden) : 0);
    sqlite3_bind_int(statement, column++, record.directory);
    sqlite3_bind_int(statement, column++, record.symlink);
    bindText(statement, column++, record.root);
    sqlite3_bind_int64(statement, column++, record.scanGeneration);
}

} // namespace

Database::~Database() { close(); }

QString Database::sqliteError(sqlite3 *db)
{
    return db ? QString::fromUtf8(sqlite3_errmsg(db)) : QStringLiteral("Database is not open");
}

bool Database::open(const QString &path, bool readOnly, QString *error)
{
    close();
    if (!readOnly && !QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) *error = "Unable to create data directory";
        return false;
    }
    if (!readOnly)
        QFile::setPermissions(QFileInfo(path).absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QByteArray encoded = QFile::encodeName(path);
    const int flags = readOnly ? SQLITE_OPEN_READONLY
                               : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    if (sqlite3_open_v2(encoded.constData(), &db_, flags | SQLITE_OPEN_NOMUTEX, nullptr) != SQLITE_OK) {
        if (error) *error = sqliteError(db_);
        close();
        return false;
    }
    path_ = path;
    if (!readOnly)
        QFile::setPermissions(path_, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    sqlite3_busy_timeout(db_, 2000);
    if (!readOnly) {
        if (!execute("PRAGMA journal_mode=WAL;", error)
            || !execute("PRAGMA synchronous=NORMAL;", error)
            || !execute("PRAGMA temp_store=MEMORY;", error)
            || !execute("PRAGMA cache_size=-32768;", error)
            || !execute("PRAGMA mmap_size=268435456;", error)
            || !execute("PRAGMA foreign_keys=ON;", error)) {
            close();
            return false;
        }
    } else {
        execute("PRAGMA query_only=ON;", nullptr);
        execute("PRAGMA mmap_size=268435456;", nullptr);
    }
    return true;
}

void Database::close()
{
    if (db_) sqlite3_close_v2(db_);
    db_ = nullptr;
    path_.clear();
}

bool Database::execute(const char *sql, QString *error) const
{
    char *message = nullptr;
    const int result = sqlite3_exec(db_, sql, nullptr, nullptr, &message);
    if (result != SQLITE_OK) {
        if (error) *error = message ? QString::fromUtf8(message) : sqliteError(db_);
        sqlite3_free(message);
        return false;
    }
    return true;
}

int Database::schemaVersion(QString *error) const
{
    Statement statement(db_, "PRAGMA user_version");
    if (!statement.valid() || sqlite3_step(statement.get()) != SQLITE_ROW) {
        if (error) *error = sqliteError(db_);
        return -1;
    }
    return sqlite3_column_int(statement.get(), 0);
}

bool Database::migrate(QString *error, int targetVersion)
{
    if (targetVersion < 1 || targetVersion > 6) {
        if (error) *error = "Unsupported migration target";
        return false;
    }
    const int version = schemaVersion(error);
    if (version < 0) return false;
    if (version > 6) {
        if (error) *error = QString("Database schema %1 is newer than this application").arg(version);
        return false;
    }
    if (version == 0) {
        static constexpr const char *migration = R"SQL(
BEGIN IMMEDIATE;
CREATE TABLE files (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    path TEXT NOT NULL UNIQUE,
    parent_path TEXT NOT NULL,
    extension TEXT NOT NULL DEFAULT '',
    type TEXT NOT NULL DEFAULT '',
    size INTEGER NOT NULL DEFAULT 0,
    mtime INTEGER NOT NULL DEFAULT 0,
    ctime INTEGER NOT NULL DEFAULT 0,
    inode INTEGER NOT NULL DEFAULT 0,
    device INTEGER NOT NULL DEFAULT 0,
    flags INTEGER NOT NULL DEFAULT 0,
    is_dir INTEGER NOT NULL DEFAULT 0,
    is_symlink INTEGER NOT NULL DEFAULT 0,
    root TEXT NOT NULL,
    scan_generation INTEGER NOT NULL DEFAULT 0,
    updated_at INTEGER NOT NULL DEFAULT (unixepoch())
);
CREATE INDEX files_parent_idx ON files(parent_path);
CREATE INDEX files_name_nocase_idx ON files(name COLLATE NOCASE);
CREATE INDEX files_extension_idx ON files(extension);
CREATE INDEX files_mtime_idx ON files(mtime);
CREATE INDEX files_size_idx ON files(size);
CREATE INDEX files_root_generation_idx ON files(root, scan_generation);
CREATE INDEX files_device_inode_idx ON files(device, inode);
CREATE VIRTUAL TABLE files_fts USING fts5(
    name, path, content='files', content_rowid='id', tokenize='trigram case_sensitive 0'
);
CREATE TRIGGER files_ai AFTER INSERT ON files BEGIN
    INSERT INTO files_fts(rowid, name, path) VALUES (new.id, new.name, new.path);
END;
CREATE TRIGGER files_ad AFTER DELETE ON files BEGIN
    INSERT INTO files_fts(files_fts, rowid, name, path)
    VALUES ('delete', old.id, old.name, old.path);
END;
CREATE TRIGGER files_au AFTER UPDATE OF name, path ON files BEGIN
    INSERT INTO files_fts(files_fts, rowid, name, path)
    VALUES ('delete', old.id, old.name, old.path);
    INSERT INTO files_fts(rowid, name, path) VALUES (new.id, new.name, new.path);
END;
PRAGMA user_version=1;
COMMIT;
)SQL";
        if (!execute(migration, error)) {
            execute("ROLLBACK", nullptr);
            return false;
        }
        if (targetVersion == 1) return true;
    }
    if (version < 2) {
        static constexpr const char *migration2 = R"SQL(
BEGIN IMMEDIATE;
ALTER TABLE files ADD COLUMN content_state INTEGER NOT NULL DEFAULT 0;
ALTER TABLE files ADD COLUMN content_extractor TEXT NOT NULL DEFAULT '';
ALTER TABLE files ADD COLUMN content_error TEXT NOT NULL DEFAULT '';
ALTER TABLE files ADD COLUMN content_revision_mtime INTEGER NOT NULL DEFAULT 0;
ALTER TABLE files ADD COLUMN content_revision_size INTEGER NOT NULL DEFAULT -1;
ALTER TABLE files ADD COLUMN content_revision_inode INTEGER NOT NULL DEFAULT 0;
ALTER TABLE files ADD COLUMN content_updated_at INTEGER NOT NULL DEFAULT 0;
CREATE INDEX files_content_state_mtime_idx ON files(content_state, mtime DESC);
CREATE TABLE content_documents (
    file_id INTEGER PRIMARY KEY REFERENCES files(id) ON DELETE CASCADE,
    content TEXT NOT NULL
);
CREATE VIRTUAL TABLE content_fts USING fts5(
    content, content='content_documents', content_rowid='file_id',
    tokenize='unicode61 remove_diacritics 2 tokenchars ''-_'''
);
CREATE TRIGGER content_ai AFTER INSERT ON content_documents BEGIN
    INSERT INTO content_fts(rowid, content) VALUES (new.file_id, new.content);
END;
CREATE TRIGGER content_ad AFTER DELETE ON content_documents BEGIN
    INSERT INTO content_fts(content_fts, rowid, content) VALUES ('delete', old.file_id, old.content);
END;
CREATE TRIGGER content_au AFTER UPDATE OF content ON content_documents BEGIN
    INSERT INTO content_fts(content_fts, rowid, content) VALUES ('delete', old.file_id, old.content);
    INSERT INTO content_fts(rowid, content) VALUES (new.file_id, new.content);
END;
CREATE TABLE document_metadata (
    file_id INTEGER PRIMARY KEY REFERENCES files(id) ON DELETE CASCADE,
    title TEXT NOT NULL DEFAULT '', author TEXT NOT NULL DEFAULT '',
    subject TEXT NOT NULL DEFAULT '', keywords TEXT NOT NULL DEFAULT '',
    page_count INTEGER NOT NULL DEFAULT 0, details_json TEXT NOT NULL DEFAULT '{}',
    truncated INTEGER NOT NULL DEFAULT 0, extracted_bytes INTEGER NOT NULL DEFAULT 0,
    extraction_ms INTEGER NOT NULL DEFAULT 0
);
CREATE TRIGGER files_content_stale AFTER UPDATE OF size,mtime,inode ON files
WHEN old.size<>new.size OR old.mtime<>new.mtime OR old.inode<>new.inode BEGIN
    DELETE FROM content_documents WHERE file_id=new.id;
    DELETE FROM document_metadata WHERE file_id=new.id;
END;
CREATE TRIGGER files_content_default AFTER INSERT ON files
WHEN new.is_dir=1 OR new.extension NOT IN ('txt','md','markdown','pdf','docx','xlsx','pptx','odt','ods','odp') BEGIN
    UPDATE files SET content_state=5 WHERE id=new.id;
END;
UPDATE files SET content_state=5
WHERE is_dir=1 OR extension NOT IN ('txt','md','markdown','pdf','docx','xlsx','pptx','odt','ods','odp');
PRAGMA user_version=2;
COMMIT;
)SQL";
        if (!execute(migration2, error)) {
            execute("ROLLBACK", nullptr);
            return false;
        }
        if (targetVersion == 2) return true;
    }
    if (version < 3) {
        static constexpr const char *migration3 = R"SQL(
BEGIN IMMEDIATE;
ALTER TABLE files ADD COLUMN metadata_state INTEGER NOT NULL DEFAULT 4;
ALTER TABLE files ADD COLUMN metadata_error TEXT NOT NULL DEFAULT '';
ALTER TABLE files ADD COLUMN metadata_revision_mtime INTEGER NOT NULL DEFAULT 0;
ALTER TABLE files ADD COLUMN metadata_revision_size INTEGER NOT NULL DEFAULT -1;
ALTER TABLE files ADD COLUMN metadata_revision_inode INTEGER NOT NULL DEFAULT 0;
ALTER TABLE files ADD COLUMN metadata_updated_at INTEGER NOT NULL DEFAULT 0;
CREATE INDEX files_metadata_state_mtime_idx ON files(metadata_state,mtime DESC);
CREATE TABLE rich_metadata (
    file_id INTEGER PRIMARY KEY REFERENCES files(id) ON DELETE CASCADE,
    camera_make TEXT NOT NULL DEFAULT '', camera_model TEXT NOT NULL DEFAULT '',
    date_taken INTEGER NOT NULL DEFAULT 0, width INTEGER NOT NULL DEFAULT 0,
    height INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX rich_metadata_camera_idx ON rich_metadata(camera_make COLLATE NOCASE,camera_model COLLATE NOCASE);
CREATE INDEX rich_metadata_dimensions_idx ON rich_metadata(width,height);
CREATE INDEX rich_metadata_date_taken_idx ON rich_metadata(date_taken);
CREATE INDEX document_metadata_author_idx ON document_metadata(author COLLATE NOCASE);
CREATE TABLE document_pages (
    id INTEGER PRIMARY KEY, file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    page_number INTEGER NOT NULL, content TEXT NOT NULL,
    UNIQUE(file_id,page_number)
);
CREATE INDEX document_pages_file_idx ON document_pages(file_id,page_number);
CREATE VIRTUAL TABLE document_page_fts USING fts5(
    content, content='document_pages', content_rowid='id',
    tokenize='unicode61 remove_diacritics 2 tokenchars ''-_'''
);
CREATE TRIGGER document_pages_ai AFTER INSERT ON document_pages BEGIN
    INSERT INTO document_page_fts(rowid,content) VALUES(new.id,new.content);
END;
CREATE TRIGGER document_pages_ad AFTER DELETE ON document_pages BEGIN
    INSERT INTO document_page_fts(document_page_fts,rowid,content) VALUES('delete',old.id,old.content);
END;
CREATE TRIGGER document_pages_au AFTER UPDATE OF content ON document_pages BEGIN
    INSERT INTO document_page_fts(document_page_fts,rowid,content) VALUES('delete',old.id,old.content);
    INSERT INTO document_page_fts(rowid,content) VALUES(new.id,new.content);
END;
CREATE TABLE usage_history (
    file_id INTEGER PRIMARY KEY REFERENCES files(id) ON DELETE CASCADE,
    open_count INTEGER NOT NULL DEFAULT 0,
    last_opened INTEGER NOT NULL DEFAULT 0
);
CREATE TRIGGER files_metadata_stale AFTER UPDATE OF size,mtime,inode ON files
WHEN old.size<>new.size OR old.mtime<>new.mtime OR old.inode<>new.inode BEGIN
    DELETE FROM rich_metadata WHERE file_id=new.id;
    DELETE FROM document_pages WHERE file_id=new.id;
    UPDATE files SET metadata_state=CASE
      WHEN new.is_dir=0 AND (new.type LIKE 'image/%' OR new.extension IN ('jpg','jpeg','png','webp','bmp','gif','tif','tiff','avif','heif','heic')) THEN 0
      ELSE 4 END WHERE id=new.id;
END;
CREATE TRIGGER files_metadata_default AFTER INSERT ON files BEGIN
    UPDATE files SET metadata_state=CASE
      WHEN new.is_dir=0 AND (new.type LIKE 'image/%' OR new.extension IN ('jpg','jpeg','png','webp','bmp','gif','tif','tiff','avif','heif','heic')) THEN 0
      ELSE 4 END WHERE id=new.id;
END;
UPDATE files SET metadata_state=0
WHERE is_dir=0 AND (type LIKE 'image/%' OR extension IN ('jpg','jpeg','png','webp','bmp','gif','tif','tiff','avif','heif','heic'));
UPDATE files SET content_state=0 WHERE extension='pdf' AND content_state=3;
PRAGMA user_version=3;
COMMIT;
)SQL";
        if (!execute(migration3, error)) {
            execute("ROLLBACK", nullptr);
            return false;
        }
        if (targetVersion == 3) return true;
    }
    if (version < 4) {
        static constexpr const char *migration4 = R"SQL(
BEGIN IMMEDIATE;
ALTER TABLE files ADD COLUMN ocr_state INTEGER NOT NULL DEFAULT 0;
ALTER TABLE files ADD COLUMN ocr_error TEXT NOT NULL DEFAULT '';
ALTER TABLE files ADD COLUMN ocr_revision_mtime INTEGER NOT NULL DEFAULT 0;
ALTER TABLE files ADD COLUMN ocr_revision_size INTEGER NOT NULL DEFAULT -1;
ALTER TABLE files ADD COLUMN ocr_revision_inode INTEGER NOT NULL DEFAULT 0;
ALTER TABLE files ADD COLUMN ocr_processed_pages INTEGER NOT NULL DEFAULT 0;
ALTER TABLE files ADD COLUMN ocr_total_pages INTEGER NOT NULL DEFAULT 0;
ALTER TABLE files ADD COLUMN ocr_retry_count INTEGER NOT NULL DEFAULT 0;
ALTER TABLE files ADD COLUMN ocr_languages TEXT NOT NULL DEFAULT '';
ALTER TABLE files ADD COLUMN ocr_updated_at INTEGER NOT NULL DEFAULT 0;
CREATE INDEX files_ocr_state_mtime_idx ON files(ocr_state,mtime DESC);
CREATE INDEX files_device_inode_ocr_idx ON files(device,inode,ocr_state);
CREATE TABLE ocr_pages (
    id INTEGER PRIMARY KEY,
    file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    page_number INTEGER NOT NULL,
    text TEXT NOT NULL,
    confidence REAL NOT NULL DEFAULT 0,
    UNIQUE(file_id,page_number)
);
CREATE INDEX ocr_pages_file_idx ON ocr_pages(file_id,page_number);
CREATE VIRTUAL TABLE ocr_fts USING fts5(
    text, content='ocr_pages', content_rowid='id',
    tokenize='unicode61 remove_diacritics 2 tokenchars ''-_'''
);
CREATE TRIGGER ocr_pages_ai AFTER INSERT ON ocr_pages BEGIN
    INSERT INTO ocr_fts(rowid,text) VALUES(new.id,new.text);
END;
CREATE TRIGGER ocr_pages_ad AFTER DELETE ON ocr_pages BEGIN
    INSERT INTO ocr_fts(ocr_fts,rowid,text) VALUES('delete',old.id,old.text);
END;
CREATE TRIGGER ocr_pages_au AFTER UPDATE OF text ON ocr_pages BEGIN
    INSERT INTO ocr_fts(ocr_fts,rowid,text) VALUES('delete',old.id,old.text);
    INSERT INTO ocr_fts(rowid,text) VALUES(new.id,new.text);
END;
CREATE TRIGGER files_ocr_stale AFTER UPDATE OF size,mtime,inode ON files
WHEN old.size<>new.size OR old.mtime<>new.mtime OR old.inode<>new.inode BEGIN
    DELETE FROM ocr_pages WHERE file_id=new.id;
    UPDATE files SET ocr_state=CASE WHEN new.is_dir=0 AND new.extension='pdf' THEN 1 ELSE 0 END,
      ocr_error='',ocr_processed_pages=0,ocr_total_pages=0,ocr_retry_count=0,ocr_languages='',
      ocr_revision_mtime=0,ocr_revision_size=-1,ocr_revision_inode=0 WHERE id=new.id;
END;
CREATE TRIGGER files_ocr_default AFTER INSERT ON files BEGIN
    UPDATE files SET ocr_state=CASE WHEN new.is_dir=0 AND new.extension='pdf' THEN 1 ELSE 0 END WHERE id=new.id;
END;
UPDATE files SET ocr_state=1 WHERE is_dir=0 AND extension='pdf' AND content_state<>6;
UPDATE files SET ocr_state=7,ocr_error='encrypted PDF' WHERE is_dir=0 AND extension='pdf' AND content_state=6;
PRAGMA user_version=4;
COMMIT;
)SQL";
        if (!execute(migration4, error)) {
            execute("ROLLBACK", nullptr);
            return false;
        }
        if (targetVersion == 4) return true;
    }
    if (version < 5) {
        // Hyphen and underscore used to be token characters. That preserved
        // whole technical identifiers but made a useful fragment such as
        // d81f94be unable to match ZTEG-d81f94be. With the default unicode61
        // separators, a full identifier is still parsed as an adjacent FTS
        // phrase while each meaningful component is independently searchable.
        static constexpr const char *migration5 = R"SQL(
BEGIN IMMEDIATE;
DROP TRIGGER content_ai;
DROP TRIGGER content_ad;
DROP TRIGGER content_au;
DROP TABLE content_fts;
CREATE VIRTUAL TABLE content_fts USING fts5(
    content, content='content_documents', content_rowid='file_id',
    tokenize='unicode61 remove_diacritics 2'
);
CREATE TRIGGER content_ai AFTER INSERT ON content_documents BEGIN
    INSERT INTO content_fts(rowid, content) VALUES (new.file_id, new.content);
END;
CREATE TRIGGER content_ad AFTER DELETE ON content_documents BEGIN
    INSERT INTO content_fts(content_fts, rowid, content) VALUES ('delete', old.file_id, old.content);
END;
CREATE TRIGGER content_au AFTER UPDATE OF content ON content_documents BEGIN
    INSERT INTO content_fts(content_fts, rowid, content) VALUES ('delete', old.file_id, old.content);
    INSERT INTO content_fts(rowid, content) VALUES (new.file_id, new.content);
END;
INSERT INTO content_fts(content_fts) VALUES('rebuild');

DROP TRIGGER document_pages_ai;
DROP TRIGGER document_pages_ad;
DROP TRIGGER document_pages_au;
DROP TABLE document_page_fts;
CREATE VIRTUAL TABLE document_page_fts USING fts5(
    content, content='document_pages', content_rowid='id',
    tokenize='unicode61 remove_diacritics 2'
);
CREATE TRIGGER document_pages_ai AFTER INSERT ON document_pages BEGIN
    INSERT INTO document_page_fts(rowid,content) VALUES(new.id,new.content);
END;
CREATE TRIGGER document_pages_ad AFTER DELETE ON document_pages BEGIN
    INSERT INTO document_page_fts(document_page_fts,rowid,content) VALUES('delete',old.id,old.content);
END;
CREATE TRIGGER document_pages_au AFTER UPDATE OF content ON document_pages BEGIN
    INSERT INTO document_page_fts(document_page_fts,rowid,content) VALUES('delete',old.id,old.content);
    INSERT INTO document_page_fts(rowid,content) VALUES(new.id,new.content);
END;
INSERT INTO document_page_fts(document_page_fts) VALUES('rebuild');

DROP TRIGGER ocr_pages_ai;
DROP TRIGGER ocr_pages_ad;
DROP TRIGGER ocr_pages_au;
DROP TABLE ocr_fts;
CREATE VIRTUAL TABLE ocr_fts USING fts5(
    text, content='ocr_pages', content_rowid='id',
    tokenize='unicode61 remove_diacritics 2'
);
CREATE TRIGGER ocr_pages_ai AFTER INSERT ON ocr_pages BEGIN
    INSERT INTO ocr_fts(rowid,text) VALUES(new.id,new.text);
END;
CREATE TRIGGER ocr_pages_ad AFTER DELETE ON ocr_pages BEGIN
    INSERT INTO ocr_fts(ocr_fts,rowid,text) VALUES('delete',old.id,old.text);
END;
CREATE TRIGGER ocr_pages_au AFTER UPDATE OF text ON ocr_pages BEGIN
    INSERT INTO ocr_fts(ocr_fts,rowid,text) VALUES('delete',old.id,old.text);
    INSERT INTO ocr_fts(rowid,text) VALUES(new.id,new.text);
END;
INSERT INTO ocr_fts(ocr_fts) VALUES('rebuild');
PRAGMA user_version=5;
COMMIT;
)SQL";
        if (!execute(migration5, error)) {
            execute("ROLLBACK", nullptr);
            return false;
        }
        if (targetVersion == 5) return true;
    }
    if (version < 6) {
        // Image OCR preprocessing gained small-region upscaling and additional
        // recognition passes. Invalidate image results once so existing users
        // benefit without having to delete the complete index.
        static constexpr const char *migration6 = R"SQL(
BEGIN IMMEDIATE;
ALTER TABLE files ADD COLUMN ocr_engine_version INTEGER NOT NULL DEFAULT 0;
DROP TRIGGER files_ocr_stale;
CREATE TRIGGER files_ocr_stale AFTER UPDATE OF size,mtime,inode ON files
WHEN old.size<>new.size OR old.mtime<>new.mtime OR old.inode<>new.inode BEGIN
    DELETE FROM ocr_pages WHERE file_id=new.id;
    UPDATE files SET ocr_state=CASE WHEN new.is_dir=0 AND new.extension='pdf' THEN 1 ELSE 0 END,
      ocr_error='',ocr_processed_pages=0,ocr_total_pages=0,ocr_retry_count=0,ocr_languages='',
      ocr_revision_mtime=0,ocr_revision_size=-1,ocr_revision_inode=0,ocr_engine_version=0 WHERE id=new.id;
END;
DELETE FROM ocr_pages WHERE file_id IN (
    SELECT id FROM files WHERE is_dir=0 AND type LIKE 'image/%'
      AND extension IN ('jpg','jpeg','png','tif','tiff','webp')
);
UPDATE files SET ocr_state=0,ocr_error='',ocr_processed_pages=0,ocr_total_pages=0,
  ocr_retry_count=0,ocr_languages='',ocr_revision_mtime=0,ocr_revision_size=-1,
  ocr_revision_inode=0,ocr_engine_version=0
WHERE is_dir=0 AND type LIKE 'image/%'
  AND extension IN ('jpg','jpeg','png','tif','tiff','webp');
PRAGMA user_version=6;
COMMIT;
)SQL";
        if (!execute(migration6, error)) {
            execute("ROLLBACK", nullptr);
            return false;
        }
    }
    return true;
}

bool Database::begin(QString *error) { return execute("BEGIN IMMEDIATE", error); }
bool Database::commit(QString *error) { return execute("COMMIT", error); }
void Database::rollback() { execute("ROLLBACK", nullptr); }

bool Database::upsert(const FileRecord &record, QString *error)
{
    Statement statement(db_, upsertSql);
    if (!statement.valid()) {
        if (error) *error = sqliteError(db_);
        return false;
    }
    bindRecord(statement.get(), record);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        if (error) *error = sqliteError(db_);
        return false;
    }
    return true;
}

bool Database::upsertBatch(const QVector<FileRecord> &records, QString *error)
{
    if (records.isEmpty()) return true;
    if (!begin(error)) return false;
    Statement statement(db_, upsertSql);
    if (!statement.valid()) {
        if (error) *error = sqliteError(db_);
        rollback();
        return false;
    }
    for (const auto &record : records) {
        bindRecord(statement.get(), record);
        if (sqlite3_step(statement.get()) != SQLITE_DONE) {
            if (error) *error = sqliteError(db_);
            rollback();
            return false;
        }
        sqlite3_reset(statement.get());
        sqlite3_clear_bindings(statement.get());
    }
    if (!commit(error)) {
        rollback();
        return false;
    }
    return true;
}

bool Database::removePath(const QString &path, bool recursive, QString *error)
{
    const QByteArray sql = recursive
        ? "DELETE FROM files WHERE path=? OR path LIKE ? ESCAPE '\\'"
        : "DELETE FROM files WHERE path=?";
    Statement statement(db_, sql);
    if (!statement.valid()) {
        if (error) *error = sqliteError(db_);
        return false;
    }
    bindText(statement.get(), 1, path);
    if (recursive) bindText(statement.get(), 2, escapedLike(path) + "/%");
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        if (error) *error = sqliteError(db_);
        return false;
    }
    return true;
}

bool Database::removeMissing(const QString &root, qint64 generation, QString *error)
{
    Statement statement(db_, "DELETE FROM files WHERE root=? AND scan_generation<>?");
    if (!statement.valid()) {
        if (error) *error = sqliteError(db_);
        return false;
    }
    bindText(statement.get(), 1, root);
    sqlite3_bind_int64(statement.get(), 2, generation);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        if (error) *error = sqliteError(db_);
        return false;
    }
    return true;
}

bool Database::markRootOffline(const QString &root, bool offline, QString *error)
{
    Statement statement(db_, offline
        ? "UPDATE files SET flags=flags|2 WHERE root=?"
        : "UPDATE files SET flags=flags&~2 WHERE root=?");
    if (!statement.valid()) {
        if (error) *error = sqliteError(db_);
        return false;
    }
    bindText(statement.get(), 1, root);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        if (error) *error = sqliteError(db_);
        return false;
    }
    return true;
}

QVector<SearchResult> Database::search(const ParsedQuery &query, int limit,
                                       bool showHidden, QString *error, bool useUsageHistory) const
{
    QVector<SearchResult> results;
    QStringList where;
    struct Binding { enum Type { Text, Integer } type; QString text; qint64 integer{}; };
    QVector<Binding> bindings;
    const bool hasText = !query.text.isEmpty();
    const bool useFts = hasText && query.text.size() >= 3;
    const bool applyUsageHistory = useUsageHistory && hasUsageHistory(db_);

    const bool needsDocumentMetadata = !query.author.isEmpty() || query.minimumPages >= 0 || query.maximumPages >= 0;
    const bool needsRichMetadata = !query.camera.isEmpty() || query.minimumWidth >= 0 || query.maximumWidth >= 0
        || query.minimumHeight >= 0 || query.maximumHeight >= 0;
    QString from = "files f";
    if (useFts) {
        from += " JOIN files_fts x ON x.rowid=f.id";
        where << "files_fts MATCH ?";
        QString expression = query.ftsExpression;
        if (expression.isEmpty()) {
            QString phrase = query.text;
            phrase.replace('"', "\"\"");
            expression = '"' + phrase + '"';
        }
        // Unified search intentionally matches the item's name only.  The path
        // column is searchable through the explicit path: filter; including it
        // here would make a query such as "guedes" return every descendant of
        // /home/guedes instead of the matching folder itself.
        if (query.scope == SearchScope::Name || query.scope == SearchScope::Unified)
            expression = "{name} : (" + expression + ')';
        else if (query.scope == SearchScope::Path) expression = "{path} : (" + expression + ')';
        bindings.append({Binding::Text, expression, 0});
    } else if (hasText) {
        // FTS trigram needs three code points. Keep the first two keystrokes
        // instant with an indexed name-prefix query. An explicit path scope is
        // allowed to scan because the user deliberately requested that scope.
        if (query.scope == SearchScope::Path) {
            where << "instr(lower(f.path),lower(?))>0";
            bindings.append({Binding::Text, query.text, 0});
        } else {
            where << "(f.name>=? COLLATE NOCASE AND f.name<? COLLATE NOCASE)";
            QString upperBound = query.text;
            upperBound.append(QChar::highSurrogate(0x10ffff));
            upperBound.append(QChar::lowSurrogate(0x10ffff));
            bindings.append({Binding::Text, query.text, 0});
            bindings.append({Binding::Text, upperBound, 0});
        }
    }
    if (needsDocumentMetadata) from += " LEFT JOIN document_metadata dm ON dm.file_id=f.id";
    if (needsRichMetadata) from += " LEFT JOIN rich_metadata rm ON rm.file_id=f.id";
    if (applyUsageHistory) from += " LEFT JOIN usage_history u ON u.file_id=f.id";
    if (!query.extension.isEmpty()) {
        where << "f.extension=?";
        bindings.append({Binding::Text, query.extension, 0});
    }
    if (!query.folder.isEmpty()) {
        where << "instr(lower(f.parent_path),lower(?))>0";
        bindings.append({Binding::Text, query.folder, 0});
    }
    if (query.minimumSize >= 0) {
        where << "f.size>?";
        bindings.append({Binding::Integer, {}, query.minimumSize});
    }
    if (query.maximumSize >= 0) {
        where << "f.size<?";
        bindings.append({Binding::Integer, {}, query.maximumSize});
    }
    if (!query.author.isEmpty()) { where << "instr(lower(dm.author),lower(?))>0"; bindings.append({Binding::Text, query.author, 0}); }
    if (!query.camera.isEmpty()) {
        where << "instr(lower(rm.camera_make||' '||rm.camera_model),lower(?))>0";
        bindings.append({Binding::Text, query.camera, 0});
    }
    auto addNumericFilters = [&where, &bindings](const QString &column, int minimum, int maximum) {
        if (minimum >= 0 && maximum == minimum) {
            where << column + "=?"; bindings.append({Binding::Integer, {}, minimum});
        } else {
            if (minimum >= 0) { where << column + ">?"; bindings.append({Binding::Integer, {}, minimum}); }
            if (maximum >= 0) { where << column + "<?"; bindings.append({Binding::Integer, {}, maximum}); }
        }
    };
    addNumericFilters("dm.page_count", query.minimumPages, query.maximumPages);
    addNumericFilters("rm.width", query.minimumWidth, query.maximumWidth);
    addNumericFilters("rm.height", query.minimumHeight, query.maximumHeight);
    if (query.directoriesOnly) where << "f.is_dir=1";
    if (query.filesOnly) where << "f.is_dir=0";
    if (query.category == "image") where << "f.type LIKE 'image/%'";
    else if (query.category == "video") where << "f.type LIKE 'video/%'";
    else if (query.category == "document")
        where << "f.extension IN ('pdf','txt','md','doc','docx','odt','xls','xlsx','ods','ppt','pptx')";
    else if (query.category == "other")
        where << "f.is_dir=0 AND f.type NOT LIKE 'image/%' AND f.type NOT LIKE 'video/%' "
                 "AND f.extension NOT IN ('pdf','txt','md','doc','docx','odt','xls','xlsx','ods','ppt','pptx')";
    if (!showHidden) where << "(f.flags&1)=0";
    where << "(f.flags&2)=0";

    int days = 0;
    if (query.modified == ModifiedFilter::Today) days = 1;
    else if (query.modified == ModifiedFilter::Days7) days = 7;
    else if (query.modified == ModifiedFilter::Days30) days = 30;
    if (days) {
        where << "f.mtime>=?";
        bindings.append({Binding::Integer, {}, QDateTime::currentSecsSinceEpoch() - days * 86400LL});
    }

    const QString usageBonus = applyUsageHistory
        ? " + min(coalesce(u.open_count,0),10)*2 + CASE WHEN u.last_opened>unixepoch()-604800 THEN 8 ELSE 0 END"
        : QString();
    QString score = "(CASE WHEN f.mtime>unixepoch()-2592000 THEN 20 ELSE 0 END"
        + usageBonus + ")";
    if (hasText) {
        score = "(CASE WHEN f.is_dir=0 AND lower(f.name)=lower(?) THEN 1200 "
                "WHEN f.is_dir=0 AND lower(f.name) LIKE lower(?) ESCAPE '\\' THEN 1050 "
                "WHEN lower(f.name)=lower(?) THEN 1000 "
                "WHEN lower(f.name) LIKE lower(?) ESCAPE '\\' THEN 700 "
                "WHEN instr(lower(f.name),lower(?))>0 THEN 450 ELSE 180 END "
                "+ CASE WHEN f.mtime>unixepoch()-2592000 THEN 20 ELSE 0 END "
                "+ CASE WHEN f.is_dir=1 THEN 5 ELSE 0 END" + usageBonus + ")";
    }
    QString sql = "SELECT f.id,f.name,f.path,f.parent_path,f.extension,f.type,f.size,f.mtime,f.ctime,"
                  "f.inode,f.device,f.flags,f.is_dir,f.is_symlink,f.root,f.scan_generation," + score
                  + " AS score," + (applyUsageHistory ? "coalesce(u.open_count,0)" : "0")
                  + (needsDocumentMetadata ? ",coalesce(dm.title,''),coalesce(dm.author,''),coalesce(dm.page_count,0)" : ",'','',0")
                  + (needsRichMetadata ? ",coalesce(rm.camera_make,''),coalesce(rm.camera_model,''),coalesce(rm.width,0),coalesce(rm.height,0),coalesce(rm.date_taken,0)" : ",'','',0,0,0")
                  + " FROM " + from;
    if (!where.isEmpty()) sql += " WHERE " + where.join(" AND ");
    sql += " ORDER BY score DESC, f.mtime DESC, length(f.path), f.name COLLATE NOCASE LIMIT ?";

    Statement statement(db_, sql.toUtf8());
    if (!statement.valid()) {
        if (error) *error = sqliteError(db_);
        return results;
    }
    int parameter = 1;
    if (hasText) {
        bindText(statement.get(), parameter++, query.text);
        bindText(statement.get(), parameter++, escapedLike(query.text) + "%");
        bindText(statement.get(), parameter++, query.text);
        bindText(statement.get(), parameter++, escapedLike(query.text) + "%");
        bindText(statement.get(), parameter++, query.text);
    }
    for (const auto &binding : bindings) {
        if (binding.type == Binding::Text) bindText(statement.get(), parameter++, binding.text);
        else sqlite3_bind_int64(statement.get(), parameter++, binding.integer);
    }
    sqlite3_bind_int(statement.get(), parameter, qBound(1, limit, 1000));

    int result = SQLITE_ROW;
    while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
        SearchResult entry;
        entry.file = recordFrom(statement.get());
        entry.score = sqlite3_column_double(statement.get(), 16);
        const int openCount = sqlite3_column_int(statement.get(), 17);
        entry.documentTitle = columnText(statement.get(), 18);
        entry.documentAuthor = columnText(statement.get(), 19);
        entry.pageCount = sqlite3_column_int(statement.get(), 20);
        entry.cameraMake = columnText(statement.get(), 21);
        entry.cameraModel = columnText(statement.get(), 22);
        entry.imageWidth = sqlite3_column_int(statement.get(), 23);
        entry.imageHeight = sqlite3_column_int(statement.get(), 24);
        entry.dateTaken = sqlite3_column_int64(statement.get(), 25);
        QStringList scoreParts;
        if (hasText) {
            if (!entry.file.directory && entry.file.name.compare(query.text, Qt::CaseInsensitive) == 0) scoreParts << "exact-file-name +1200";
            else if (!entry.file.directory && entry.file.name.startsWith(query.text, Qt::CaseInsensitive)) scoreParts << "file-name-prefix +1050";
            else if (entry.file.name.compare(query.text, Qt::CaseInsensitive) == 0) scoreParts << "exact-folder-name +1000";
            else if (entry.file.name.startsWith(query.text, Qt::CaseInsensitive)) scoreParts << "name-prefix +700";
            else if (entry.file.name.contains(query.text, Qt::CaseInsensitive)) scoreParts << "name-substring +450";
            else scoreParts << "path +180";
        }
        if (entry.file.mtime > QDateTime::currentSecsSinceEpoch() - 2592000) scoreParts << "recent-modification +20";
        if (openCount > 0 && applyUsageHistory) scoreParts << QString("local-usage +%1").arg(qMin(openCount, 10) * 2);
        entry.scoreExplanation = scoreParts.join(", ") + QString("; total %1").arg(entry.score, 0, 'f', 2);
        results.append(std::move(entry));
    }
    if (result != SQLITE_DONE && error) *error = sqliteError(db_);
    for (auto &entry : results) {
        if (query.scope == SearchScope::Path) entry.matchOrigin = "path";
        else if (query.scope == SearchScope::Name
                 || entry.file.name.contains(query.text, Qt::CaseInsensitive)) entry.matchOrigin = "name";
        else entry.matchOrigin = "path";
    }
    return results;
}

QVector<SearchResult> Database::searchContent(const ParsedQuery &query, int limit,
                                              bool showHidden, QString *error, bool useUsageHistory) const
{
    QVector<SearchResult> results;
    if (query.text.trimmed().isEmpty()) return results;
    const bool applyUsageHistory = useUsageHistory && hasUsageHistory(db_);
    const bool needsRichMetadata = !query.camera.isEmpty() || query.minimumWidth >= 0 || query.maximumWidth >= 0
        || query.minimumHeight >= 0 || query.maximumHeight >= 0;
    QString expression = query.ftsExpression;
    if (expression.isEmpty()) {
        QString text = query.text;
        text.replace('"', "\"\"");
        expression = '"' + text + '"';
    }
    QStringList where{"content_fts MATCH ?", "(f.flags&2)=0"};
    struct Binding { bool text; QString value; qint64 number{}; };
    QVector<Binding> bindings{{true, expression, 0}};
    if (!showHidden) where << "(f.flags&1)=0";
    if (!query.extension.isEmpty()) { where << "f.extension=?"; bindings.append({true, query.extension, 0}); }
    if (!query.folder.isEmpty()) { where << "instr(lower(f.parent_path),lower(?))>0"; bindings.append({true, query.folder, 0}); }
    if (query.minimumSize >= 0) { where << "f.size>?"; bindings.append({false, {}, query.minimumSize}); }
    if (query.maximumSize >= 0) { where << "f.size<?"; bindings.append({false, {}, query.maximumSize}); }
    if (!query.author.isEmpty()) { where << "instr(lower(m.author),lower(?))>0"; bindings.append({true, query.author, 0}); }
    if (!query.camera.isEmpty()) {
        where << "instr(lower(coalesce(rm.camera_make,'')||' '||coalesce(rm.camera_model,'')),lower(?))>0";
        bindings.append({true, query.camera, 0});
    }
    auto addNumericFilters = [&where, &bindings](const QString &column, int minimum, int maximum) {
        if (minimum >= 0 && maximum == minimum) {
            where << column + "=?"; bindings.append({false, {}, minimum});
        } else {
            if (minimum >= 0) { where << column + ">?"; bindings.append({false, {}, minimum}); }
            if (maximum >= 0) { where << column + "<?"; bindings.append({false, {}, maximum}); }
        }
    };
    addNumericFilters("m.page_count", query.minimumPages, query.maximumPages);
    addNumericFilters("rm.width", query.minimumWidth, query.maximumWidth);
    addNumericFilters("rm.height", query.minimumHeight, query.maximumHeight);
    if (query.directoriesOnly) where << "f.is_dir=1";
    if (query.filesOnly) where << "f.is_dir=0";
    int days = query.modified == ModifiedFilter::Today ? 1
             : query.modified == ModifiedFilter::Days7 ? 7
             : query.modified == ModifiedFilter::Days30 ? 30 : 0;
    if (days) { where << "f.mtime>=?"; bindings.append({false, {}, QDateTime::currentSecsSinceEpoch() - days * 86400LL}); }
    if (query.category == "document")
        where << "f.extension IN ('pdf','txt','md','markdown','docx','xlsx','pptx','odt','ods','odp')";

    const QString usageBonus = applyUsageHistory
        ? "+min(coalesce(u.open_count,0),10)*2+CASE WHEN u.last_opened>unixepoch()-604800 THEN 8 ELSE 0 END"
        : QString();
    const QString sql =
        "SELECT f.id,f.name,f.path,f.parent_path,f.extension,f.type,f.size,f.mtime,f.ctime,"
        "f.inode,f.device,f.flags,f.is_dir,f.is_symlink,f.root,f.scan_generation,"
        "120.0+(-bm25(content_fts))" + usageBonus + " AS score,"
        "snippet(content_fts,0,'[[PFH]]','[[/PFH]]',' … ',24),"
        "coalesce(m.title,''),coalesce(m.author,''),coalesce(m.page_count,0),"
        + (needsRichMetadata ? "coalesce(rm.camera_make,''),coalesce(rm.camera_model,''),coalesce(rm.width,0),coalesce(rm.height,0),coalesce(rm.date_taken,0)," : "'','',0,0,0,")
        + (applyUsageHistory ? "coalesce(u.open_count,0)" : "0") + " "
        "FROM content_fts JOIN files f ON f.id=content_fts.rowid "
        "LEFT JOIN document_metadata m ON m.file_id=f.id "
        + (needsRichMetadata ? "LEFT JOIN rich_metadata rm ON rm.file_id=f.id " : QString())
        + (applyUsageHistory ? "LEFT JOIN usage_history u ON u.file_id=f.id " : QString())
        + "WHERE " + where.join(" AND ")
        + " ORDER BY score DESC, f.mtime DESC LIMIT ?";
    Statement statement(db_, sql.toUtf8());
    if (!statement.valid()) { if (error) *error = sqliteError(db_); return results; }
    int parameter = 1;
    for (const auto &binding : bindings) {
        if (binding.text) bindText(statement.get(), parameter++, binding.value);
        else sqlite3_bind_int64(statement.get(), parameter++, binding.number);
    }
    sqlite3_bind_int(statement.get(), parameter, qBound(1, limit, 3000));
    int step = SQLITE_ROW;
    while ((step = sqlite3_step(statement.get())) == SQLITE_ROW) {
        SearchResult entry;
        entry.file = recordFrom(statement.get());
        entry.score = sqlite3_column_double(statement.get(), 16);
        entry.snippet = columnText(statement.get(), 17);
        entry.documentTitle = columnText(statement.get(), 18);
        entry.documentAuthor = columnText(statement.get(), 19);
        entry.pageCount = sqlite3_column_int(statement.get(), 20);
        entry.cameraMake = columnText(statement.get(), 21);
        entry.cameraModel = columnText(statement.get(), 22);
        entry.imageWidth = sqlite3_column_int(statement.get(), 23);
        entry.imageHeight = sqlite3_column_int(statement.get(), 24);
        entry.dateTaken = sqlite3_column_int64(statement.get(), 25);
        const int openCount = sqlite3_column_int(statement.get(), 26);
        entry.scoreExplanation = QString("content +120%1; total %2")
            .arg(openCount > 0 && applyUsageHistory
                     ? QString(", local-usage +%1").arg(qMin(openCount, 10) * 2) : QString())
            .arg(entry.score, 0, 'f', 2);
        entry.matchOrigin = "content";
        results.append(std::move(entry));
    }
    if (step != SQLITE_DONE && error) *error = sqliteError(db_);
    QVector<qint64> pdfIds;
    for (const auto &entry : results) if (entry.file.extension == "pdf") pdfIds.append(entry.file.id);
    if (!pdfIds.isEmpty()) {
        QStringList placeholders;
        for (qsizetype index = 0; index < pdfIds.size(); ++index) placeholders << "?";
        const QString pageSql = "SELECT p.file_id,min(p.page_number) FROM document_page_fts "
            "JOIN document_pages p ON p.id=document_page_fts.rowid WHERE document_page_fts MATCH ? "
            "AND p.file_id IN (" + placeholders.join(',') + ") GROUP BY p.file_id";
        Statement pages(db_, pageSql.toUtf8());
        if (pages.valid()) {
            bindText(pages.get(), 1, expression);
            for (int index = 0; index < pdfIds.size(); ++index)
                sqlite3_bind_int64(pages.get(), index + 2, pdfIds.at(index));
            QHash<qint64, int> matchPages;
            while (sqlite3_step(pages.get()) == SQLITE_ROW)
                matchPages.insert(sqlite3_column_int64(pages.get(), 0), sqlite3_column_int(pages.get(), 1));
            for (auto &entry : results) entry.matchPage = matchPages.value(entry.file.id);
        }
    }
    return results;
}

QVector<SearchResult> Database::searchOcr(const ParsedQuery &query, int limit,
                                          bool showHidden, QString *error, bool useUsageHistory) const
{
    QVector<SearchResult> results;
    if (query.text.trimmed().isEmpty()) return results;
    QString expression = query.ftsExpression;
    if (expression.isEmpty()) {
        QString text = query.text; text.replace('"', "\"\"");
        expression = '"' + text + '"';
    }
    // OCR commonly loses or misreads the final glyph of a small word. For a
    // single, unquoted OCR term, also accept the same prefix without its last
    // character. This stays isolated from filename/native-content matching.
    if (!query.phrase && query.text.size() >= 6 && !query.text.contains(QChar::Space)) {
        QString prefix = query.text.left(query.text.size() - 1);
        prefix.replace('"', "\"\"");
        expression = '(' + expression + " OR \"" + prefix + "\"*)";
    }
    const bool applyUsageHistory = useUsageHistory && hasUsageHistory(db_);
    const bool needsRich = !query.camera.isEmpty() || query.minimumWidth >= 0 || query.maximumWidth >= 0
        || query.minimumHeight >= 0 || query.maximumHeight >= 0;
    QStringList where{"ocr_fts MATCH ?", "(f.flags&2)=0"};
    struct Binding { bool text; QString value; qint64 number{}; };
    QVector<Binding> bindings{{true, expression, 0}};
    if (!showHidden) where << "(f.flags&1)=0";
    if (!query.extension.isEmpty()) { where << "f.extension=?"; bindings.append({true, query.extension, 0}); }
    if (!query.folder.isEmpty()) { where << "instr(lower(f.parent_path),lower(?))>0"; bindings.append({true, query.folder, 0}); }
    if (query.minimumSize >= 0) { where << "f.size>?"; bindings.append({false, {}, query.minimumSize}); }
    if (query.maximumSize >= 0) { where << "f.size<?"; bindings.append({false, {}, query.maximumSize}); }
    if (!query.author.isEmpty()) { where << "instr(lower(coalesce(dm.author,'')),lower(?))>0"; bindings.append({true, query.author, 0}); }
    if (!query.camera.isEmpty()) {
        where << "instr(lower(coalesce(rm.camera_make,'')||' '||coalesce(rm.camera_model,'')),lower(?))>0";
        bindings.append({true, query.camera, 0});
    }
    auto numeric = [&where, &bindings](const QString &column, int minimum, int maximum) {
        if (minimum >= 0 && maximum == minimum) { where << column + "=?"; bindings.append({false, {}, minimum}); }
        else {
            if (minimum >= 0) { where << column + ">?"; bindings.append({false, {}, minimum}); }
            if (maximum >= 0) { where << column + "<?"; bindings.append({false, {}, maximum}); }
        }
    };
    numeric("coalesce(dm.page_count,f.ocr_total_pages)", query.minimumPages, query.maximumPages);
    numeric("rm.width", query.minimumWidth, query.maximumWidth);
    numeric("rm.height", query.minimumHeight, query.maximumHeight);
    if (query.directoriesOnly) where << "f.is_dir=1";
    if (query.filesOnly) where << "f.is_dir=0";
    if (query.category == "image") where << "f.type LIKE 'image/%'";
    else if (query.category == "document") where << "f.extension='pdf'";
    else if (query.category == "video") where << "0";
    int days = query.modified == ModifiedFilter::Today ? 1
             : query.modified == ModifiedFilter::Days7 ? 7
             : query.modified == ModifiedFilter::Days30 ? 30 : 0;
    if (days) { where << "f.mtime>=?"; bindings.append({false, {}, QDateTime::currentSecsSinceEpoch() - days * 86400LL}); }
    const QString usage = applyUsageHistory
        ? "+min(coalesce(u.open_count,0),10)*2+CASE WHEN u.last_opened>unixepoch()-604800 THEN 8 ELSE 0 END"
        : QString();
    const QString phraseBonus = query.phrase ? "+6" : QString();
    const QString sql =
        "SELECT f.id,f.name,f.path,f.parent_path,f.extension,f.type,f.size,f.mtime,f.ctime,"
        "f.inode,f.device,f.flags,f.is_dir,f.is_symlink,f.root,f.scan_generation,"
        "80.0+min(max(op.confidence,0),100)*0.10+(-bm25(ocr_fts))" + phraseBonus + usage + " AS score,"
        "snippet(ocr_fts,0,'[[PFH]]','[[/PFH]]',' … ',24),op.page_number,op.confidence,"
        "coalesce(dm.title,''),coalesce(dm.author,''),coalesce(dm.page_count,f.ocr_total_pages),"
        + (needsRich ? "coalesce(rm.camera_make,''),coalesce(rm.camera_model,''),coalesce(rm.width,0),coalesce(rm.height,0),coalesce(rm.date_taken,0)," : "'','',0,0,0,")
        + (applyUsageHistory ? "coalesce(u.open_count,0) " : "0 ")
        + "FROM ocr_fts JOIN ocr_pages op ON op.id=ocr_fts.rowid JOIN files f ON f.id=op.file_id "
          "LEFT JOIN document_metadata dm ON dm.file_id=f.id "
        + (needsRich ? "LEFT JOIN rich_metadata rm ON rm.file_id=f.id " : QString())
        + (applyUsageHistory ? "LEFT JOIN usage_history u ON u.file_id=f.id " : QString())
        + "WHERE " + where.join(" AND ") + " ORDER BY score DESC,f.mtime DESC LIMIT ?";
    Statement statement(db_, sql.toUtf8());
    if (!statement.valid()) { if (error) *error = sqliteError(db_); return results; }
    int parameter = 1;
    for (const auto &binding : bindings) {
        if (binding.text) bindText(statement.get(), parameter++, binding.value);
        else sqlite3_bind_int64(statement.get(), parameter++, binding.number);
    }
    sqlite3_bind_int(statement.get(), parameter, qBound(1, limit, 3000));
    int step = SQLITE_ROW;
    while ((step = sqlite3_step(statement.get())) == SQLITE_ROW) {
        SearchResult entry;
        entry.file = recordFrom(statement.get());
        entry.score = sqlite3_column_double(statement.get(), 16);
        entry.snippet = columnText(statement.get(), 17);
        entry.matchPage = sqlite3_column_int(statement.get(), 18);
        const double confidence = sqlite3_column_double(statement.get(), 19);
        entry.documentTitle = columnText(statement.get(), 20);
        entry.documentAuthor = columnText(statement.get(), 21);
        entry.pageCount = sqlite3_column_int(statement.get(), 22);
        entry.cameraMake = columnText(statement.get(), 23);
        entry.cameraModel = columnText(statement.get(), 24);
        entry.imageWidth = sqlite3_column_int(statement.get(), 25);
        entry.imageHeight = sqlite3_column_int(statement.get(), 26);
        entry.dateTaken = sqlite3_column_int64(statement.get(), 27);
        entry.matchOrigin = "ocr";
        entry.scoreExplanation = QString("OCR +80, confidence +%1%2; total %3")
            .arg(confidence * 0.10, 0, 'f', 2).arg(query.phrase ? ", phrase +6" : "")
            .arg(entry.score, 0, 'f', 2);
        results.append(std::move(entry));
    }
    if (step != SQLITE_DONE && error) *error = sqliteError(db_);
    return results;
}

QVector<FileRecord> Database::pendingContent(const QStringList &extensions, int limit,
                                             QString *error) const
{
    QVector<FileRecord> files;
    if (extensions.isEmpty()) return files;
    QStringList placeholders;
    for (qsizetype i = 0; i < extensions.size(); ++i) placeholders << QStringLiteral("?");
    const QString sql =
        "SELECT id,name,path,parent_path,extension,type,size,mtime,ctime,inode,device,flags,is_dir,is_symlink,root,scan_generation "
        "FROM files WHERE content_state IN (0,1) AND is_dir=0 AND (flags&2)=0 AND extension IN ("
        + placeholders.join(',') + ") ORDER BY mtime DESC LIMIT ?";
    Statement statement(db_, sql.toUtf8());
    if (!statement.valid()) { if (error) *error = sqliteError(db_); return files; }
    int index = 1;
    for (const auto &extension : extensions) bindText(statement.get(), index++, extension.toLower());
    sqlite3_bind_int(statement.get(), index, qBound(1, limit, 1000));
    int step = SQLITE_ROW;
    while ((step = sqlite3_step(statement.get())) == SQLITE_ROW) files.append(recordFrom(statement.get()));
    if (step != SQLITE_DONE && error) *error = sqliteError(db_);
    return files;
}

bool Database::setContentState(qint64 fileId, ContentState state, const QString &extractor,
                               const QString &message, QString *error)
{
    Statement statement(db_, "UPDATE files SET content_state=?,content_extractor=?,content_error=? WHERE id=?");
    if (!statement.valid()) { if (error) *error = sqliteError(db_); return false; }
    sqlite3_bind_int(statement.get(), 1, static_cast<int>(state));
    bindText(statement.get(), 2, extractor);
    bindText(statement.get(), 3, message.left(512));
    sqlite3_bind_int64(statement.get(), 4, fileId);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) { if (error) *error = sqliteError(db_); return false; }
    return true;
}

bool Database::storeContent(const FileRecord &revision, const ExtractResult &result,
                            const QString &extractor, QString *error)
{
    if (!begin(error)) return false;
    if (!storeContentInTransaction(revision, result, extractor, error)) {
        rollback();
        return false;
    }
    if (!commit(error)) { rollback(); return false; }
    return true;
}

bool Database::storeContentBatch(const QVector<ContentUpdate> &updates, QString *error)
{
    if (updates.isEmpty()) return true;
    if (!begin(error)) return false;
    for (const auto &update : updates) {
        if (!storeContentInTransaction(update.revision, update.result, update.extractor, error)) {
            rollback();
            return false;
        }
    }
    if (!commit(error)) { rollback(); return false; }
    return true;
}

bool Database::storeContentInTransaction(const FileRecord &revision, const ExtractResult &result,
                                         const QString &extractor, QString *error)
{
    Statement revisionCheck(db_, "SELECT size,mtime,inode FROM files WHERE id=?");
    if (!revisionCheck.valid()) {
        if (error) *error = sqliteError(db_);
        return false;
    }
    sqlite3_bind_int64(revisionCheck.get(), 1, revision.id);
    if (sqlite3_step(revisionCheck.get()) != SQLITE_ROW
        || sqlite3_column_int64(revisionCheck.get(), 0) != revision.size
        || sqlite3_column_int64(revisionCheck.get(), 1) != revision.mtime
        || static_cast<quint64>(sqlite3_column_int64(revisionCheck.get(), 2)) != revision.inode) {
        return true; // File changed or vanished: discard this stale extraction.
    }
    bool ok = true;
    if (result.state == ContentState::Indexed) {
        Statement content(db_, "INSERT INTO content_documents(file_id,content) VALUES(?,?) "
                               "ON CONFLICT(file_id) DO UPDATE SET content=excluded.content");
        if (!content.valid()) ok = false;
        else {
            sqlite3_bind_int64(content.get(), 1, revision.id);
            const QString indexedText = QStringList{result.title, result.author, result.subject,
                result.keywords, result.text}.join('\n');
            bindText(content.get(), 2, indexedText);
            ok = sqlite3_step(content.get()) == SQLITE_DONE;
        }
    } else {
        Statement remove(db_, "DELETE FROM content_documents WHERE file_id=?");
        sqlite3_bind_int64(remove.get(), 1, revision.id);
        ok = remove.valid() && sqlite3_step(remove.get()) == SQLITE_DONE;
    }
    if (ok && (revision.extension == "pdf" || !result.pages.isEmpty())) {
        Statement removePages(db_, "DELETE FROM document_pages WHERE file_id=?");
        if (!removePages.valid()) ok = false;
        else {
            sqlite3_bind_int64(removePages.get(), 1, revision.id);
            ok = sqlite3_step(removePages.get()) == SQLITE_DONE;
        }
    }
    if (ok && !result.pages.isEmpty()) {
        Statement insertPage(db_, "INSERT INTO document_pages(file_id,page_number,content) VALUES(?,?,?)");
        if (!insertPage.valid()) ok = false;
        for (int index = 0; ok && index < result.pages.size(); ++index) {
            sqlite3_bind_int64(insertPage.get(), 1, revision.id);
            sqlite3_bind_int(insertPage.get(), 2, index + 1);
            bindText(insertPage.get(), 3, result.pages.at(index));
            ok = sqlite3_step(insertPage.get()) == SQLITE_DONE;
            sqlite3_reset(insertPage.get());
            sqlite3_clear_bindings(insertPage.get());
        }
    }
    if (ok) {
        Statement metadata(db_, "INSERT INTO document_metadata(file_id,title,author,subject,keywords,page_count,details_json,truncated,extracted_bytes,extraction_ms) "
            "VALUES(?,?,?,?,?,?,?,?,?,?) ON CONFLICT(file_id) DO UPDATE SET title=excluded.title,author=excluded.author,"
            "subject=excluded.subject,keywords=excluded.keywords,page_count=excluded.page_count,details_json=excluded.details_json,"
            "truncated=excluded.truncated,extracted_bytes=excluded.extracted_bytes,extraction_ms=excluded.extraction_ms");
        if (!metadata.valid()) ok = false;
        else {
            sqlite3_bind_int64(metadata.get(), 1, revision.id);
            bindText(metadata.get(), 2, result.title); bindText(metadata.get(), 3, result.author);
            bindText(metadata.get(), 4, result.subject); bindText(metadata.get(), 5, result.keywords);
            sqlite3_bind_int(metadata.get(), 6, result.pageCount); bindText(metadata.get(), 7, result.detailsJson);
            sqlite3_bind_int(metadata.get(), 8, result.truncated); sqlite3_bind_int64(metadata.get(), 9, result.bytesRead);
            sqlite3_bind_int64(metadata.get(), 10, result.extractionMs);
            ok = sqlite3_step(metadata.get()) == SQLITE_DONE;
        }
    }
    if (ok) {
        Statement update(db_, "UPDATE files SET content_state=?,content_extractor=?,content_error=?,"
            "content_revision_mtime=?,content_revision_size=?,content_revision_inode=?,content_updated_at=unixepoch() WHERE id=?");
        if (!update.valid()) ok = false;
        else {
            sqlite3_bind_int(update.get(), 1, static_cast<int>(result.state)); bindText(update.get(), 2, extractor);
            bindText(update.get(), 3, result.error.left(512)); sqlite3_bind_int64(update.get(), 4, revision.mtime);
            sqlite3_bind_int64(update.get(), 5, revision.size); sqlite3_bind_int64(update.get(), 6, revision.inode);
            sqlite3_bind_int64(update.get(), 7, revision.id);
            ok = sqlite3_step(update.get()) == SQLITE_DONE;
        }
    }
    if (ok && revision.extension == "pdf") {
        bool needsOcr = result.state != ContentState::Encrypted;
        if (!result.pages.isEmpty()) {
            needsOcr = false;
            for (const auto &page : result.pages) {
                if (!sufficientNativeText(page)) { needsOcr = true; break; }
            }
        }
        if (!needsOcr) {
            Statement removeOcr(db_, "DELETE FROM ocr_pages WHERE file_id=?");
            if (!removeOcr.valid()) ok = false;
            else {
                sqlite3_bind_int64(removeOcr.get(), 1, revision.id);
                ok = sqlite3_step(removeOcr.get()) == SQLITE_DONE;
            }
        }
        if (ok) {
            const OcrState ocrState = result.state == ContentState::Encrypted ? OcrState::Unsupported
                : needsOcr ? OcrState::Pending : OcrState::NotRequired;
            Statement ocr(db_, "UPDATE files SET ocr_state=?,ocr_error=?,"
                               "ocr_processed_pages=CASE WHEN ? THEN ocr_processed_pages ELSE 0 END,"
                               "ocr_total_pages=?,ocr_retry_count=CASE WHEN ? THEN ocr_retry_count ELSE 0 END WHERE id=?");
            if (!ocr.valid()) ok = false;
            else {
                sqlite3_bind_int(ocr.get(), 1, static_cast<int>(ocrState));
                bindText(ocr.get(), 2, result.state == ContentState::Encrypted ? "encrypted PDF" : QString());
                sqlite3_bind_int(ocr.get(), 3, needsOcr);
                sqlite3_bind_int(ocr.get(), 4, result.pageCount);
                sqlite3_bind_int(ocr.get(), 5, needsOcr);
                sqlite3_bind_int64(ocr.get(), 6, revision.id);
                ok = sqlite3_step(ocr.get()) == SQLITE_DONE;
            }
        }
    }
    if (!ok) {
        if (error) *error = sqliteError(db_);
        return false;
    }
    return true;
}

bool Database::resetContent(const QStringList &extensions, QString *error)
{
    if (!begin(error)) return false;
    if (!execute("DELETE FROM content_documents; DELETE FROM document_metadata; DELETE FROM document_pages;", error)) { rollback(); return false; }
    QString sql = "UPDATE files SET content_state=5,content_error='',content_extractor=''";
    if (!extensions.isEmpty()) {
        QStringList quoted;
        for (QString extension : extensions) quoted << "'" + extension.replace("'", "''") + "'";
        sql += "; UPDATE files SET content_state=0 WHERE is_dir=0 AND extension IN (" + quoted.join(',') + ')';
    }
    if (!execute(sql.toUtf8().constData(), error) || !commit(error)) { rollback(); return false; }
    return true;
}

bool Database::resetInterruptedContent(QString *error)
{
    return execute("UPDATE files SET content_state=1 WHERE content_state=2", error);
}

QHash<ContentState, qint64> Database::contentStateCounts(QString *error) const
{
    QHash<ContentState, qint64> counts;
    Statement statement(db_, "SELECT content_state,count(*) FROM files WHERE is_dir=0 GROUP BY content_state");
    if (!statement.valid()) { if (error) *error = sqliteError(db_); return counts; }
    int step = SQLITE_ROW;
    while ((step = sqlite3_step(statement.get())) == SQLITE_ROW)
        counts.insert(static_cast<ContentState>(sqlite3_column_int(statement.get(), 0)), sqlite3_column_int64(statement.get(), 1));
    if (step != SQLITE_DONE && error) *error = sqliteError(db_);
    return counts;
}

QVector<ExtractorMetrics> Database::contentExtractorMetrics(QString *error) const
{
    QVector<ExtractorMetrics> metrics;
    Statement statement(db_, R"SQL(
WITH ranked AS (
  SELECT f.content_extractor AS extractor,m.extraction_ms,m.extracted_bytes,
         row_number() OVER (PARTITION BY f.content_extractor ORDER BY m.extraction_ms) AS position,
         count(*) OVER (PARTITION BY f.content_extractor) AS sample_count
  FROM files f JOIN document_metadata m ON m.file_id=f.id
  WHERE f.content_extractor<>'' AND f.content_state IN (3,4)
)
SELECT extractor,count(*),avg(extraction_ms),
       max(CASE WHEN position=(sample_count*95+99)/100 THEN extraction_ms END),
       sum(extracted_bytes)
FROM ranked GROUP BY extractor ORDER BY extractor
)SQL");
    if (!statement.valid()) { if (error) *error = sqliteError(db_); return metrics; }
    int step = SQLITE_ROW;
    while ((step = sqlite3_step(statement.get())) == SQLITE_ROW) {
        ExtractorMetrics item;
        item.extractor = columnText(statement.get(), 0);
        item.documents = sqlite3_column_int64(statement.get(), 1);
        item.averageMilliseconds = sqlite3_column_double(statement.get(), 2);
        item.p95Milliseconds = sqlite3_column_int64(statement.get(), 3);
        item.bytesProcessed = sqlite3_column_int64(statement.get(), 4);
        metrics.append(std::move(item));
    }
    if (step != SQLITE_DONE && error) *error = sqliteError(db_);
    return metrics;
}

QVector<FileRecord> Database::pendingMetadata(int limit, QString *error) const
{
    QVector<FileRecord> files;
    Statement statement(db_,
        "SELECT id,name,path,parent_path,extension,type,size,mtime,ctime,inode,device,flags,is_dir,is_symlink,root,scan_generation "
        "FROM files WHERE metadata_state IN (0,1) AND is_dir=0 AND (flags&2)=0 "
        "ORDER BY mtime DESC LIMIT ?");
    if (!statement.valid()) { if (error) *error = sqliteError(db_); return files; }
    sqlite3_bind_int(statement.get(), 1, qBound(1, limit, 1000));
    int step = SQLITE_ROW;
    while ((step = sqlite3_step(statement.get())) == SQLITE_ROW) files.append(recordFrom(statement.get()));
    if (step != SQLITE_DONE && error) *error = sqliteError(db_);
    return files;
}

bool Database::setMetadataState(qint64 fileId, MetadataState state, const QString &message, QString *error)
{
    Statement statement(db_, "UPDATE files SET metadata_state=?,metadata_error=? WHERE id=?");
    if (!statement.valid()) { if (error) *error = sqliteError(db_); return false; }
    sqlite3_bind_int(statement.get(), 1, static_cast<int>(state));
    bindText(statement.get(), 2, message.left(512));
    sqlite3_bind_int64(statement.get(), 3, fileId);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) { if (error) *error = sqliteError(db_); return false; }
    return true;
}

bool Database::storeRichMetadata(const FileRecord &revision, const RichMetadata &metadata, QString *error)
{
    if (!begin(error)) return false;
    if (!storeRichMetadataInTransaction(revision, metadata, error)) { rollback(); return false; }
    if (!commit(error)) { rollback(); return false; }
    return true;
}

bool Database::storeRichMetadataBatch(const QVector<MetadataUpdate> &updates, QString *error)
{
    if (updates.isEmpty()) return true;
    if (!begin(error)) return false;
    for (const auto &update : updates) {
        if (!storeRichMetadataInTransaction(update.revision, update.metadata, error)) {
            rollback(); return false;
        }
    }
    if (!commit(error)) { rollback(); return false; }
    return true;
}

bool Database::storeRichMetadataInTransaction(const FileRecord &revision, const RichMetadata &metadata,
                                              QString *error)
{
    Statement revisionCheck(db_, "SELECT size,mtime,inode FROM files WHERE id=?");
    if (!revisionCheck.valid()) { if (error) *error = sqliteError(db_); return false; }
    sqlite3_bind_int64(revisionCheck.get(), 1, revision.id);
    if (sqlite3_step(revisionCheck.get()) != SQLITE_ROW
        || sqlite3_column_int64(revisionCheck.get(), 0) != revision.size
        || sqlite3_column_int64(revisionCheck.get(), 1) != revision.mtime
        || static_cast<quint64>(sqlite3_column_int64(revisionCheck.get(), 2)) != revision.inode) {
        return true;
    }
    const MetadataState state = metadata.width > 0 && metadata.height > 0
        ? MetadataState::Indexed : MetadataState::Failed;
    bool ok = true;
    if (state == MetadataState::Indexed) {
        Statement statement(db_, R"SQL(
INSERT INTO rich_metadata(file_id,camera_make,camera_model,date_taken,width,height)
VALUES(?,?,?,?,?,?) ON CONFLICT(file_id) DO UPDATE SET
 camera_make=excluded.camera_make,camera_model=excluded.camera_model,
 date_taken=excluded.date_taken,width=excluded.width,height=excluded.height
)SQL");
        if (!statement.valid()) ok = false;
        else {
            int column = 1;
            sqlite3_bind_int64(statement.get(), column++, revision.id);
            bindText(statement.get(), column++, metadata.cameraMake); bindText(statement.get(), column++, metadata.cameraModel);
            sqlite3_bind_int64(statement.get(), column++, metadata.dateTaken);
            sqlite3_bind_int(statement.get(), column++, metadata.width);
            sqlite3_bind_int(statement.get(), column++, metadata.height);
            ok = sqlite3_step(statement.get()) == SQLITE_DONE;
        }
    }
    if (ok) {
        Statement update(db_, "UPDATE files SET metadata_state=?,metadata_error=?,metadata_revision_mtime=?,"
                              "metadata_revision_size=?,metadata_revision_inode=?,metadata_updated_at=unixepoch() WHERE id=?");
        if (!update.valid()) ok = false;
        else {
            sqlite3_bind_int(update.get(), 1, static_cast<int>(state)); bindText(update.get(), 2, metadata.error.left(512));
            sqlite3_bind_int64(update.get(), 3, revision.mtime); sqlite3_bind_int64(update.get(), 4, revision.size);
            sqlite3_bind_int64(update.get(), 5, revision.inode); sqlite3_bind_int64(update.get(), 6, revision.id);
            ok = sqlite3_step(update.get()) == SQLITE_DONE;
        }
    }
    if (!ok) { if (error) *error = sqliteError(db_); return false; }
    return true;
}

QHash<MetadataState, qint64> Database::metadataStateCounts(QString *error) const
{
    QHash<MetadataState, qint64> counts;
    Statement statement(db_, "SELECT metadata_state,count(*) FROM files WHERE is_dir=0 GROUP BY metadata_state");
    if (!statement.valid()) { if (error) *error = sqliteError(db_); return counts; }
    int step = SQLITE_ROW;
    while ((step = sqlite3_step(statement.get())) == SQLITE_ROW)
        counts.insert(static_cast<MetadataState>(sqlite3_column_int(statement.get(), 0)), sqlite3_column_int64(statement.get(), 1));
    if (step != SQLITE_DONE && error) *error = sqliteError(db_);
    return counts;
}

bool Database::resetInterruptedMetadata(QString *error)
{
    return execute("UPDATE files SET metadata_state=1 WHERE metadata_state=2", error);
}

bool Database::resetImageMetadata(QString *error)
{
    return execute("DELETE FROM rich_metadata; UPDATE files SET metadata_state=CASE "
                   "WHEN is_dir=0 AND (type LIKE 'image/%' OR extension IN "
                   "('jpg','jpeg','png','webp','bmp','gif','tif','tiff','avif','heif','heic')) THEN 0 ELSE 4 END", error);
}

bool Database::recordOpen(qint64 fileId, QString *error)
{
    Statement statement(db_, "INSERT INTO usage_history(file_id,open_count,last_opened) VALUES(?,1,unixepoch()) "
                             "ON CONFLICT(file_id) DO UPDATE SET open_count=open_count+1,last_opened=unixepoch()");
    if (!statement.valid()) { if (error) *error = sqliteError(db_); return false; }
    sqlite3_bind_int64(statement.get(), 1, fileId);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) { if (error) *error = sqliteError(db_); return false; }
    return true;
}

bool Database::clearUsageHistory(QString *error)
{
    return execute("DELETE FROM usage_history", error);
}

QVector<FileRecord> Database::pendingOcr(bool pdfEnabled, bool imagesEnabled, bool waitForContent, int limit,
                                         QString *error) const
{
    QVector<FileRecord> files;
    QStringList kinds;
    if (pdfEnabled) kinds << (waitForContent
        ? "(f.extension='pdf' AND f.content_state NOT IN (0,1,2))"
        : "f.extension='pdf'");
    if (imagesEnabled) kinds << "(f.type LIKE 'image/%' AND f.extension IN ('jpg','jpeg','png','tif','tiff','webp'))";
    if (kinds.isEmpty()) return files;
    const QString sql =
        "SELECT id,name,path,parent_path,extension,type,size,mtime,ctime,inode,device,flags,is_dir,is_symlink,root,scan_generation "
        "FROM files f WHERE f.ocr_state IN (1,2,9) AND f.is_dir=0 AND (f.flags&2)=0 AND ("
        + kinds.join(" OR ") + ") ORDER BY f.mtime DESC LIMIT ?";
    Statement statement(db_, sql.toUtf8());
    if (!statement.valid()) { if (error) *error = sqliteError(db_); return files; }
    sqlite3_bind_int(statement.get(), 1, qBound(1, limit, 100));
    int step = SQLITE_ROW;
    while ((step = sqlite3_step(statement.get())) == SQLITE_ROW) files.append(recordFrom(statement.get()));
    if (step != SQLITE_DONE && error) *error = sqliteError(db_);
    return files;
}

bool Database::setOcrState(qint64 fileId, OcrState state, const QString &message, QString *error)
{
    Statement statement(db_, "UPDATE files SET ocr_state=?,ocr_error=?,ocr_updated_at=unixepoch() WHERE id=?");
    if (!statement.valid()) { if (error) *error = sqliteError(db_); return false; }
    sqlite3_bind_int(statement.get(), 1, static_cast<int>(state));
    bindText(statement.get(), 2, message.left(512));
    sqlite3_bind_int64(statement.get(), 3, fileId);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) { if (error) *error = sqliteError(db_); return false; }
    return true;
}

bool Database::beginOcr(const FileRecord &revision, const QString &languages, QString *error)
{
    Statement statement(db_, "UPDATE files SET ocr_state=3,ocr_error='',ocr_languages=?,"
                             "ocr_revision_mtime=?,ocr_revision_size=?,ocr_revision_inode=?,ocr_updated_at=unixepoch() "
                             "WHERE id=? AND size=? AND mtime=? AND inode=?");
    if (!statement.valid()) { if (error) *error = sqliteError(db_); return false; }
    int column = 1;
    bindText(statement.get(), column++, languages);
    sqlite3_bind_int64(statement.get(), column++, revision.mtime);
    sqlite3_bind_int64(statement.get(), column++, revision.size);
    sqlite3_bind_int64(statement.get(), column++, revision.inode);
    sqlite3_bind_int64(statement.get(), column++, revision.id);
    sqlite3_bind_int64(statement.get(), column++, revision.size);
    sqlite3_bind_int64(statement.get(), column++, revision.mtime);
    sqlite3_bind_int64(statement.get(), column++, revision.inode);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) { if (error) *error = sqliteError(db_); return false; }
    return sqlite3_changes(db_) == 1;
}

bool Database::storeOcrPage(const FileRecord &revision, const OcrPageResult &page,
                            const QString &languages, QString *error)
{
    if (!begin(error)) return false;
    Statement revisionCheck(db_, "SELECT 1 FROM files WHERE id=? AND size=? AND mtime=? AND inode=?");
    if (!revisionCheck.valid()) { rollback(); if (error) *error = sqliteError(db_); return false; }
    sqlite3_bind_int64(revisionCheck.get(), 1, revision.id);
    sqlite3_bind_int64(revisionCheck.get(), 2, revision.size);
    sqlite3_bind_int64(revisionCheck.get(), 3, revision.mtime);
    sqlite3_bind_int64(revisionCheck.get(), 4, revision.inode);
    if (sqlite3_step(revisionCheck.get()) != SQLITE_ROW) {
        rollback(); if (error) *error = "stale OCR revision"; return false;
    }
    bool ok = true;
    if (!page.nativeText && !page.text.trimmed().isEmpty()) {
        Statement insert(db_, "INSERT INTO ocr_pages(file_id,page_number,text,confidence) VALUES(?,?,?,?) "
                              "ON CONFLICT(file_id,page_number) DO UPDATE SET text=excluded.text,confidence=excluded.confidence");
        if (!insert.valid()) ok = false;
        else {
            sqlite3_bind_int64(insert.get(), 1, revision.id);
            sqlite3_bind_int(insert.get(), 2, page.pageNumber);
            bindText(insert.get(), 3, page.text);
            sqlite3_bind_double(insert.get(), 4, page.confidence);
            ok = sqlite3_step(insert.get()) == SQLITE_DONE;
        }
    }
    if (ok) {
        Statement update(db_, "UPDATE files SET ocr_processed_pages=max(ocr_processed_pages,?),"
                              "ocr_total_pages=max(ocr_total_pages,?),ocr_languages=?,ocr_updated_at=unixepoch() WHERE id=?");
        if (!update.valid()) ok = false;
        else {
            sqlite3_bind_int(update.get(), 1, page.pageNumber);
            sqlite3_bind_int(update.get(), 2, page.totalPages);
            bindText(update.get(), 3, languages);
            sqlite3_bind_int64(update.get(), 4, revision.id);
            ok = sqlite3_step(update.get()) == SQLITE_DONE;
        }
    }
    if (!ok) { if (error) *error = sqliteError(db_); rollback(); return false; }
    if (!commit(error)) { rollback(); return false; }
    return true;
}

bool Database::finishOcr(const FileRecord &revision, OcrState state, int totalPages,
                         const QString &message, QString *error)
{
    Statement statement(db_, "UPDATE files SET ocr_state=?,ocr_error=?,ocr_total_pages=max(ocr_total_pages,?),"
                             "ocr_revision_mtime=?,ocr_revision_size=?,ocr_revision_inode=?,ocr_engine_version=?,ocr_updated_at=unixepoch() "
                             "WHERE id=? AND size=? AND mtime=? AND inode=?");
    if (!statement.valid()) { if (error) *error = sqliteError(db_); return false; }
    int column = 1;
    sqlite3_bind_int(statement.get(), column++, static_cast<int>(state));
    bindText(statement.get(), column++, message.left(512));
    sqlite3_bind_int(statement.get(), column++, totalPages);
    sqlite3_bind_int64(statement.get(), column++, revision.mtime);
    sqlite3_bind_int64(statement.get(), column++, revision.size);
    sqlite3_bind_int64(statement.get(), column++, revision.inode);
    sqlite3_bind_int(statement.get(), column++, OcrPipelineVersion);
    sqlite3_bind_int64(statement.get(), column++, revision.id);
    sqlite3_bind_int64(statement.get(), column++, revision.size);
    sqlite3_bind_int64(statement.get(), column++, revision.mtime);
    sqlite3_bind_int64(statement.get(), column++, revision.inode);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) { if (error) *error = sqliteError(db_); return false; }
    return sqlite3_changes(db_) == 1;
}

bool Database::failOcr(const FileRecord &revision, const QString &message, QString *error)
{
    Statement statement(db_, "UPDATE files SET ocr_retry_count=ocr_retry_count+1,"
                             "ocr_state=CASE WHEN ocr_retry_count+1<=2 THEN 1 ELSE 6 END,"
                             "ocr_error=?,ocr_updated_at=unixepoch() WHERE id=? AND size=? AND mtime=? AND inode=?");
    if (!statement.valid()) { if (error) *error = sqliteError(db_); return false; }
    bindText(statement.get(), 1, message.left(512));
    sqlite3_bind_int64(statement.get(), 2, revision.id);
    sqlite3_bind_int64(statement.get(), 3, revision.size);
    sqlite3_bind_int64(statement.get(), 4, revision.mtime);
    sqlite3_bind_int64(statement.get(), 5, revision.inode);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) { if (error) *error = sqliteError(db_); return false; }
    return true;
}

bool Database::resetInterruptedOcr(QString *error)
{
    return execute("UPDATE files SET ocr_retry_count=ocr_retry_count+1,ocr_state="
                   "CASE WHEN ocr_retry_count+1<=2 THEN 1 ELSE 6 END,"
                   "ocr_error='OCR worker interrupted' WHERE ocr_state=3", error);
}

bool Database::resetOcr(bool includeIndexed, QString *error)
{
    if (!begin(error)) return false;
    const char *remove = includeIndexed ? "DELETE FROM ocr_pages"
        : "DELETE FROM ocr_pages WHERE file_id IN (SELECT id FROM files WHERE ocr_state IN (5,6,7,8))";
    if (!execute(remove, error)) { rollback(); return false; }
    const char *update = includeIndexed
        ? "UPDATE files SET ocr_state=CASE WHEN extension='pdf' AND content_state=6 THEN 7 WHEN extension='pdf' THEN 1 ELSE 0 END,"
          "ocr_error=CASE WHEN extension='pdf' AND content_state=6 THEN 'encrypted PDF' ELSE '' END,ocr_processed_pages=0,ocr_retry_count=0,ocr_languages=''"
        : "UPDATE files SET ocr_state=CASE WHEN extension='pdf' AND content_state=6 THEN 7 WHEN extension='pdf' THEN 1 ELSE 0 END,"
          "ocr_error=CASE WHEN extension='pdf' AND content_state=6 THEN 'encrypted PDF' ELSE '' END,ocr_processed_pages=0,ocr_retry_count=0 WHERE ocr_state IN (5,6,7,8)";
    if (!execute(update, error) || !commit(error)) { rollback(); return false; }
    return true;
}

bool Database::applyOcrPolicy(bool pdfEnabled, bool imagesEnabled, QString *error)
{
    if (!begin(error)) return false;
    const QString images = "type LIKE 'image/%' AND extension IN ('jpg','jpeg','png','tif','tiff','webp')";
    // Unsupported is also used when a language pack was temporarily absent.
    // Requeue only that specific reason when OCR becomes available again;
    // genuine unsupported/encrypted files remain untouched.
    const QString retryMissingLanguage = "(ocr_state<>7 OR ocr_error='OCR language pack not found')";
    QStringList statements;
    if (pdfEnabled)
        statements << "UPDATE files SET ocr_state=1,ocr_error='' WHERE is_dir=0 AND extension='pdf' AND ocr_state IN (0,7,9) "
                      "AND " + retryMissingLanguage + " "
                      "AND content_state<>6 AND (content_state<>3 OR EXISTS(SELECT 1 FROM document_pages p WHERE p.file_id=files.id AND length(trim(p.content))<32))";
    else
        statements << "UPDATE files SET ocr_state=0 WHERE extension='pdf' AND ocr_state IN (1,2,9)";
    if (imagesEnabled)
        statements << "UPDATE files SET ocr_state=1,ocr_error='' WHERE is_dir=0 AND " + images
                      + " AND ocr_state IN (0,7,9) AND " + retryMissingLanguage;
    else
        statements << "UPDATE files SET ocr_state=0 WHERE " + images + " AND ocr_state IN (1,2,9)";
    if (!execute(statements.join(';').toUtf8().constData(), error) || !commit(error)) { rollback(); return false; }
    return true;
}

bool Database::reuseOcrForHardlink(const FileRecord &revision, const QString &languages, QString *error)
{
    Statement source(db_, "SELECT id FROM files WHERE id<>? AND device=? AND inode=? AND size=? AND mtime=? "
                          "AND ocr_state=4 AND ocr_languages=? AND ocr_engine_version=? LIMIT 1");
    if (!source.valid()) { if (error) *error = sqliteError(db_); return false; }
    sqlite3_bind_int64(source.get(), 1, revision.id);
    sqlite3_bind_int64(source.get(), 2, revision.device);
    sqlite3_bind_int64(source.get(), 3, revision.inode);
    sqlite3_bind_int64(source.get(), 4, revision.size);
    sqlite3_bind_int64(source.get(), 5, revision.mtime);
    bindText(source.get(), 6, languages);
    sqlite3_bind_int(source.get(), 7, OcrPipelineVersion);
    if (sqlite3_step(source.get()) != SQLITE_ROW) return false;
    const qint64 sourceId = sqlite3_column_int64(source.get(), 0);
    if (!begin(error)) return false;
    Statement copy(db_, "INSERT INTO ocr_pages(file_id,page_number,text,confidence) "
                        "SELECT ?,page_number,text,confidence FROM ocr_pages WHERE file_id=?");
    bool ok = copy.valid();
    if (ok) {
        sqlite3_bind_int64(copy.get(), 1, revision.id);
        sqlite3_bind_int64(copy.get(), 2, sourceId);
        ok = sqlite3_step(copy.get()) == SQLITE_DONE;
    }
    Statement update(db_, "UPDATE files SET ocr_state=4,ocr_error='',"
                          "ocr_processed_pages=(SELECT ocr_processed_pages FROM files WHERE id=?),"
                          "ocr_total_pages=(SELECT ocr_total_pages FROM files WHERE id=?),"
                          "ocr_languages=?,ocr_engine_version=?,ocr_updated_at=unixepoch() WHERE id=?");
    if (ok && update.valid()) {
        sqlite3_bind_int64(update.get(), 1, sourceId);
        sqlite3_bind_int64(update.get(), 2, sourceId);
        bindText(update.get(), 3, languages);
        sqlite3_bind_int(update.get(), 4, OcrPipelineVersion);
        sqlite3_bind_int64(update.get(), 5, revision.id);
        ok = sqlite3_step(update.get()) == SQLITE_DONE;
    } else ok = false;
    if (!ok) { if (error) *error = sqliteError(db_); rollback(); return false; }
    if (!commit(error)) { rollback(); return false; }
    return true;
}

QHash<OcrState, qint64> Database::ocrStateCounts(QString *error) const
{
    QHash<OcrState, qint64> counts;
    Statement statement(db_, "SELECT ocr_state,count(*) FROM files WHERE is_dir=0 GROUP BY ocr_state");
    if (!statement.valid()) { if (error) *error = sqliteError(db_); return counts; }
    int step = SQLITE_ROW;
    while ((step = sqlite3_step(statement.get())) == SQLITE_ROW)
        counts.insert(static_cast<OcrState>(sqlite3_column_int(statement.get(), 0)), sqlite3_column_int64(statement.get(), 1));
    if (step != SQLITE_DONE && error) *error = sqliteError(db_);
    return counts;
}

qint64 Database::ocrPageCount(QString *error) const
{
    Statement statement(db_, "SELECT coalesce(sum(ocr_processed_pages),0) FROM files");
    if (!statement.valid() || sqlite3_step(statement.get()) != SQLITE_ROW) {
        if (error) *error = sqliteError(db_);
        return -1;
    }
    return sqlite3_column_int64(statement.get(), 0);
}

bool Database::movePathPreservingContent(const QString &oldPath, const FileRecord &record,
                                         bool directory, QString *error)
{
    if (directory) {
        Statement statement(db_, "UPDATE files SET path=?||substr(path,length(?)+1),"
            "parent_path=CASE WHEN parent_path=? THEN ? ELSE ?||substr(parent_path,length(?)+1) END,"
            "name=CASE WHEN path=? THEN ? ELSE name END WHERE path=? OR path LIKE ? ESCAPE '\\'");
        if (!statement.valid()) { if (error) *error = sqliteError(db_); return false; }
        int i = 1;
        bindText(statement.get(), i++, record.path); bindText(statement.get(), i++, oldPath);
        bindText(statement.get(), i++, QFileInfo(oldPath).path()); bindText(statement.get(), i++, record.parentPath);
        bindText(statement.get(), i++, record.path); bindText(statement.get(), i++, oldPath);
        bindText(statement.get(), i++, oldPath); bindText(statement.get(), i++, record.name);
        bindText(statement.get(), i++, oldPath); bindText(statement.get(), i++, escapedLike(oldPath) + "/%");
        return sqlite3_step(statement.get()) == SQLITE_DONE;
    }
    Statement statement(db_, "UPDATE files SET name=?,path=?,parent_path=?,extension=?,type=?,flags=?,root=?,scan_generation=?,updated_at=unixepoch() "
        "WHERE path=? AND device=? AND inode=? AND size=? AND mtime=?");
    if (!statement.valid()) { if (error) *error = sqliteError(db_); return false; }
    int i = 1;
    bindText(statement.get(), i++, record.name); bindText(statement.get(), i++, record.path);
    bindText(statement.get(), i++, record.parentPath); bindText(statement.get(), i++, record.extension);
    bindText(statement.get(), i++, record.type); sqlite3_bind_int(statement.get(), i++, record.hidden ? static_cast<int>(Hidden) : 0);
    bindText(statement.get(), i++, record.root); sqlite3_bind_int64(statement.get(), i++, record.scanGeneration);
    bindText(statement.get(), i++, oldPath); sqlite3_bind_int64(statement.get(), i++, record.device);
    sqlite3_bind_int64(statement.get(), i++, record.inode); sqlite3_bind_int64(statement.get(), i++, record.size);
    sqlite3_bind_int64(statement.get(), i++, record.mtime);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) { if (error) *error = sqliteError(db_); return false; }
    if (sqlite3_changes(db_) == 0) {
        if (!removePath(oldPath, false, error)) return false;
        return upsert(record, error);
    }
    return true;
}

bool Database::optimize(QString *error) { return execute("PRAGMA optimize", error); }

bool Database::quickCheck(QString *error) const
{
    Statement statement(db_, "PRAGMA quick_check(1)");
    if (!statement.valid() || sqlite3_step(statement.get()) != SQLITE_ROW) {
        if (error) *error = sqliteError(db_);
        return false;
    }
    const QString result = columnText(statement.get(), 0);
    if (result != "ok") {
        if (error) *error = result.isEmpty() ? QStringLiteral("database integrity check failed") : result;
        return false;
    }
    return true;
}

bool Database::checkpointWal(QString *error)
{
    const int result = sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_PASSIVE,
                                                  nullptr, nullptr);
    if (result != SQLITE_OK && result != SQLITE_BUSY) {
        if (error) *error = sqliteError(db_);
        return false;
    }
    return true;
}

qint64 Database::fileCount(QString *error) const
{
    Statement statement(db_, "SELECT count(*) FROM files");
    if (!statement.valid() || sqlite3_step(statement.get()) != SQLITE_ROW) {
        if (error) *error = sqliteError(db_);
        return -1;
    }
    return sqlite3_column_int64(statement.get(), 0);
}

qint64 Database::databaseSize() const
{
    return QFileInfo(path_).size() + QFileInfo(path_ + "-wal").size();
}

} // namespace purrfind
