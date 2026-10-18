#!/bin/bash
set -e

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ -f "${SRC_DIR}/Makefile" ]; then
    make -C "${SRC_DIR}" clean 2>/dev/null || true
fi

rm -f "${SRC_DIR}/kiprim-control"
rm -f "${SRC_DIR}"/src/*.o
rm -rf "${SRC_DIR}/build"
rm -rf "${SRC_DIR}/.cache"

echo "All binaries and object files cleaned successfully."
