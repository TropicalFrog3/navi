#!/bin/bash

# Exit on error
set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SLS_DIR="$REPO_ROOT"
PROD_SLS_DIR="$REPO_ROOT/prod-release/navi"

echo "Building Navi..."
cd "$SLS_DIR"
make || "$SLS_DIR/docker/build.sh"

echo "Updating prod-release for Navi..."
mkdir -p "$PROD_SLS_DIR/bin"
mkdir -p "$PROD_SLS_DIR/res"
cp "$SLS_DIR/bin/libnavi.so" "$PROD_SLS_DIR/bin/"
cp "$SLS_DIR/bin/library-inject.so" "$PROD_SLS_DIR/bin/"
# Verify that updates.yaml did not lose any hashes
prev_hashes=0
if [ -f "$PROD_SLS_DIR/res/updates.yaml" ]; then
    prev_hashes=$(grep -cE '^\s*-\s+[0-9a-f]{64}' "$PROD_SLS_DIR/res/updates.yaml" || true)
fi
new_hashes=$(grep -cE '^\s*-\s+[0-9a-f]{64}' "$SLS_DIR/res/updates.yaml")
if [ "$new_hashes" -lt "$prev_hashes" ]; then
    echo "ERROR: updates.yaml hash count decreased ($new_hashes < $prev_hashes)! Aborting."
    exit 1
fi
echo "Verified updates.yaml: $new_hashes hashes (previous: $prev_hashes)"

cp "$SLS_DIR/res/updates.yaml" "$PROD_SLS_DIR/res/"
cp "$SLS_DIR/res/config.yaml" "$PROD_SLS_DIR/res/"
[ -f "$SLS_DIR/res/config.diff.yaml" ] && cp "$SLS_DIR/res/config.diff.yaml" "$PROD_SLS_DIR/res/"
[ -f "$SLS_DIR/res/cache.diff.yaml" ] && cp "$SLS_DIR/res/cache.diff.yaml" "$PROD_SLS_DIR/res/"
cp "$SLS_DIR/res/version" "$PROD_SLS_DIR/res/"
cp "$SLS_DIR/res/update-all.sh" "$PROD_SLS_DIR/res/"
rm -rf "$PROD_SLS_DIR/res/inject-scripts"
[ -d "$SLS_DIR/res/inject-scripts" ] && cp -r "$SLS_DIR/res/inject-scripts" "$PROD_SLS_DIR/res/"
cp "$SLS_DIR/setup.sh" "$PROD_SLS_DIR/"
rm -rf "$PROD_SLS_DIR/tools" "$PROD_SLS_DIR/docs"
cp -r "$SLS_DIR/tools" "$PROD_SLS_DIR/"
cp -r "$SLS_DIR/docs" "$PROD_SLS_DIR/"

chmod +x "$REPO_ROOT/uninstall.sh"
cp "$REPO_ROOT/install.sh" "$REPO_ROOT/prod-release/"
cp "$REPO_ROOT/uninstall.sh" "$REPO_ROOT/prod-release/"
if [ -f "$REPO_ROOT/scripts/legacy_migration.sh" ]; then
    mkdir -p "$REPO_ROOT/prod-release/scripts"
    cp "$REPO_ROOT/scripts/legacy_migration.sh" "$REPO_ROOT/prod-release/scripts/"
fi
if [ -d "$REPO_ROOT/release/navi" ]; then
    cp "$REPO_ROOT/uninstall.sh" "$REPO_ROOT/release/navi/"
fi

echo "Generating update-manifest.yaml..."
cd "$REPO_ROOT/prod-release"
VER_STR=$(grep '^Version:' navi/res/version 2>/dev/null | cut -d' ' -f2 | xargs || echo '1.0.0')
printf "version: \"%s\"\nfiles:\n" "$VER_STR" > update-manifest.yaml
find navi install.sh uninstall.sh $( [ -d scripts ] && echo scripts ) -type f | sort | while read -r f; do
    hash=$(sha256sum "$f" | awk '{print $1}')
    printf "  \"%s\": \"%s\"\n" "$f" "$hash" >> update-manifest.yaml
done

echo "Packaging navi.zip..."
rm -f "$REPO_ROOT/navi.zip"
zip -q -r "$REPO_ROOT/navi.zip" install.sh uninstall.sh update-manifest.yaml navi $( [ -d scripts ] && echo scripts )

echo "Prod-release and navi.zip updated successfully!"
