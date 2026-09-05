#!/bin/sh
set -eu

build_dir=${1:-build}
stage=$(mktemp -d "${TMPDIR:-/tmp}/purrfind-stage.XXXXXX")
trap 'rm -rf "$stage"' EXIT HUP INT TERM

DESTDIR="$stage" cmake --install "$build_dir" >/dev/null
for path in \
    usr/bin/purrfind \
    usr/bin/purrfind-indexer \
    usr/share/dbus-1/services/org.purrfind.Indexer.service \
    usr/share/applications/io.github.guedessoftware.PurrFind.desktop \
    usr/share/metainfo/io.github.guedessoftware.PurrFind.metainfo.xml \
    usr/share/icons/hicolor/scalable/apps/io.github.guedessoftware.PurrFind.svg \
    etc/xdg/autostart/io.github.guedessoftware.PurrFind-autostart.desktop; do
    test -e "$stage/$path" || { echo "missing staged file: $path" >&2; exit 1; }
done
service_file=$(find "$stage/usr" -path '*/systemd/user/purrfind-indexer.service' -type f -print -quit)
if test -z "$service_file"; then
    echo "missing staged file: */systemd/user/purrfind-indexer.service" >&2
    exit 1
fi
test -x "$stage/usr/bin/purrfind"
test -x "$stage/usr/bin/purrfind-indexer"
echo "CMake DESTDIR installation is complete"
