# Database schema

Schema version 6 migrates every prior database in place. Existing `files`,
content, and metadata rows remain searchable while background work continues.

```text
files
 ├── Phase 1 metadata and files_fts (trigram)
 └── content state, extractor, error, revision, updated_at
      ├── content_documents (full normalized text)
      │    └── content_fts (unicode61 external-content FTS5)
      └── document_metadata (title, author, pages, details, metrics)
 ├── rich_metadata (searchable camera, dimensions, capture date)
 ├── document_pages → document_page_fts (PDF match-page lookup)
 └── usage_history (optional PurrFind-only open count and last-opened time)
 └── OCR state, revision, progress, retry count, languages, and error
      └── ocr_pages (page number, text, confidence)
           └── ocr_fts (unicode61 external-content FTS5)
```

Foreign keys and deletion triggers remove document text, FTS postings, and
metadata with their file. Metadata-change triggers delete stale content. FTS
tables use external-content triggers so `snippet()` works without loading source
documents. WAL and prepared statements remain unchanged.

The XDG data directory is mode 0700 and the database mode 0600 when supported.
WAL/SHM inherit owner-only database creation permissions.

Schema v3 adds persistent metadata states and revision columns. Image metadata
is invalidated by size/mtime/inode changes. Preview bitmaps are deliberately
absent from SQLite and live in the disposable XDG cache.

Schema v4 adds the persistent OCR state machine and page-granular text. A page
transaction verifies file size/mtime/inode before replacing its row, updates
progress, and immediately activates its FTS posting. File deletion cascades to
OCR pages; revision changes delete them and schedule eligible PDFs again.
Finished hardlinks may reuse rows only when device, inode, revision, and OCR
languages agree. No rendered bitmap or Tesseract coordinate data is stored.

WAL permits concurrent readers while writers use short transactions.
`PRAGMA optimize` runs when queues drain and passive checkpoints bound WAL growth.
A lightweight `quick_check(1)` runs at most weekly. On corruption, the database
and sidecars move intact to `recovery/index-TIMESTAMP`, a clean schema is created,
and roots reconcile. Manual rebuild uses the same path and keeps configuration.
Only the three newest recovery snapshots are retained to prevent unbounded disk
growth.

Schema v5 retokenizes document and OCR FTS indexes so fragments separated by
hyphens and underscores remain searchable. Schema v6 records the OCR pipeline
version and invalidates image OCR produced by older preprocessing. Eligible
images are then reprocessed automatically under the current OCR policy; PDF
results remain intact because their pipeline did not change.
