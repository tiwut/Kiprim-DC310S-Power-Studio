#!/bin/bash
set -e

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NPROC=$(nproc 2>/dev/null || echo 2)

make -C "${SRC_DIR}" -j"${NPROC}"

echo "Build completed successfully: ${SRC_DIR}/kiprim-control"
