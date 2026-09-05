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

Flatpak is not a supported artifact for this candidate. A persistent independent
systemd user indexer cannot be installed/activated inside an ordinary Flatpak,
and access to arbitrary explicit roots cannot be represented safely without
`filesystem=host`. Shipping a reduced or over-permissioned manifest would be
misleading. Native packages remain the correct architecture until a portal and
sandbox lifecycle design is implemented and validated.

Uninstall removes package-owned binaries, metadata, activation files, and units,
but intentionally preserves each user's index and configuration. See
troubleshooting for explicit purge instructions.
