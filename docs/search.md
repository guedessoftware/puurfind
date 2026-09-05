# Search

The current schema stores one row per path with parent, extension, basic MIME type,
size, timestamps, inode/device, directory/symlink/hidden/offline flags, root, and
scan generation. `PRAGMA user_version` drives migrations. WAL, NORMAL synchronous
mode, a 32 MiB cache, memory temp storage, and a conservative 256 MiB mmap ceiling
balance latency and durability.

An external-content FTS5 table indexes `name` and `path` using SQLite's
case-insensitive trigram tokenizer. Triggers keep it synchronized. Queries of
three or more characters use FTS; one- and two-character inputs use an indexed
name-prefix lookup to avoid a million-row substring scan. Every
user value is bound to a prepared statement, including escaped FTS phrases.

Ranking prioritizes exact names, name prefixes, name substrings, then path
matches, with small recent-file and directory bonuses. Structured filters are
parsed separately from free text:

- `type:pdf` or `ext:pdf`
- `folder:Documents` or `in:Documents`
- `modified:today`, `modified:7d`, `modified:30d`
- `size:>10MB` or `size:<1GB`
- `kind:file`, `kind:folder`
- `author:João`, `camera:Canon`
- `pages:>20`, `width:>3000`, `height:<5000`

The category UI adds a documented internal `category:` filter for images,
documents, videos, and other files.

Schema v2 adds a separate Unicode content FTS using `unicode61` and
`remove_diacritics=2`. Schema v5 treats `-` and `_` as separators so both a
complete technical identifier and a component such as `d81f94be` from
`ZTEG-d81f94be` are searchable. Ordinary terms are combined with AND; quoted
text becomes an FTS phrase. IP, IPv6, MAC, ASN, and equipment identifiers are
covered by tests.

Default search merges name/path and content candidates. Metadata matches retain
scores of 180–1000 while content starts around 120, so a weak paragraph mention
cannot outrank a strong filename. `name:`, `path:`, and `content:` restrict scope.
Content results use FTS5 `snippet()` markers; QML escapes document text before
turning those markers into violet highlights.

Schema v3 keeps the same search indexes. Metadata filters add indexed joins
only when a metadata field is present. Empty local-use history also avoids its
join entirely. This preserves the normal filename/content fast paths.

Schema v4 adds a separate external-content `ocr_fts` table over page text. Its
tokenizer matches native content, including accent folding and the schema-v5
technical-identifier component behavior. Unified search merges OCR candidates after native
content; `source:ocr` or `match:ocr` selects OCR alone, while `source:native`
excludes it. Results retain the matching page, confidence, and a highlighted
stored-text snippet, so search never invokes Tesseract.

OCR starts near score 80 and receives a small confidence term, phrase bonus,
FTS relevance, and bounded usage bonus. Native content starts near 120 and
strong name/path matches remain at 180–1000. Low-confidence recognition can
therefore be useful without overtaking an exact filename.
