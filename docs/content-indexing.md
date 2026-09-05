# Content indexing

Content indexing is a second, independent layer. Filesystem metadata is committed
first and is immediately searchable. A single low-priority worker then rebuilds
its queue from persistent database states and processes recent documents first.
No thread is created per document, the worker runs at nice level 10, sleeps
between documents, and can be paused without affecting filename search.

States are numeric and stable: `NOT_INDEXED`, `QUEUED`, `INDEXING`, `INDEXED`,
`NO_TEXT`, `UNSUPPORTED`, `ENCRYPTED`, `FAILED`, and `TOO_LARGE`. Interrupted
`INDEXING` rows become `QUEUED` on startup. The database itself is the queue;
there is no fragile sidecar file.

The default file limit is 100 MiB and normalized extracted text is capped at
8 MiB per document. TXT/Markdown are read in 64 KiB chunks. Archive entries,
total expansion, entry count, and compression ratios have independent limits.
FTS writes from up to 16 completed extractions are committed together.

Every job carries size, mtime, and inode. A filesystem change immediately
invalidates and removes old text; the worker is cancelled where possible. A
final revision check prevents stale output from being committed even if a race
occurs. Same-device/inode renames update the existing row and preserve content.

Settings expose enable/disable, individual formats, maximum size, content-only
directory exclusions, pause/resume, and content-only reindex. `PRAGMA optimize`
runs after the queue drains; automatic `VACUUM` is deliberately avoided.

The `ContentMetrics` D-Bus method computes per-extractor document count,
average extraction time, p95 extraction time, and processed bytes on demand.
It is intentionally separate from the frequently refreshed status call so
collecting development metrics cannot slow the UI.
