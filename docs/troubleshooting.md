# Troubleshooting

## The index does not update

Check `systemctl --user status purrfind-indexer` and
`journalctl --user -u purrfind-indexer`. Restart with
`systemctl --user restart purrfind-indexer`. PurrFind reconciles configured
roots at startup, including changes made while logged out. The index is a
disposable cache; Settings → Index → Rebuild index preserves the old database
under the XDG data recovery directory, keeps configuration, and crawls again.

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
the indexer. PurrFind never downloads language data.

## Remove local data

Uninstalling a package intentionally keeps configuration and the disposable
index. To purge manually after stopping the service, remove the `purrfind`
directories below the user XDG config, data, and cache locations. Never remove
source documents; they are not owned by PurrFind.
