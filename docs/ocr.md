# Local OCR

OCR is an optional third indexing layer. Filesystem metadata is committed first,
native document text remains the next background stage, and OCR is scheduled
last. Processing is performed entirely on the local computer; no page, image,
recognized text, or metadata is sent to a service.

`purrfind-indexer` owns the persistent queue but never loads Tesseract. It starts
one `purrfind-ocr-worker` process per document. The worker uses the native
Tesseract API and sends bounded JSON events per page to the indexer. Each page is
revision-checked and committed atomically, so partial progress is searchable and
a changed or deleted file cannot activate stale text.

Scanned PDFs are rendered with Poppler at 200 DPI. Each page is checked for a
sufficient native text layer first; only pages with fewer than 32 useful
characters or mostly non-text data are rendered for OCR. Hybrid PDFs therefore
retain good native text and OCR only scanned pages. Originals are never modified
and no replacement PDF is created.

JPEG, PNG, TIFF, and WebP images are supported when their Qt decoder is present.
PDF OCR defaults on; image OCR defaults off. `source:ocr` (also `match:ocr`)
restricts a query to recognized text. Results carry an OCR badge, highlighted
snippet, confidence-adjusted score, and PDF page number for the existing preview.

Tesseract language files are discovered locally. A pt-BR locale initially
selects `por+eng` when available; other locales start with `eng`. Settings show
only installed packs and never download data. Distribution package names vary,
but commonly resemble `tesseract-data-eng` and `tesseract-data-por`.
