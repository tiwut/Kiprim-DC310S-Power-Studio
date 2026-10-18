#!/bin/bash
set -e

BIN_PATH="${HOME}/.local/bin/kiprim-control"
DESKTOP_PATH="${HOME}/.local/share/applications/org.kiprim.dc310s.desktop"
ICON_PATH="${HOME}/.local/share/icons/hicolor/scalable/apps/org.kiprim.dc310s.svg"

rm -f "${BIN_PATH}" "${DESKTOP_PATH}" "${ICON_PATH}"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "${HOME}/.local/share/applications" 2>/dev/null || true
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t "${HOME}/.local/share/icons/hicolor" 2>/dev/null || true
fi

echo "Kiprim Power Studio uninstalled successfully."
