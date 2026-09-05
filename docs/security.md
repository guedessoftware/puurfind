# Content index security

The content index can contain sensitive document text and never leaves the
machine. PurrFind has no upload, telemetry, account, cloud API, or network parser.
It runs as the logged-in user without setuid, capabilities, or root access.

ZIP containers are never extracted to disk. Entries with absolute paths or
`..` traversal are rejected. Defaults allow at most 2,048 entries, 64 MiB per
entry, 256 MiB total uncompressed data, and a 200:1 compression ratio.

libxml2 parsing uses `NONET` and, where available, `NO_XXE`; DTD/entity
substitution is never enabled. Tests verify that local external entities do not
expand. Invalid/truncated ZIP, XML, PDF, binary text, and empty files become
document states instead of terminating the service. Logs never include extracted
text.

Users may exclude a path from all indexing or only from content indexing. The
latter preserves filename/path discovery while keeping document text out of the
database.

Image preview uses only Qt codecs advertised at runtime and caps decoded image
allocation at 128 MiB plus a 100-megapixel/32,768-pixel dimension guard. PDF
preview uses Poppler directly; no external thumbnailer or process is invoked.
Preview cache directories are mode 0700 and generated files mode 0600. EXIF GPS
coordinates remain local and are never reverse-geocoded or sent to map APIs.

OCR has an additional process boundary, watchdog, retry ceiling, resource and
decode limits, and performs page rendering in memory. It never modifies a PDF,
creates an OCR copy, downloads language data, or logs recognized text. See
[`ocr-security.md`](ocr-security.md) for the exact limits and failure model.

The systemd user unit applies an owner-only umask, no-new-privileges, private
temporary storage, read-only system directories, kernel/control-group
protections, restricted SUID/SGID and realtime operations, restart-storm
protection, a Unix-socket-only address-family policy, and a 15-second stop
timeout. `ProtectHome` remains disabled because
reading user-selected files is the product's purpose. PurrFind never changes
sysctl, runs as root, executes user-supplied commands, or persists its own logs.
