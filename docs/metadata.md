# Rich metadata

Schema v3 introduces a persistent, low-priority `MetadataQueue`. It indexes
images progressively after filesystem metadata and never blocks startup or
search. Size, mtime, and inode are checked again before commit.

With Exiv2, the only persisted image fields are camera make/model, capture date,
width, and height. On-demand preview additionally shows lens, ISO, aperture, exposure,
focal length, software, copyright, orientation, and GPS coordinates. GPS is
local-only. Without Exiv2, Qt still supplies dimensions and previews.

PDF extraction records pages, title, author, subject, keywords, dates, version,
and encryption state. OOXML reuses `docProps/core.xml` and `docProps/app.xml`;
ODF reuses `meta.xml`. Search syntax initially exposes `author:`, `camera:`,
`pages:`, `width:`, and `height:`.
