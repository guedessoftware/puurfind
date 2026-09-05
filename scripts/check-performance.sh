#!/bin/sh
set -eu
binary=${1:-./build/purrfind-benchmark}
records=${2:-100000}
output=$($binary --records "$records")
printf '%s\n' "$output"
actual_records=$(printf '%s\n' "$output" | awk '/^records:/ {print $2}')
p95=$(printf '%s\n' "$output" | awk '/^query_p95_ms:/ {print $2}')
typing_p95=$(printf '%s\n' "$output" | awk '/^typing_p95_ms:/ {print $2}')
scoped_p95=$(printf '%s\n' "$output" | awk '/^scoped_filename_p95_ms:/ {print $2}')
insert_rate=$(printf '%s\n' "$output" | awk '/^insert_records_per_second:/ {print $2}')
rss=$(printf '%s\n' "$output" | awk '/^rss_bytes:/ {print $2}')
database=$(printf '%s\n' "$output" | awk '/^database_bytes:/ {print $2}')
test "$actual_records" = "$records"
test -n "$p95" -a -n "$typing_p95" -a -n "$scoped_p95" -a -n "$insert_rate" -a -n "$rss" -a -n "$database"
awk -v query="$p95" -v typing="$typing_p95" -v scoped="$scoped_p95" \
    -v rate="$insert_rate" -v rss="$rss" -v db="$database" -v rows="$records" 'BEGIN {
    if (query > 100.0 || typing > 50.0 || scoped > 50.0) exit 1
    if (rate < 1000.0 || rss > 536870912) exit 1
    if (db <= 0 || db > rows * 2048) exit 1
}'
