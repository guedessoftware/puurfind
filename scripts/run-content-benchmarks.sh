#!/bin/sh
set -eu

benchmark=${1:-./build/purrfind-content-benchmark}
for documents in 10000 50000 100000; do
    "$benchmark" --documents "$documents"
done
