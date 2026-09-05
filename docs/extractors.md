# Extractors

`ExtractorRegistry` selects one implementation from metadata, enabled types,
and validated container input:

- `PlainTextExtractor`: TXT, Markdown, UTF-8 BOM, UTF-8, and ASCII. NUL/control
  sampling rejects mislabeled binary data; malformed UTF-8 fails without looping.
- `PdfExtractor`: Poppler-Qt6 text extraction plus title, author, subject,
  keywords, and page count. It never renders pages. Locked documents become
  `ENCRYPTED`; image-only PDFs become `NO_TEXT` for a future OCR pipeline.
- `OfficeExtractor`: libzip/libxml2 in-memory reads of DOCX main text, headers,
  footers, notes/comments; XLSX shared/inline strings, worksheets and sheet
  names; PPTX slides and speaker notes.
- `OpenDocumentExtractor`: ODT, ODS, and ODP `content.xml` plus metadata from
  `meta.xml`.

No extractor invokes LibreOffice, `pdftotext`, `unzip`, shell tools, network
services, or OCR. New internal implementations only need to satisfy the
`ContentExtractor` contract and register themselves.

