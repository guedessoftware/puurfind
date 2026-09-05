# Phase 3 performance

Release measurements on 2026-09-04 use Qt 6.11.2, SQLite 3.53.4, Poppler-Qt6
26.08.0, Exiv2 0.28.8, and local temporary storage.

| 100-item preview corpus | Cold average | Cold p95 | Warm average | Warm p95 |
|---|---:|---:|---:|---:|
| PNG images | 18.40 ms | 18.87 ms | 3.66 ms | 3.71 ms |
| one-page PDFs | 13.00 ms | 13.28 ms | 2.32 ms | 2.38 ms |
| text documents | 0.49 ms | 0.61 ms | 0.13 ms | 0.14 ms |

The mixed 300-preview benchmark peaked at 194.1 MiB RSS, within the 250 MiB
normal-use budget. It deliberately uses 100 distinct revision keys per class.

At one million filesystem records, schema v3 measured 9.37 ms mean, 31.51 ms
p95, and 33.62 ms maximum; schema v2 measured 9.25/31.82/35.36 ms. The v3
database was 757.5 MiB versus 731.5 MiB for v2 before any EXIF rows, a 3.6%
schema/index increase. A 100k typical content query measured 71.67 ms mean and
82.52 ms p95 versus Phase 2's 74.93/88.54 ms.

The final 100k content insertion measured 15.957 s (6,267 documents/s), 15.899 s
CPU time, 118.4 MiB RSS, and a 74.4 MiB database. Filename-only queries in that
corpus measured 0.76 ms mean and 0.83 ms p95.

An isolated indexer with both background queues drained, including one indexed
image, used 36,584 KiB RSS. During a three-second `/proc` sample it consumed
0 CPU ticks and performed 0 bytes of I/O.

A separate 100k-image metadata backfill inserted 36,711 rows/s in 2.724 s.
The indexed filter `camera:canon width:>3000 height:>2000` measured 36.33 ms
mean and 36.88 ms p95. Rich rows added 11,563,008 bytes (11.0 MiB) to a
44,109,824-byte metadata-only database; peak benchmark RSS was 101.0 MiB.

Preview timings use small repeatable fixtures and warm-cache results include
RAM/disk lookup. They do not claim equivalent render time for complex PDFs or
pathological camera files. Reproduce with `scripts/run-preview-benchmark.sh`.

## Phase 4 comparison

At one million file rows plus 100,000 OCR pages, the unchanged normal search
path measured 9.65 ms mean and 31.89 ms p95, compared with 9.37/31.51 ms in
Phase 3. OCR-only search measured 80.93/81.72 ms and the OCR corpus added
14.8 MiB. Recognition throughput, DPI/language comparisons, CPU, memory, and
limitations are recorded in [`ocr-performance.md`](ocr-performance.md).

## Phase 5 release-candidate gate

On the same local system, the 100k Release gate measured 9.197 s initial
insertion (10,873 records/s), 1.47 ms query mean, 0.96 ms p50, 3.70 ms p95,
3.85 ms p99, 4.35 ms maximum, 88.8 MiB RSS, and a 78.5 MiB database. Simulated
incremental typing measured 0.45/0.50/0.53 ms p50/p95/p99. `/proc/self/io` did
not expose usable physical-write accounting in this run, so it is reported as
unavailable rather than zero.

The current one-million-row run measured 107.664 s insertion (9,288 records/s),
9.63 ms mean, 3.37 ms p50, 31.79 ms p95, 32.54 ms p99, 34.37 ms maximum,
157.4 MiB RSS, and 799.3 MiB database size. Physical-write accounting was
unavailable on this kernel. The five-million-row run completed in 596.074 s
(8,388 records/s), with 47.75 ms mean, 14.00 ms p50, 164.29 ms p95, 166.15 ms
p99, 152.3 MiB RSS, and 4.21 GiB database size. The 5M result is a scale
characterization rather than a claim that every query remains under the 1M
target; it identifies the need for a future sharding/FTS optimization before
very large deployments. Real desktop, suspend/removable-volume, and remote
distribution-CI validation remain release gates; see `audit.md` and
`phase5-report.md`.
