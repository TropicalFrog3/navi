#!/bin/bash
set -euo pipefail

REPO="TropicalFrog3/navi"
MANIFEST_URL="https://raw.githubusercontent.com/$REPO/main/navi/res/version"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/navi-full-update.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT

if command -v curl >/dev/null 2>&1; then
    manifest="$(curl -fsSL --retry 3 --retry-delay 2 "$MANIFEST_URL")"
else
    manifest="$(wget -qO- "$MANIFEST_URL")"
fi

version="$(printf '%s\n' "$manifest" | sed -n 's/^Version: *//p' | head -n 1)"
if [ -z "$version" ]; then
    exit 1
fi

archive="$TMP_DIR/navi.zip"
url="https://github.com/$REPO/releases/download/$version/navi.zip"
if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 --retry-delay 2 "$url" -o "$archive"
else
    wget -q --tries=3 -O "$archive" "$url"
fi

unzip -q -o "$archive" -d "$TMP_DIR/extracted"
bash "$TMP_DIR/extracted/install.sh"
