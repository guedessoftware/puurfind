# Phase 2 benchmark baseline

Phase 3 comparison and preview-cache measurements are in
[`performance.md`](performance.md). The tables below are retained as the exact
pre-metadata baseline.

Phase 4 OCR recognition and search measurements are in
[`ocr-performance.md`](ocr-performance.md).

Measured on 2026-09-04 with the Release build (`-O3`) on Linux
7.1.10-zen1-1-zen, an Intel Core i5-9400F (6 logical CPUs), Qt 6.11.2, and
SQLite 3.53.4. The synthetic generator inserts metadata directly into a fresh
SQLite database on local storage; query samples are warm-cache results over six
representative queries repeated 30 times.

| Records | Batch indexing | Throughput | Query mean | Query p95 | Query max | DB startup | Process RSS | Database |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 100,000 | 7.464 s | 13,398/s | 1.18 ms | 3.46 ms | 4.42 ms | 0.06 ms | 86.7 MiB | 72.6 MiB |
| 1,000,000 | 89.409 s | 11,185/s | 9.25 ms | 31.82 ms | 35.36 ms | 0.09 ms | 164.9 MiB | 731.5 MiB |

The schema-v2 1M p95 remains below the 50 ms metadata target. Explicit scoped
filename/path queries measured 18.91 ms average and 31.46 ms p95 at 1M. Startup here means
opening the existing SQLite database read-only and applying query pragmas; UI
process startup/painting is environment- and compositor-dependent and is not
misrepresented by that number. RSS is the benchmark process after insertion and
queries, including SQLite cache, not the normal indexer footprint.

An isolated Phase 2 idle-indexer sample after startup reconciliation used 32,140 KiB
RSS. Across a three-second observation `/proc` reported 0 CPU ticks, 0 bytes
read, and 0 bytes written. This short measurement supports the event-driven
idle design but is not a long-duration power benchmark.

## Content FTS benchmark

Synthetic documents contain technical identifiers, a phrase, and common terms.
The test includes 100 warm-cache unified samples plus a filename-only comparison.

| Documents | Batch indexing | Docs/s | Unified mean | Unified p95 | Filename mean | Filename p95 | RSS | Database |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 10,000 | 1.115 s | 8,969 | 8.06 ms | 9.25 ms | 0.50 ms | 0.51 ms | 34.8 MiB | 6.1 MiB |
| 50,000 | 6.545 s | 7,639 | 36.94 ms | 42.88 ms | 0.34 ms | 0.35 ms | 85.8 MiB | 35.1 MiB |
| 100,000 | 14.559 s | 6,869 | 74.93 ms | 88.54 ms | 0.69 ms | 0.76 ms | 115.3 MiB | 71.0 MiB |

At 100k, content p95 remains below the 100 ms Phase 2 target. Index CPU time was
14.445 s over 14.559 s wall time in the unthrottled synthetic builder. The real
service uses one nice-level-10 worker plus a 20 ms inter-document throttle, so
it intentionally trades throughput for responsiveness.

The metadata generator supports the required larger tier without creating filesystem
entries:

```sh
./build/purrfind-benchmark --records 5000000
```

The 5M tier was not run for this report because it needs roughly 3.4 GiB of
temporary database storage and several minutes on the measured machine. The
included `scripts/run-benchmarks.sh` runs all 100k/1M/5M tiers when that resource
budget is available. Content tiers can be repeated with
`scripts/run-content-benchmarks.sh`.

## Interpretation and limitations

- These are reproducible synthetic metadata measurements, not claims about
  every filesystem or cold-cache workload.
- Initial real-filesystem indexing also pays directory traversal and metadata
  syscall costs, which depend heavily on storage and directory shape.
- Search transport and QML painting add small environment-dependent latency not
  included in the core query timing.
- Database size varies with average path length and tokenizer input.
