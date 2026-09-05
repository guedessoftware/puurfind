#!/bin/sh
set -eu

binary=${1:-./build/purrfind-ocr-benchmark}
pages=${2:-100}

for dpi in 150 200 300; do
    OMP_THREAD_LIMIT=1 QT_QPA_PLATFORM=offscreen "$binary" --pages "$pages" --languages por+eng --dpi "$dpi"
done

for languages in por eng por+eng; do
    OMP_THREAD_LIMIT=1 QT_QPA_PLATFORM=offscreen "$binary" --pages "$pages" --languages "$languages" --dpi 200
done
