# Indexing

On startup, the indexer loads XDG configuration, opens
`$XDG_DATA_HOME/purrfind/index.sqlite3` (normally
`~/.local/share/purrfind/index.sqlite3`), installs recursive inotify watches,
and reconciles every configured root once. Unavailable removable roots are
marked offline rather than deleted.

The crawler uses `std::filesystem`/`lstat`, indexes symlink metadata without
following symlink directory trees, tolerates disappearing or inaccessible
entries, and commits records in batches of 512. Each reconciliation has a
generation; records not seen in that root are removed at the end. This handles
changes made while the service was stopped without periodic full-disk scans.

After reconciliation, inotify is entirely event-driven. Events are coalesced
for 200 ms and committed in one transaction. New or moved-in directories get
watches and a one-time subtree visit. Queue overflow schedules reconciliation.
There is no polling scan, so idle filesystem I/O is zero apart from SQLite/OS
maintenance.

Content extraction is deliberately downstream of this path. Each metadata batch
wakes the persistent low-priority content queue, so name/path results become
available first while full-text coverage grows progressively. See
`content-indexing.md` for states, cancellation, limits, and throttling.

OCR is a third, still lower-priority layer. PDFs first pass through native text
extraction; a tested page heuristic schedules only pages lacking useful text.
Eligible page/image revisions live in SQLite, so the queue survives restarts.
Each recognized page becomes searchable atomically while the remaining pages
continue. See `ocr.md` and `ocr-scheduling.md` for formats and policy.

Default exclusions are:

- `$HOME/.cache`
- `$HOME/.config`, `$HOME/.local`, and `$HOME/.var`
- generated package/tool data in `$HOME/.android`, `$HOME/.cargo/{git,registry}`,
  `$HOME/.gradle`, `$HOME/.npm`, `$HOME/.ollama`, `$HOME/.pub-cache`,
  `$HOME/.rustup`, `$HOME/.steam`, and `$HOME/.vscode`
- `$XDG_DATA_HOME/purrfind` (the index must never index its own WAL/database)

They are persisted and visible. The internal PurrFind data exclusion is enforced
to prevent a database-write/inotify feedback loop; the other exclusions are
editable. This is not a blanket dotfile rule: hidden files outside those explicit
application-state paths are indexed and can be hidden at query time. Permission
errors are logged and skipped; root privileges are never requested.

An explicit root is traversed as a directory tree, including mount points below
it; symlinked directories are never followed. Users who do not want a nested
local, network, or removable mount indexed must exclude it. If an explicit root
is unavailable at reconciliation, its rows are marked offline and hidden from
normal results rather than deleted; reconnecting and restarting/reindexing makes
them current again.
