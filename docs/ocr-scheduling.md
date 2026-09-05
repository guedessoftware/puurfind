# OCR scheduling

The persistent OCR state machine is `NOT_REQUIRED`, `PENDING`, `QUEUED`,
`PROCESSING`, `INDEXED`, `NO_TEXT`, `FAILED`, `UNSUPPORTED`, `SKIPPED`, and
`PAUSED`. Processing state, revision, retry count, languages, page progress, and
errors survive logout, restart, or worker failure.

Default policy:

- scanned/hybrid PDF OCR enabled;
- image OCR enabled;
- one worker process and one OpenMP thread;
- process nice level 19 and Linux idle I/O class;
- 200 DPI, at most 100 pages and 500 MiB per PDF;
- reduce to the low profile on battery and pause below 30%;
- yield while system load exceeds 1.25 times the logical CPU count;
- 90-second watchdog per page and at most two retries.

Normal and High profiles are explicit choices and permit two or at most four
Tesseract threads, while still processing one document at a time. Search and
interactive preview remain in their normal-priority processes/threads. The
queue can be paused, resumed, reprocessed, and given OCR-only path exclusions.
Changing languages prompts whether existing recognized text should be kept or
reprocessed.

PDFs wait for the native content queue when PDF extraction is enabled, avoiding
duplicate Poppler work. Hardlinks with matching device, inode, revision, and
languages reuse indexed OCR without hashing large files. A rename preserving
that identity keeps OCR; size, mtime, or inode changes delete stale pages and
queue the new revision.
