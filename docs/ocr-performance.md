# OCR performance

Release measurements on 2026-09-04 used Tesseract 5.5.3, Leptonica 1.87.0,
Poppler-Qt6 26.08.0, `por` and `eng`, and the low one-thread resource profile.
The controlled printed-text page contains Portuguese, English, IP/ASN and
network identifiers. Accuracy below means both `FIRENETWORK` and `AS26615` were
recognized; it is not a general OCR quality claim.

| 100 pages, `por+eng` | Pages/min | Mean/page | p95 | Controlled accuracy | Peak RSS |
|---|---:|---:|---:|---:|---:|
| 150 DPI | 561.3 | 106.89 ms | 106 ms | 100% | 134.6 MiB |
| 200 DPI | 498.2 | 120.44 ms | 119 ms | 100% | 135.7 MiB |
| 300 DPI | 332.1 | 180.66 ms | 180 ms | 100% | 139.0 MiB |

200 DPI remains the default: it preserves margin for smaller real-world print
while costing substantially less than 300 DPI. There is no unproven adaptive
second pass or deskew stage.

| 100 pages at 200 DPI | Pages/min | Mean/page | CPU | Peak RSS |
|---|---:|---:|---:|---:|
| `por` | 502.2 | 119.48 ms | 99.7% of one core | 92.0 MiB |
| `eng` | 606.3 | 98.96 ms | 99.6% of one core | 115.1 MiB |
| `por+eng` | 499.7 | 120.07 ms | 99.9% of one core | 135.7 MiB |

The 1,000-page `por+eng` run sustained 502.7 pages/minute, 119.36 ms mean,
120 ms p95, 99.7% of one CPU core, 137.2 MiB peak RSS, zero measured physical
I/O bytes during the cached run, zero errors, and 208 KiB of final OCR index
growth for the repetitive fixture. The 10,000-page tier is provided by the
benchmark but was not executed here; at measured throughput it would occupy one
core for about 20 minutes without adding independent coverage.

With one million filesystem rows and 100,000 synthetic OCR pages, normal search
measured 9.65 ms mean and 31.89 ms p95 versus Phase 3's 9.37/31.51 ms. OCR-only
queries measured 80.93 ms mean and 81.72 ms p95. The OCR pages added 15,540,224
bytes (14.8 MiB). Reproduce OCR/DPI/language runs with
`scripts/run-ocr-benchmarks.sh`; use `purrfind-benchmark --ocr-pages` for search.

With all queues drained and OCR enabled, an isolated indexer over an empty root
used 37,108 KiB RSS. A three-second `/proc` sample recorded 0 CPU ticks, 0 bytes
read, and 0 bytes written, confirming that the OCR queue does not busy-poll when
idle. This short sample is a regression check, not a long-duration power test.
