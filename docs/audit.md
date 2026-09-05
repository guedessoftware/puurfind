# Phase 5 architectural audit

The audit covered every source, QML, test, benchmark, script, document, build,
and packaging file. It retained the existing architecture and changed only
measured or concrete reliability issues.

## Corrected findings

- UI status polling woke the process every 1.5 seconds: live status is now
  D-Bus signal-driven, with a slow retry only while the indexer is offline.
- Fast typing could queue multiple obsolete D-Bus searches: one request is now
  in flight and only the newest pending query is retained.
- SQLite corruption had no product-level recovery: weekly `quick_check`, safe
  preservation, automatic rebuild, and manual rebuild were added.
- Shutdown relied on member destruction order: crawler, watcher, content,
  metadata, OCR, and WAL lifecycle are now explicit.
- inotify `ENOSPC` was logged but not actionable: status includes exhaustion and
  the system limit; overflow explicitly requests reconciliation.
- Ubuntu/Debian Qt 6.4 could not configure against the former 6.5 floor; the
  code-compatible floor is now 6.4 and covered by CI.
- Package identity, icon, activation, restart-storm protection, source hygiene,
  and third-party notices were incomplete and are now defined.
- A real headless UI startup exposed two QML runtime failures missed by lint:
  a final `CheckBox.display` collision and unsupported selection properties on
  `Text`. Both were fixed and the smoke now produces the published screenshot.

## Reviewed without speculative rewrite

Prepared statements/FTS quoting, symlink non-traversal, revision checks, WAL
readers, extraction limits, ZIP traversal/bombs, XML XXE, preview cache LRU,
image limits, OCR isolation/retries, batching, and queue cancellation remain.
No P0/P1 defect is known from this audit.

## Remaining validation boundaries

Real suspend/resume, removable filesystems, KDE/GNOME Wayland, HiDPI,
multi-monitor compositor behavior, and month-scale dogfooding require hardware
or interactive sessions. X11 global `Super+F` was subsequently verified with
the resident tray process, so the earlier X11-only warning is obsolete.
CI definitions are present but remote CI results do not exist in this
workspace. The 5M benchmark has been executed and its large-index p95 is a
known scalability limit; distribution package lifecycle tests remain a release
gate. Both are tracked in the [Fase 5 report](phase5-report.md).
