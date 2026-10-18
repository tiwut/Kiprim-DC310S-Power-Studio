#!/bin/bash
set -e

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="${HOME}/.local/bin"
APP_DIR="${HOME}/.local/share/applications"
ICON_DIR="${HOME}/.local/share/icons/hicolor/scalable/apps"

make -C "${SRC_DIR}"

mkdir -p "${BIN_DIR}" "${APP_DIR}" "${ICON_DIR}"

install -m 755 "${SRC_DIR}/kiprim-control" "${BIN_DIR}/kiprim-control"
install -m 644 "${SRC_DIR}/data/org.kiprim.dc310s.desktop" "${APP_DIR}/org.kiprim.dc310s.desktop"
install -m 644 "${SRC_DIR}/data/icons/org.kiprim.dc310s.svg" "${ICON_DIR}/org.kiprim.dc310s.svg"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "${APP_DIR}" 2>/dev/null || true
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t "${HOME}/.local/share/icons/hicolor" 2>/dev/null || true
fi

if ! groups "$USER" | grep -q '\bdialout\b'; then
    echo "Notice: Add user to dialout group for serial access: sudo usermod -aG dialout $USER"
fi

echo "Kiprim Power Studio installed successfully."
