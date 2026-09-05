# Preview architecture

Preview is requested only for the selected result. The UI shows filename,
path/snippet, and basic metadata immediately, waits for a 110 ms navigation
debounce, then starts one cancellable task on a dedicated one-thread pool. A new selection cancels
the previous token and generation checking prevents stale completion from
replacing the current panel.

`PreviewRegistry` selects these internal providers:

- images: runtime-advertised Qt codecs, scaled decoding, static GIF frame, EXIF orientation;
- PDF: Poppler first or matching page, with simple previous/next navigation;
- TXT/Markdown: at most 256 KiB read and a query-centered excerpt;
- DOCX/XLSX/PPTX/ODT/ODS/ODP: structured searchable text and useful metadata;
- video: native Qt Multimedia playback inside the preview panel, with
  play/pause, seek, duration, and mute controls;
- folders: capped, non-recursive immediate-item count;
- generic: MIME, size, date, owner, permissions, inode/device, and theme icon fallback.

The panel is a tall canvas and toggles with `Ctrl+P`. Technical metadata stays
behind **Details**, while extracted text is shown only on request. It is a
preview surface, not a full document editor or file manager.
