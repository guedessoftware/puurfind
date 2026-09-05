# Architecture

PurrFind has three deliberately separate layers:

```text
purrfind (Qt Quick UI)
    ↕ session D-Bus, org.purrfind.Indexer1
purrfind-indexer (crawler + inotify + event queue)
    ↳ low-priority persistent content queue → extractor registry
    ↳ low-priority persistent metadata queue → EXIF/dimension providers
    ↳ lowest-priority persistent OCR queue → one disposable OCR worker
                                               ↳ Poppler + Tesseract API

Selected results take a separate UI-only route through a 110 ms debounce,
cancellation token, small concurrent job, preview registry, and revision-keyed
cache. Preview generation never runs inside search or the indexer D-Bus request.
    ↕ batched prepared statements
purrfind-core (configuration, filesystem metadata, SQLite, parser, ranking)
```

The UI owns no crawler and closing it cannot stop the indexer. The indexer owns
writes while searches are served through its D-Bus API, avoiding cross-process
write races. A second UI invocation calls `Show` on the existing per-user D-Bus
service instead of creating another window.

Filesystem work, initial reconciliation, database writes, query dispatch, and
preview loading do not happen in the QML render loop. Initial crawls run in one
background worker—never one thread per file. Search replies carry a generation
number in the UI so stale asynchronous results are discarded.

`ContentExtractor` is the narrow format contract. The internal registry supplies
plain-text, Poppler PDF, OOXML, and OpenDocument implementations. Extraction is
revision checked and cannot hold up metadata insertion or UI queries.

OCR is deliberately outside both the indexer and the UI process. `OcrQueue`
selects eligible revisions and controls a single `purrfind-ocr-worker` child per
document. The child renders and recognizes pages in memory, returning bounded
page events. `OcrScheduler` applies load/battery policy, while
`OcrLanguageManager` exposes only locally installed Tesseract data. A crash or
watchdog kill loses at most the current page; already committed pages remain
searchable and revision checks prevent stale commits.

The UI activates `org.purrfind.Indexer` through the session bus and systemd user
service. Its public identity is `io.github.guedessoftware.PurrFind`. Search has
one request in flight and replaces its single pending request while typing;
obsolete replies are discarded by generation. Connected status is signal-driven.
