#!/bin/sh
set -eu
version=$(tr -d '\n' < VERSION)
case "$version" in *[!0-9A-Za-z.-]*) echo "Invalid VERSION" >&2; exit 1;; esac
mkdir -p dist
archive="dist/PurrFind-${version}-source.tar.xz"
tar --sort=name --mtime="@${SOURCE_DATE_EPOCH:-0}" --owner=0 --group=0 --numeric-owner \
  --exclude='./build' --exclude='./build-*' --exclude='./dist' --exclude='./dist/**' \
  --exclude='./dist-*' --exclude='./dist-*/**' \
  --exclude='./CMakeFiles' --exclude='./CMakeFiles/**' --exclude='./CMakeCache.txt' \
  --exclude='./cmake_install.cmake' --exclude='./CTestTestfile.cmake' --exclude='./Makefile' \
  --exclude='./_CPack_Packages' --exclude='./_CPack_Packages/**' --exclude='./.git' \
  --exclude='./*.rpm' --exclude='./*.deb' --exclude='./*.pkg.tar.zst' \
  --exclude='./purrfind-debug*' --exclude='*.sqlite3*' \
  --transform "s,^.,PurrFind-${version}," -cJf "$archive" .
source_hash=$(sha256sum "$archive" | awk '{print $1}')
sed "s/@SOURCE_SHA256@/$source_hash/" packaging/arch/PKGBUILD > dist/PKGBUILD
(cd dist && {
  sha256sum "$(basename "$archive")" PKGBUILD
  # Pass only the artifacts belonging to this release.  Without the variable,
  # hashes are limited to files explicitly staged in dist by the packager;
  # this avoids silently publishing checksums for obsolete RCs left by local
  # iteration.
  if test -n "${PURRFIND_RELEASE_ARTIFACTS:-}"; then
    for artifact in $PURRFIND_RELEASE_ARTIFACTS; do
      test -f "$artifact" || { echo "missing release artifact: $artifact" >&2; exit 1; }
      sha256sum "$artifact"
    done
  fi
} > SHA256SUMS)
if test -n "${PURRFIND_SIGNING_KEY:-}"; then gpg --local-user "$PURRFIND_SIGNING_KEY" --armor --detach-sign dist/SHA256SUMS; fi
printf 'Created %s and dist/SHA256SUMS\n' "$archive"
