# Changelog

## [0.5.0-rc2] - 2026-09-04

### Changed

- The preview is now a tall, dedicated document canvas; inline technical
  metadata is hidden behind Details and extracted text expands only on request.
- The category strip, brand header, and keyboard/status footer now match the
  compact visual language of the reference interface, including live counts
  and measured search latency.
- Quick and advanced filters moved to an on-demand popover so they no longer
  consume preview height.
- Result rows now enforce independent text and metadata columns, preventing
  long document snippets and OCR badges from overlapping size, type, or date.
  Row reuse, bounded scrolling, consistent separators, and fixed icon/metadata
  widths keep large result lists stable and aligned.
- Video preview now uses Qt Multimedia for in-panel MP4/WebM playback with
  play/pause, seeking, duration, and mute controls. Document text binds to the
  viewport width for correct wrapping, and PDF/image previews render for the
  taller canvas instead of the former landscape-sized target.
- A persistent system-tray indicator now exposes Open, shortcut status, and
  Quit actions. X11 sessions use a native global-key grab for reliable
  `Super+F`; Wayland retains the desktop-portal implementation.
- The autostart shortcut listener now loads the Qt/QML interface only on demand
  and releases it after 60 seconds hidden, substantially reducing resident use.
- Recursive inotify setup and configuration changes are incremental and no
  longer monopolize the indexer's D-Bus event loop.
- Image OCR results carry a pipeline version; upgraded preprocessing causes an
  automatic image-only refresh while preserving the rest of the index.
- Empty searches no longer issue broad database queries, filter chips replace
  conflicting values, and tab labels no longer display partial-page counts.

### Fixed

- Recovery snapshots are limited to the three newest copies.
- The systemd indexer unit is limited to Unix sockets and the Arch package
  declares its icon-theme runtime dependency.

## [0.5.0-rc1] - 2026-09-04

### Added

- Weekly lightweight database integrity checking, automatic preservation/rebuild, and a user-facing rebuild action.
- Ubuntu LTS, Debian stable, Fedora, Arch, feature-matrix, sanitizer, install, and performance-gate CI jobs.
- Stable `io.github.guedessoftware.PurrFind` identity, scalable icon, D-Bus activation, CPack DEB/RPM/source configuration, Arch recipe, deterministic source release script, and checksums.
- Query-parser fuzz corpus, unusual path coverage, interrupted-transaction/WAL checks, and a 35,000-event coalescing/overflow simulation.

### Changed

- At most one search request remains in flight while typing; only the newest pending query is dispatched.
- UI status updates are signal-driven after connection instead of polling every 1.5 seconds.
- The overlay is positioned inside the available geometry of the screen containing the pointer.
- The indexer unit now has restart-storm protection, a stop timeout, owner-only umask, and compatible systemd hardening.

### Fixed

- inotify watch exhaustion is no longer silent and is visible in status/settings.
- Indexer shutdown now explicitly drains worker ownership before closing SQLite.
- Content and OCR searches now match components after hyphens or underscores,
  such as `d81f94be` inside `ZTEG-d81f94be`, via a preserving schema-v5 migration.

## [0.4.0] - 2026-09-04

### Added

- Entirely local Tesseract OCR for scanned/hybrid PDFs and JPEG, PNG, TIFF, and WebP images (enabled by default; can be disabled in Settings).
- Isolated per-document OCR worker, persistent page-level queue, watchdog, bounded retries, battery/load-aware scheduling, and language-pack discovery.
- Schema-v4 OCR pages/FTS, progressive results, confidence-aware ranking, `source:ocr`, page-aware previews, status controls, tests, and benchmarks.

### Security

- Owner-only OCR index data, in-memory page rendering, worker memory/image limits, disabled core dumps, and no network or recognized-text logging.

## [0.3.0] - 2026-09-04

### Added
- Asynchronous, cancellable selected-item preview registry for images, PDF, text, Office/ODF, folders, and generic files.
- EXIF backfill for searchable camera, dimensions, and capture date; orientation, lens, exposure, ISO, and local GPS remain on-demand.
- Schema-v3 metadata filters, PDF match-page index, local usage history, score explanations, and preview benchmarks.
- 96 MiB RAM LRU and 256 MiB revision-keyed disk thumbnail cache under XDG cache.

### Changed
- Ranking distinguishes exact/prefix file matches from folders and applies a tightly bounded optional local-use bonus.
- Result icons now use the system MIME theme with an internal painted fallback.

All notable changes to PurrFind are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and Semantic
Versioning.

## [0.2.0] - 2026-09-04

### Added

- Background full-text indexing for plain text, Markdown, PDF, OOXML, and OpenDocument files.
- Secure extractor registry backed by Poppler-Qt6, libzip, and libxml2.
- Persistent content states, document metadata, Unicode FTS5, snippets, scopes, and phrase search.
- Pause/resume, content-only reindexing, per-type settings, progress metrics, and content benchmarks.

### Changed

- Unified ranking now combines filename, path, and lower-priority content matches.
- Database schema migrates automatically from version 1 to version 2.

### Security

- Owner-only XDG storage permissions and bounded ZIP/XML/text processing.

## [0.1.0] - 2026-09-04

### Added

- Native Qt 6/QML search overlay and settings UI.
- Independent user indexer with initial crawling, inotify updates, and D-Bus IPC.
- SQLite WAL database, versioned migrations, trigram FTS5 search, filters, and ranking.
- XDG configuration/data locations and configurable roots and exclusions.
- Portal-based global shortcut, keyboard-first controls, previews, and file actions.
- Unit/integration tests, synthetic benchmark, systemd user unit, and packaging metadata.

### Security

- Fully local operation with prepared SQL statements and no telemetry or network access.
