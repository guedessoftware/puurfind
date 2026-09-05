#!/bin/sh
set -eu

"${1:-./build/purrfind-metadata-benchmark}" --images "${2:-100000}"
