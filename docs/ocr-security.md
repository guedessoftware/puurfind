# OCR security and privacy

OCR runs in a disposable child process rather than the indexer or UI. A malformed
PDF, codec fault, Tesseract failure, timeout, or excessive memory use therefore
fails one job. The parent terminates a worker after 90 seconds without a page
event and retries no more than twice.

The worker disables core dumps, has a 1.5 GiB virtual-address ceiling, runs at
nice 19 and Linux idle I/O priority, and exits after every document. PDF render
buffers are capped at 50 megapixels. Image decoding rejects dimensions above
32,768 pixels, 100 megapixels, or Qt's 256 MiB allocation limit and downsizes
very large sources before recognition.

Pages are rendered in memory. No temporary page images accumulate, no external
thumbnailer or command-line Tesseract process is used per page, and PDFs are not
rewritten. Poppler and Tesseract receive no network facility from PurrFind.
Recognized text is never written to logs.

OCR text can be sensitive. It lives in the same owner-only XDG SQLite database
as native full text: directory mode 0700 and database/cache files mode 0600 when
supported. OCR-specific exclusions let users keep selected folders out of this
layer while retaining filename discovery.
