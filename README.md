# PurrFind

**Fast, native file and content search for Linux.**

It keeps a private local index and searches filenames, paths, and document
contents as you type. Version 0.5.0-rc2 stabilizes the architecture for a first
public beta: recovery, hardening, cross-distribution CI, native packaging, and
performance gates build on the entirely local OCR introduced in version 0.4.
OCR runs strictly in the background
for scanned PDFs and, when enabled, images—without delaying filename or native
full-text availability.

## Highlights

- Instant local search backed by SQLite FTS5 trigram indexes
- Full-text document search with snippets and phrase/scoped queries
- Local OCR for scanned and hybrid PDFs, with optional JPEG/PNG/TIFF/WebP OCR
- Rich selected-item previews for images, PDFs, text, Office/ODF, folders, and
  local MP4/WebM video playback
- Local EXIF search, PDF match-page previews, and optional usage-aware ranking
- Low-resource, event-driven indexing with batched writes
- Native Qt 6/QML interface for Wayland and X11
- Resident system-tray status with open/quit actions and a reliable native
  `Super+F` listener on X11 (portal fallback on Wayland)
- Local-only operation: no accounts, telemetry, uploads, or network calls
- Filters for extension, folder, modification date, size, and category
- Versioned database migrations and distribution-friendly CMake installs

## Build

Required packages are a C++20 compiler, CMake 3.24+, Ninja, Qt 6.4+ (Core,
DBus, Gui, Multimedia, Qml, Quick, and Quick Controls 2), SQLite 3 with FTS5, Poppler-Qt6,
libzip, libxml2, Exiv2, Tesseract/Leptonica, installed Tesseract language data,
and the normal Linux inotify headers. The build never downloads dependencies or
language packs. Packagers may disable optional format families with
`-DPURRFIND_WITH_PDF=OFF`, `-DPURRFIND_WITH_OFFICE=OFF`, or
`-DPURRFIND_WITH_EXIV2=OFF`; use `-DPURRFIND_WITH_OCR=OFF` to build without
Tesseract. Without Exiv2, Qt image previews and dimensions remain available.
Language package names vary by distribution (commonly `tesseract-data-eng` and
`tesseract-data-por`) and are optional runtime resources.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Run from the build tree:

```sh
./build/purrfind-indexer &
./build/purrfind
```

Install using the selected prefix and enable the per-user service:

```sh
cmake --install build
systemctl --user daemon-reload
systemctl --user enable --now purrfind-indexer.service
```

For a staged package, use `DESTDIR` normally. Install destinations use
`GNUInstallDirs`; the system-wide XDG autostart entry is placed below the full
`CMAKE_INSTALL_SYSCONFDIR` path so `/usr` packages correctly install it in
`/etc/xdg/autostart`.

## Packages and release candidates

CMake/CPack can produce source, DEB, and RPM packages. An Arch recipe lives in
`packaging/arch`. Run `scripts/release.sh` for a deterministic source archive
and `SHA256SUMS`; optional signing uses an externally configured GPG key. Native
packages are currently the supported distribution route. See
[packaging](docs/packaging.md) for exact status, Flatpak constraints, uninstall,
purge, and release gates.

## Search syntax

Examples: `contract type:pdf`, `content:"neutral network"`, `name:proposal`,
`path:Documents`, `camera:Canon width:>3000 type:image`, `pages:>20 author:João`,
`source:ocr FIRENETWORK`, and `backup modified:7d`.
Use `kind:file`, `kind:folder`, or the category buttons in the UI.

The default roots and exclusions are visible in Settings. By default PurrFind
indexes `$HOME` and excludes common application-state/tool-cache directories and its
own XDG data directory; hidden files remain searchable.

See [architecture](docs/architecture.md), [indexing](docs/indexing.md),
[search](docs/search.md), [content indexing](docs/content-indexing.md),
[extractors](docs/extractors.md), [database](docs/database.md),
[security](docs/security.md), [benchmarks](docs/benchmarks.md), and
[development](docs/development.md). The release-candidate audit and remaining
validation gates are tracked in the [Fase 5 report](docs/phase5-report.md).
Recovery help is in
[troubleshooting](docs/troubleshooting.md). Phase 4 is documented in
[OCR](docs/ocr.md), [scheduling](docs/ocr-scheduling.md),
[OCR security](docs/ocr-security.md), and
[OCR performance](docs/ocr-performance.md). Phase 3 is documented in
[previews](docs/previews.md), [metadata](docs/metadata.md),
[ranking](docs/ranking.md), [cache](docs/cache.md), and
[performance](docs/performance.md).

## Global shortcut

On X11, PurrFind registers `Super+F` directly with the X server. On Wayland it
uses the standard XDG Desktop Portal Global Shortcuts API, which may show a
one-time approval dialog or override a conflicting binding. The tray icon
shows whether the listener is running and whether shortcut registration
succeeded. Portal support varies by desktop version; see
[development notes](docs/development.md#global-shortcut) for a safe fallback.

## License

GPL-3.0-or-later.
