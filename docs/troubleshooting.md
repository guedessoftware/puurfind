# Troubleshooting

## Missing Qt library when starting the application

Install the Debian package through `apt`, which resolves the declared Qt/QML
runtime dependencies:

```sh
sudo apt install ./purrfind-*.deb
```

For an existing `dpkg -i` installation, run `sudo apt -f install` and then
retry `purrfind`. A message such as `libQt6Multimedia.so.6: cannot open shared
object file` means that the dependency repair has not completed.

## The index does not update

Check `systemctl --user status purrfind-indexer` and
`journalctl --user -u purrfind-indexer`. Restart with
`systemctl --user restart purrfind-indexer`. PurrFind reconciles configured
roots at startup, including changes made while logged out. The index is a
disposable cache; Settings → Index → Rebuild index preserves the old database
under the XDG data recovery directory, keeps configuration, and crawls again.

After installing or upgrading the package, reload the user unit before
starting it:

```sh
systemctl --user daemon-reload
systemctl --user enable --now purrfind-indexer.service
```

If systemd reports a failed start, inspect the actual cause with
`systemctl --user status purrfind-indexer.service --no-pager -l` and
`journalctl --user -u purrfind-indexer.service -n 80 --no-pager`. An already
active service is healthy; use `systemctl --user restart purrfind-indexer.service`
when replacing a running instance.

If the status shows `218/CAPABILITIES`, the installed unit is from an older
build that used system-manager-only sandbox options. Reinstall the current
package, run `systemctl --user daemon-reload`, and retry; the current unit
keeps user-manager-compatible protections and allows the index to be written
under the user's XDG data directory.

## inotify watch limit

Settings reports when `inotify_add_watch` returns `ENOSPC`, shows the kernel
limit, and schedules reconciliation after queue overflow. PurrFind never changes
sysctl automatically. Reduce indexed roots or have the administrator raise
`fs.inotify.max_user_watches` according to distribution policy.

## Global shortcut and Wayland

On X11, PurrFind registers the configured shortcut directly. On Wayland it
requests it through the Desktop Portal. KDE, GNOME, and portal versions differ
in support and approval UI. The tray menu reports whether registration is
active. A denial or
conflict appears in the overlay. As a portable fallback, bind `purrfind` in the
desktop keyboard settings. If another application already owns the same X11
combination, PurrFind reports the conflict instead of silently replacing it.

## OCR language packs

Settings lists only installed Tesseract `.traineddata` files. Install the
distribution package for `eng`, `por`, or the desired language, then restart
the indexer. On Ubuntu/Debian, the default Portuguese setup is:

```sh
sudo apt install tesseract-ocr tesseract-ocr-eng tesseract-ocr-por
systemctl --user restart purrfind-indexer.service
```

The DEB package recommends the English and Portuguese packs automatically;
the explicit command is useful after installing with `--no-install-recommends`.
PurrFind never downloads language data.

## Remove local data

Uninstalling a package intentionally keeps configuration and the disposable
index. To purge manually after stopping the service, remove the `purrfind`
directories below the user XDG config, data, and cache locations. Never remove
source documents; they are not owned by PurrFind.
