#!/bin/sh
set -eu

benchmark=${1:-./build/purrfind-benchmark}
for records in 100000 1000000 5000000; do
    "$benchmark" --records "$records"
done

