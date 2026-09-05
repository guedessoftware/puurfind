#!/bin/sh
set -eu

QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-offscreen} \
QT_QUICK_BACKEND=${QT_QUICK_BACKEND:-software} \
    "${1:-./build/purrfind-preview-benchmark}"
