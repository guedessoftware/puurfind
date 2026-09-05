# Preview cache

Preview images use a mutex-protected 96 MiB RAM LRU and an optional 256 MiB disk
cache at `$XDG_CACHE_HOME/purrfind/previews`. PNG thumbnails have a small JSON
sidecar for non-content display metadata. The key hashes file id, mtime,
size, inode, page, and target dimensions. A changed revision therefore cannot
reuse an old image. Disk entries are PNG thumbnails, never blobs in the search
database, and oldest entries are pruned periodically.

Cache clearing is available in Settings. Text and metadata remain bounded and
are cached only in RAM; original full-resolution images are never retained.
