# Packaging and release engineering

The stable application ID is `io.github.guedessoftware.PurrFind`. Native
packages install the UI, per-user indexer, OCR worker, D-Bus activation file,
systemd user unit, desktop entry, XDG autostart entry for the hidden shortcut
listener, AppStream metadata, and scalable hicolor icon.

- DEB and RPM are generated with CPack from a Release build configured with
  `-DCMAKE_INSTALL_PREFIX=/usr`. Each package must be built inside its target
  distribution (Ubuntu/Debian for DEB, Fedora for RPM); Qt and image/archive
  libraries are not ABI-portable across distributions.
- Debian builds install `dpkg-dev` so `dpkg-shlibdeps` records the exact ABI
  package names (including Ubuntu's `t64` transitions). QML imports and the
  multimedia plugin are loaded dynamically, so their runtime packages remain
  explicit CPack dependencies as well.
- `packaging/arch/PKGBUILD` is the Arch recipe. Release automation replaces its
  checksum placeholder before publication.
- `scripts/release.sh` creates a sorted, timestamp-normalized source `.tar.xz`
  without build/cache/database output and writes `SHA256SUMS`.
- `scripts/validate-install.sh build` exercises a clean CMake `DESTDIR`
  installation and verifies every runtime, D-Bus, systemd, desktop, autostart,
  AppStream, and icon artifact.
- Set `PURRFIND_SIGNING_KEY` to create `SHA256SUMS.asc`; private keys never live
  in the repository. Sign a release tag separately with `git tag -s vX.Y.Z`.

## Flatpak

`packaging/io.github.guedessoftware.PurrFind.yml` is the sandboxed build. It
bundles the PDF, office, metadata and Tesseract libraries/data so its feature
set matches the native packages. The Flatpak build sets `PURRFIND_FLATPAK=ON`:
the GUI supervises `purrfind-indexer` in the same sandbox because a Flatpak
cannot install a host `systemd --user` unit. The D-Bus API remains identical.

The manifest grants read-only access to `$HOME`, private writable XDG data for
the index, and access to the GlobalShortcuts portal. This is intentionally
different from DEB/RPM/Arch: Wayland shortcut registration must be approved by
the desktop portal and no compositor-specific daemon is touched. Users who
need to index a directory outside their home can grant it with the desktop's
Flatpak permissions UI (or `flatpak override --filesystem=/path:ro`); the
native packages continue to support arbitrary configured roots directly.

Build and test it with:

```sh
flatpak-builder --user --install --force-clean build-flatpak \
  packaging/io.github.guedessoftware.PurrFind.yml
flatpak run io.github.guedessoftware.PurrFind --background
```

The native package paths are not changed by this option. CI must build DEB in
Ubuntu/Debian, RPM in Fedora, and the Arch recipe on Arch; copying a package
between those distributions is not supported because Qt and image libraries
carry distribution-specific ABI names.

Uninstall removes package-owned binaries, metadata, activation files, and units,
but intentionally preserves each user's index and configuration. See
troubleshooting for explicit purge instructions.
