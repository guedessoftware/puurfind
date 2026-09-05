# Development

Configure, compile, and test with:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

Run a synthetic benchmark without creating physical files:

```sh
./build/purrfind-benchmark --records 100000
./build/purrfind-content-benchmark --documents 10000
./scripts/run-preview-benchmark.sh
./scripts/run-metadata-benchmark.sh ./build/purrfind-metadata-benchmark 100000
./scripts/run-ocr-benchmarks.sh ./build/purrfind-ocr-benchmark
./scripts/run-benchmarks.sh ./build/purrfind-benchmark
```

The three scripted tiers are 100k, 1M, and 5M database rows. Large tiers need
several GiB of temporary disk space. Report the machine, build type, and cold or
warm cache state with any published figures.

Optional extractor builds for distribution bootstrapping:

```sh
cmake -S . -B build-minimal -G Ninja \
  -DPURRFIND_WITH_PDF=OFF -DPURRFIND_WITH_OFFICE=OFF \
  -DPURRFIND_WITH_EXIV2=OFF -DPURRFIND_WITH_OCR=OFF
```

OCR needs the Tesseract development library for compilation and one or more
system-provided `.traineddata` files at runtime. It never downloads them. The
worker is installed beside the main binaries when `PURRFIND_WITH_OCR=ON`.
`purrfind-ocr-benchmark --help` exposes the 100/1,000/10,000-page, DPI, and
language controls. `purrfind-benchmark --ocr-pages 100000` measures search and
index size with a synthetic OCR corpus.

Useful log controls include `QT_LOGGING_RULES='purrfind.*.debug=true'`. Per-file
events are not logged at info level.

## Global shortcut

On X11 the app uses a direct root-window key grab, including Caps Lock and Num
Lock variants. This avoids desktop-portal application-ID ambiguity and works
while the QML interface is unloaded. On Wayland it uses
`org.freedesktop.portal.GlobalShortcuts`, the desktop-neutral and Wayland-safe
API. KDE and newer portal backends support it; availability and approval UI
depend on the desktop. If the portal is absent, bind `purrfind` to the chosen
shortcut in the desktop's keyboard settings. This is also the safe fallback on
GNOME versions whose portal backend does not expose GlobalShortcuts.

The resident process exposes a system-tray icon. A click opens the search UI;
its menu reports shortcut state and provides explicit Open and Quit actions.
The XDG autostart entry starts this listener after the KDE panel at login.
Calling `purrfind` again focuses the existing instance, so that binding remains
single-instance and does not leave helper processes behind. The X11 listener
receives only the configured key combination and does not inspect normal
keyboard input.

## Platform notes

- The overlay uses portable Qt window flags. Wayland compositors decide final
  placement and activation according to their focus policy.
- MIME classification uses `QMimeDatabase`; no libmagic dependency is required.
- Plain-text preview reads at most 256 KiB and returns a bounded query-centered excerpt.
- Every rich preview is asynchronous, cancellable, and generated only for the selected result.

## Release checks

`scripts/check-performance.sh` is the 100k CI regression gate; 1M and 5M are
manual release checks. `scripts/release.sh` creates a timestamp-normalized source
archive, generated Arch recipe, and SHA256SUMS. Configure native packages with
`-DCMAKE_INSTALL_PREFIX=/usr`, then use `cpack -G DEB` or `cpack -G RPM`.
See `audit.md`, `packaging.md`, and `troubleshooting.md`.
