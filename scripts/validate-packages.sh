#!/bin/sh
set -eu

# Read-only package gate.  It intentionally uses the native package manager
# tools when available, so it can run on a workstation without installing or
# modifying anything.  CI performs the actual install in each distro image.
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
dist="$root/dist"

failures=0
check() {
    label=$1
    shift
    if "$@"; then
        printf 'OK   %s\n' "$label"
    else
        printf 'FAIL %s\n' "$label" >&2
        failures=$((failures + 1))
    fi
}

if command -v dpkg-deb >/dev/null 2>&1 && test -f "$dist/purrfind-0.5.0-rc3-x86_64.deb"; then
    check 'DEB metadata and payload' sh -c \
        'dpkg-deb --field "$1" Package Version Architecture Depends Recommends >/dev/null &&
         dpkg-deb --contents "$1" | grep -q "usr/bin/purrfind" &&
         dpkg-deb --contents "$1" | grep -q "usr/bin/purrfind-indexer"' sh \
        "$dist/purrfind-0.5.0-rc3-x86_64.deb"
else
    printf 'SKIP DEB inspection (dpkg-deb or artifact unavailable)\n'
fi

if command -v rpm >/dev/null 2>&1 && test -f "$dist/purrfind-0.5.0-rc3-x86_64.rpm"; then
    rpm_db=$(mktemp -d "${TMPDIR:-/tmp}/purrfind-rpm-db.XXXXXX")
    trap 'rmdir "$rpm_db" 2>/dev/null || true' EXIT HUP INT TERM
    check 'RPM metadata and OCR requirements' sh -c \
        'rpm --dbpath "$2" -qp --queryformat "%{NAME} %{VERSION}-%{RELEASE} %{ARCH}\n" "$1" >/dev/null &&
         rpm --dbpath "$2" -qp --requires "$1" | grep -q "tesseract" &&
         rpm --dbpath "$2" -qp --list "$1" | grep -q "usr/share/purrfind/tessdata/eng.traineddata" &&
         rpm --dbpath "$2" -qp --list "$1" | grep -q "usr/share/purrfind/tessdata/por.traineddata"' sh \
        "$dist/purrfind-0.5.0-rc3-x86_64.rpm" "$rpm_db"
else
    printf 'SKIP RPM inspection (rpm or artifact unavailable)\n'
fi

if command -v pacman >/dev/null 2>&1 && test -f "$dist/purrfind-0.5.0rc3-27-x86_64.pkg.tar.zst"; then
    check 'Arch metadata and OCR requirements' sh -c \
        'pacman -Qp "$1" >/dev/null &&
         pacman -Qp --info "$1" | grep -q "tesseract" &&
         tar -tf "$1" | grep -q "usr/share/purrfind/tessdata/eng.traineddata" &&
         tar -tf "$1" | grep -q "usr/share/purrfind/tessdata/por.traineddata"' sh \
        "$dist/purrfind-0.5.0rc3-27-x86_64.pkg.tar.zst"
else
    check 'Arch recipe keeps all default OCR packs required' sh -c \
        'grep -q "PURRFIND_BUNDLE_OCR_DATA" "$1" ||
         grep -q "tesseract-data-eng" "$1"' sh \
        "$root/packaging/arch/PKGBUILD"
fi

if command -v flatpak-builder >/dev/null 2>&1; then
    check 'Flatpak manifest parses and has sandbox permissions' sh -c \
        'flatpak-builder --show-manifest "$1" >/dev/null &&
         grep -q -- "--socket=session-bus" "$1" &&
         grep -q -- "--talk-name=org.freedesktop.portal.GlobalShortcuts" "$1"' sh \
        "$root/packaging/io.github.guedessoftware.PurrFind.yml"
else
    printf 'SKIP Flatpak manifest inspection (flatpak-builder unavailable)\n'
fi

test "$failures" -eq 0
