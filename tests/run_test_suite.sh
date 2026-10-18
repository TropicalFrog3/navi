#!/usr/bin/env bash
#
# tests/run_test_suite.sh
# Comprehensive offline verification test suite for Navi
# Tests config migration, cache migration, cache backups, atomic updates,
# payload integrity checks, rollbacks, faked URLs, and category uninstallation.
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_TMP="$(mktemp -d /tmp/sls_test_suite_XXXXXX)"

trap 'rm -rf "$TEST_TMP"' EXIT

# Load legacy migration variables and helper functions
# shellcheck source=/dev/null
source "$REPO_ROOT/scripts/legacy_migration.sh"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
}

fail() {
    echo -e "${RED}[FAIL]${NC} $1"
    exit 1
}

step() {
    echo -e "\n${BLUE}================================================================${NC}"
    echo -e "${BLUE}>>> $1${NC}"
    echo -e "${BLUE}================================================================${NC}"
}

export SLS_TEST_ENV=1
export SLS_UPDATE_AUTO_ACCEPT=1
export SLS_UNINSTALL_AUTO_ACCEPT=1

step "Test Case 1 & 2: Config Migration and Cache Migration Unit Tests"
echo "Building and running unit tests with test_migration..."
if [ ! -x "$REPO_ROOT/bin/test_migration" ]; then
    make -C "$REPO_ROOT" audit-libs -j"$(nproc)" >/dev/null 2>&1 || "$REPO_ROOT/docker/build.sh" >/dev/null

    g++ -m32 -std=c++20 -D_GLIBCXX_USE_CXX11_ABI=0 -fuse-ld=gold -isystem"$REPO_ROOT/include" \
        -I"$REPO_ROOT/src" -I"$REPO_ROOT/src/core" -I"$REPO_ROOT/src/model" -I"$REPO_ROOT/src/model/migrations" -I"$REPO_ROOT/src/presenter" -I"$REPO_ROOT/src/view" -I"$REPO_ROOT/src/model/sdk" \
        "$REPO_ROOT/tests/test_migration.cpp" \
        "$REPO_ROOT/src/model/migrations/config_migration.cpp" \
        "$REPO_ROOT/src/model/migrations/cache_migration.cpp" \
        "$REPO_ROOT/src/model/migrations/legacy_migration.cpp" \
        "$REPO_ROOT/lib/libyaml-cpp.a" \
        -o "$REPO_ROOT/bin/test_migration"
fi

"$REPO_ROOT/bin/test_migration"
pass "Config and Cache Migration unit tests passed successfully."

step "Test Case 1b: Native Install & Manifest Code Wire-Format Unit Tests"
echo "Compiling and running test_native_install..."
if command -v docker >/dev/null 2>&1; then
    docker run --rm --mount=type=bind,source="$REPO_ROOT",target=/src --workdir=/src sls-host:latest sh -c \
        "make bin/test_native_install && ./bin/test_native_install"
elif [ -x "$REPO_ROOT/bin/test_native_install" ]; then
    "$REPO_ROOT/bin/test_native_install"
else
    fail "Neither docker nor precompiled bin/test_native_install found."
fi
pass "Native Install and Manifest Code unit tests passed successfully."

step "Test Case 3: Auto-Updater Up-to-Date Scenario (Faked Local URL)"
SANDBOX_HOME="$TEST_TMP/fake_home"
mkdir -p "$SANDBOX_HOME/.local/share/navi" "$SANDBOX_HOME/.config/navi"

# Compile test_autoupdate harness (host or docker) if not already present
if [ ! -x "$REPO_ROOT/bin/test_autoupdate" ]; then
    if ! g++ -m32 -std=c++20 -D_GLIBCXX_USE_CXX11_ABI=0 -fuse-ld=gold -isystem"$REPO_ROOT/include" \
        -I"$REPO_ROOT/src" -I"$REPO_ROOT/src/core" -I"$REPO_ROOT/src/model" -I"$REPO_ROOT/src/presenter" -I"$REPO_ROOT/src/view" -I"$REPO_ROOT/src/model/sdk" \
        "$REPO_ROOT/tests/test_autoupdate.cpp" \
        "$REPO_ROOT/src/presenter/autoupdate.cpp" \
        "$REPO_ROOT/src/core/curl.cpp" \
        "$REPO_ROOT/lib/libyaml-cpp.a" \
        -lssl -lcrypto -lcurl \
        -o "$REPO_ROOT/bin/test_autoupdate" 2>/dev/null; then
        if command -v docker >/dev/null 2>&1; then
            docker run --rm --mount=type=bind,source="$REPO_ROOT",target=/src --workdir=/src sls-host:latest \
                g++ -m32 -std=c++20 -D_GLIBCXX_USE_CXX11_ABI=0 -fuse-ld=gold -isysteminclude \
                -Isrc -Isrc/core -Isrc/model -Isrc/presenter -Isrc/view -Isrc/model/sdk \
                tests/test_autoupdate.cpp src/presenter/autoupdate.cpp src/core/curl.cpp lib/libyaml-cpp.a \
                -lssl -lcrypto -lcurl -o bin/test_autoupdate
        fi
    fi
fi

MOCK_VER_FILE="$TEST_TMP/version_uptodate.yaml"
cat << 'EOF' > "$MOCK_VER_FILE"
Version: "0.0.1"
Changelog: "Older mock version"
DownloadUrl: "file:///dev/null"
EOF

OUTPUT=$(HOME="$SANDBOX_HOME" SLS_UPDATE_VERSION_URL="file://$MOCK_VER_FILE" "$REPO_ROOT/bin/test_autoupdate")
echo "$OUTPUT"

if echo "$OUTPUT" | grep -q "Already up to date"; then
    pass "Auto-updater correctly identified that local version is up-to-date."
else
    fail "Auto-updater failed to detect up-to-date state."
fi

step "Test Case 4: Successful Atomic Update with Manifest Integrity Check & Cache Backup"
# 1. Setup initial live install in sandbox
mkdir -p "$SANDBOX_HOME/.local/share/navi"
echo "LIVE_NAVI_V1" > "$SANDBOX_HOME/.local/share/navi/libnavi.so"
echo "LIVE_LIBRARY_INJECT_V1" > "$SANDBOX_HOME/.local/share/navi/library-inject.so"

# Setup initial user config
mkdir -p "$SANDBOX_HOME/.config/navi"
echo "LogLevels: '0x0'" > "$SANDBOX_HOME/.config/navi/config.yaml"

# Setup initial cache dirs and untouchable manifest dir
set_legacy_paths "$SANDBOX_HOME"
mkdir -p "$LEGACY_DEPOTS_DIR"
echo "dummy_cache_content" > "$LEGACY_DEPOTS_DIR/100.dat"
mkdir -p "$LEGACY_MANIFESTS_DIR"
echo "untouchable_manifest_content" > "$LEGACY_MANIFESTS_DIR/100.manifest"

# 2. Build mock release archive
RELEASE_SRC="$TEST_TMP/mock_release_src"
mkdir -p "$RELEASE_SRC/navi"
echo "NEW_NAVI_V99" > "$RELEASE_SRC/navi/libnavi.so"
echo "NEW_LIBRARY_INJECT_V99" > "$RELEASE_SRC/navi/library-inject.so"
cp "$REPO_ROOT/uninstall.sh" "$RELEASE_SRC/uninstall.sh"

cat << 'EOF' > "$RELEASE_SRC/install.sh"
#!/bin/bash
set -e
TARGET="$HOME/.local/share/navi"
mkdir -p "$TARGET"
cp -f navi/libnavi.so "$TARGET/libnavi.so"
cp -f navi/library-inject.so "$TARGET/library-inject.so"
cp -f uninstall.sh "$TARGET/uninstall.sh"
echo "Mock installation complete."
EOF
chmod +x "$RELEASE_SRC/install.sh"

# Generate update-manifest.yaml with real SHA-256 hashes
cd "$RELEASE_SRC"
cat << EOF > update-manifest.yaml
version: "99.0.0"
files:
  "install.sh": "$(sha256sum install.sh | awk '{print $1}')"
  "uninstall.sh": "$(sha256sum uninstall.sh | awk '{print $1}')"
  "navi/libnavi.so": "$(sha256sum navi/libnavi.so | awk '{print $1}')"
  "navi/library-inject.so": "$(sha256sum navi/library-inject.so | awk '{print $1}')"
EOF

MOCK_ZIP="$TEST_TMP/mock_release_99.zip"
zip -q -r "$MOCK_ZIP" install.sh uninstall.sh update-manifest.yaml navi
cd "$REPO_ROOT"

# Mock version manifest pointing to mock zip
MOCK_VER_UPGRADE="$TEST_TMP/version_upgrade.yaml"
cat << EOF > "$MOCK_VER_UPGRADE"
Version: "99.0.0"
Changelog: "Awesome mock update v99.0.0"
DownloadUrl: "file://$MOCK_ZIP"
EOF

# Run autoupdate
UPGRADE_OUTPUT=$(HOME="$SANDBOX_HOME" SLS_UPDATE_VERSION_URL="file://$MOCK_VER_UPGRADE" "$REPO_ROOT/bin/test_autoupdate")
echo "$UPGRADE_OUTPUT"

# Verify payload integrity check passed
if ! echo "$UPGRADE_OUTPUT" | grep -q "Payload integrity verified successfully"; then
    fail "Payload integrity check did not pass."
fi

# Verify live files were updated
LIVE_SO_CONTENT=$(cat "$SANDBOX_HOME/.local/share/navi/libnavi.so")
if [ "$LIVE_SO_CONTENT" != "NEW_NAVI_V99" ]; then
    fail "Live libnavi.so was not updated! Got: $LIVE_SO_CONTENT"
fi

# Verify user config backup was created
if ! ls "$SANDBOX_HOME/.config/navi/backups/"*.bak >/dev/null 2>&1; then
    fail "User config backup was not created during update!"
fi

# Verify cache backup was created for depots
if ! ls "$SANDBOX_HOME/.local/share/navi/cache_backups/" >/dev/null 2>&1; then
    fail "Cache backup was not created during update!"
fi

# Verify manifest directory was untouched and NOT backed up
if [ ! -f "$LEGACY_MANIFESTS_DIR/100.manifest" ]; then
    fail "Manifest file was modified or deleted during update!"
fi
if find "$SANDBOX_HOME/.local/share/navi/cache_backups/" -name "morrenus_manifests" | grep -q .; then
    fail "Manifest directory was copied into cache backup (manifests must be completely untouched)!"
fi

# Verify staging directory was cleaned up
if ls -d "$SANDBOX_HOME/.local/share/navi/updates/staging_"* >/dev/null 2>&1; then
    fail "Staging directory was not cleaned up after successful update!"
fi

pass "Atomic update with integrity verification and cache backup completed successfully (manifests untouched)."

step "Test Case 5: Tampered Payload Detection & Staging Cleanup"
# Create a release where a file does not match the manifest
TAMPERED_SRC="$TEST_TMP/mock_tampered_src"
mkdir -p "$TAMPERED_SRC/navi"
echo "ORIGINAL_CONTENT" > "$TAMPERED_SRC/navi/libnavi.so"
echo "ORIGINAL_INJECT" > "$TAMPERED_SRC/navi/library-inject.so"
cp "$RELEASE_SRC/install.sh" "$TAMPERED_SRC/install.sh"

cd "$TAMPERED_SRC"
cat << EOF > update-manifest.yaml
version: "99.1.0"
files:
  "install.sh": "$(sha256sum install.sh | awk '{print $1}')"
  "navi/libnavi.so": "0000000000000000000000000000000000000000000000000000000000000000"
  "navi/library-inject.so": "$(sha256sum navi/library-inject.so | awk '{print $1}')"
EOF

TAMPERED_ZIP="$TEST_TMP/mock_tampered.zip"
zip -q -r "$TAMPERED_ZIP" install.sh update-manifest.yaml navi
cd "$REPO_ROOT"

MOCK_VER_TAMPERED="$TEST_TMP/version_tampered.yaml"
cat << EOF > "$MOCK_VER_TAMPERED"
Version: "99.1.0"
Changelog: "Tampered test update"
DownloadUrl: "file://$TAMPERED_ZIP"
EOF

TAMPER_OUTPUT=$(HOME="$SANDBOX_HOME" SLS_UPDATE_VERSION_URL="file://$MOCK_VER_TAMPERED" "$REPO_ROOT/bin/test_autoupdate" 2>&1 || true)
echo "$TAMPER_OUTPUT"

if echo "$TAMPER_OUTPUT" | grep -q "Manifest checksum mismatch"; then
    pass "Auto-updater successfully detected tampered payload hash mismatch!"
else
    fail "Auto-updater failed to detect tampered payload!"
fi

# Live files must NOT have changed
LIVE_SO_CONTENT=$(cat "$SANDBOX_HOME/.local/share/navi/libnavi.so")
if [ "$LIVE_SO_CONTENT" != "NEW_NAVI_V99" ]; then
    fail "Live file was corrupted by tampered payload! Content: $LIVE_SO_CONTENT"
fi

# Staging must be removed
if ls -d "$SANDBOX_HOME/.local/share/navi/updates/staging_"* >/dev/null 2>&1; then
    fail "Staging directory was not cleaned up after tampered payload abort!"
fi
pass "Tampered payload rejected, live files untouched, and staging purged."

step "Test Case 6: Auto-Updater Failure & Staged Rollback"
FAIL_SRC="$TEST_TMP/mock_fail_src"
mkdir -p "$FAIL_SRC/navi"
echo "CORRUPTED_SO" > "$FAIL_SRC/navi/libnavi.so"
echo "CORRUPTED_INJECT" > "$FAIL_SRC/navi/library-inject.so"

cat << 'EOF' > "$FAIL_SRC/install.sh"
#!/bin/bash
echo "Simulating crash midway through install..."
exit 1
EOF
chmod +x "$FAIL_SRC/install.sh"

cd "$FAIL_SRC"
cat << EOF > update-manifest.yaml
version: "99.2.0"
files:
  "install.sh": "$(sha256sum install.sh | awk '{print $1}')"
  "navi/libnavi.so": "$(sha256sum navi/libnavi.so | awk '{print $1}')"
  "navi/library-inject.so": "$(sha256sum navi/library-inject.so | awk '{print $1}')"
EOF

FAIL_ZIP="$TEST_TMP/mock_fail.zip"
zip -q -r "$FAIL_ZIP" install.sh update-manifest.yaml navi
cd "$REPO_ROOT"

MOCK_VER_FAIL="$TEST_TMP/version_fail.yaml"
cat << EOF > "$MOCK_VER_FAIL"
Version: "99.2.0"
Changelog: "Fail test update"
DownloadUrl: "file://$FAIL_ZIP"
EOF

FAIL_OUTPUT=$(HOME="$SANDBOX_HOME" SLS_UPDATE_VERSION_URL="file://$MOCK_VER_FAIL" "$REPO_ROOT/bin/test_autoupdate" 2>&1 || true)
echo "$FAIL_OUTPUT"

# Verify rollback message in output
if echo "$FAIL_OUTPUT" | grep -q -E 'Rolling back|Release installer failed'; then
    pass "Installer failure detected."
else
    fail "Installer failure was not caught!"
fi

# Verify live file was restored to previous state
LIVE_SO_CONTENT=$(cat "$SANDBOX_HOME/.local/share/navi/libnavi.so")
if [ "$LIVE_SO_CONTENT" != "NEW_NAVI_V99" ]; then
    fail "Rollback failed! Live content is: $LIVE_SO_CONTENT, expected: NEW_NAVI_V99"
fi
pass "Rollback verified: previous live installation was successfully restored."

step "Test Case 7: Uninstaller Category Isolation & Idempotency"
UNINSTALL_HOME="$TEST_TMP/uninstall_home"
set_legacy_paths "$UNINSTALL_HOME"
mkdir -p "$UNINSTALL_HOME/.local/share/navi" \
         "$LEGACY_MANIFESTS_DIR" \
         "$UNINSTALL_HOME/.steam/steam/config/stplug-in" \
         "$UNINSTALL_HOME/.local/share/Steam/config/stplug-in" \
         "$UNINSTALL_HOME/.local/share/Steam/config/depotcache" \
         "$UNINSTALL_HOME/.steam/steam/config/depotcache" \
         "$UNINSTALL_HOME/.local/share/Steam/depotcache" \
         "$UNINSTALL_HOME/.steam/steam/depotcache" \
         "$UNINSTALL_HOME/.headcrab" \
         "$UNINSTALL_HOME/.local/bin" \
         "$UNINSTALL_HOME/.local/share/applications" \
         "$UNINSTALL_HOME/.config/navi" \
         "$UNINSTALL_HOME/.config/Tachibana Labs" \
         "$UNINSTALL_HOME/.local/share/navi/cache_backups"

# Category 1 files
echo "binary" > "$UNINSTALL_HOME/.local/share/navi/libnavi.so"
echo "binary" > "$LEGACY_APP_BIN"
echo "binary" > "$UNINSTALL_HOME/.headcrab/headcrab"

# Preserved manifest and lua files (strictly untouched, no deletion, no changes)
echo "untouchable_manifest_100" > "$LEGACY_MANIFESTS_DIR/100.manifest"
echo "untouchable_lua_script" > "$UNINSTALL_HOME/.steam/steam/config/stplug-in/100.lua"
echo "untouchable_plugin_manifest" > "$UNINSTALL_HOME/.steam/steam/config/stplug-in/100.manifest"
echo "untouchable_lua_script2" > "$UNINSTALL_HOME/.local/share/Steam/config/stplug-in/200.lua"
echo "untouchable_steam_depotcache_manifest" > "$UNINSTALL_HOME/.local/share/Steam/config/depotcache/789_101.manifest"
echo "untouchable_steam_depotcache_manifest2" > "$UNINSTALL_HOME/.steam/steam/config/depotcache/789_102.manifest"
echo "untouchable_steam_depotcache_manifest3" > "$UNINSTALL_HOME/.local/share/Steam/depotcache/789_103.manifest"

# Category 2 files: Steam wrapper and backup
echo "ORIGINAL_NATIVE_STEAM" > "$UNINSTALL_HOME/.local/bin/steam.pre-navi.bak"
echo "MODIFIED_NAVI_WRAPPER with navi LD_AUDIT" > "$UNINSTALL_HOME/.local/bin/steam"
echo "Desktop Entry" > "$UNINSTALL_HOME/.local/share/applications/$LEGACY_DESKTOP_NAME"
echo "tracking" > "$UNINSTALL_HOME/.navi.installed"

# Category 3 files: User configs
echo "important_user_settings: true" > "$UNINSTALL_HOME/.config/navi/config.yaml"
echo "cloud_settings: true" > "$UNINSTALL_HOME/.config/Tachibana Labs/settings.yaml"

# 7A: Run uninstaller keeping configs
echo "Running uninstall.sh preserving user configs..."
HOME="$UNINSTALL_HOME" SLS_UNINSTALL_KEEP_CONFIGS=1 "$REPO_ROOT/uninstall.sh"

# Verify Category 1 removed (except preserved morrenus_manifests)
if [ -d "$UNINSTALL_HOME/.local/share/navi" ] || [ -d "$UNINSTALL_HOME/.headcrab" ]; then
    fail "Category 1 directories were not removed!"
fi
if [ -f "$LEGACY_APP_BIN" ]; then
    fail "Legacy application binary was not removed!"
fi

# Verify morrenus_manifests PRESERVED intact
if [ ! -f "$LEGACY_MANIFESTS_DIR/100.manifest" ]; then
    fail "morrenus_manifests was deleted! Must be strictly preserved."
fi

# Verify stplug-in luas and manifests PRESERVED intact
if [ ! -f "$UNINSTALL_HOME/.steam/steam/config/stplug-in/100.lua" ] || \
   [ ! -f "$UNINSTALL_HOME/.steam/steam/config/stplug-in/100.manifest" ] || \
   [ ! -f "$UNINSTALL_HOME/.local/share/Steam/config/stplug-in/200.lua" ]; then
    fail "Steam plugin luas or manifests in stplug-in were deleted! Must be strictly preserved."
fi

# Verify Steam depotcache manifests PRESERVED intact
if [ ! -f "$UNINSTALL_HOME/.local/share/Steam/config/depotcache/789_101.manifest" ] || \
   [ ! -f "$UNINSTALL_HOME/.steam/steam/config/depotcache/789_102.manifest" ] || \
   [ ! -f "$UNINSTALL_HOME/.local/share/Steam/depotcache/789_103.manifest" ]; then
    fail "Steam depotcache manifests were deleted! Must be strictly preserved."
fi

# Verify Category 2 restored
if [ ! -f "$UNINSTALL_HOME/.local/bin/steam" ]; then
    fail "Native steam wrapper was not restored!"
fi
WRAPPER_CONTENT=$(cat "$UNINSTALL_HOME/.local/bin/steam")
if [ "$WRAPPER_CONTENT" != "ORIGINAL_NATIVE_STEAM" ]; then
    fail "Restored wrapper content does not match original backup!"
fi
if [ -f "$UNINSTALL_HOME/.local/share/applications/$LEGACY_DESKTOP_NAME" ]; then
    fail "Legacy desktop entry was not removed!"
fi
if [ -f "$UNINSTALL_HOME/.navi.installed" ]; then
    fail "Tracking file was not removed!"
fi

# Verify Category 3 PRESERVED
if [ ! -f "$UNINSTALL_HOME/.config/navi/config.yaml" ]; then
    fail "Category 3 user config was deleted when it should have been preserved!"
fi
if [ ! -f "$UNINSTALL_HOME/.config/Tachibana Labs/settings.yaml" ]; then
    fail "Category 3 Tachibana Labs config was deleted!"
fi
pass "Uninstaller correctly removed Category 1 & 2 while strictly preserving Category 3 configs, luas, and manifests."

# 7B: Run uninstaller removing configs
echo "Running uninstall.sh removing user configs..."
HOME="$UNINSTALL_HOME" SLS_UNINSTALL_REMOVE_CONFIGS=1 "$REPO_ROOT/uninstall.sh"

if [ -f "$UNINSTALL_HOME/.config/navi/config.yaml" ] || [ -f "$UNINSTALL_HOME/.config/Tachibana Labs/settings.yaml" ]; then
    fail "User configs were not removed when requested!"
fi

# Verify morrenus_manifests, stplug-in, and depotcache are STILL preserved even when configs are removed
if [ ! -f "$LEGACY_MANIFESTS_DIR/100.manifest" ]; then
    fail "morrenus_manifests was deleted during config removal! Must be strictly preserved."
fi
if [ ! -f "$UNINSTALL_HOME/.steam/steam/config/stplug-in/100.lua" ]; then
    fail "Steam plugin luas were deleted during config removal! Must be strictly preserved."
fi
if [ ! -f "$UNINSTALL_HOME/.local/share/Steam/config/depotcache/789_101.manifest" ] || \
   [ ! -f "$UNINSTALL_HOME/.steam/steam/config/depotcache/789_102.manifest" ] || \
   [ ! -f "$UNINSTALL_HOME/.local/share/Steam/depotcache/789_103.manifest" ]; then
    fail "Steam depotcache manifests were deleted during config removal! Must be strictly preserved."
fi
pass "Uninstaller correctly removed Category 3 user configs while strictly keeping manifests and luas."

# 7C: Idempotency test (rerunning uninstall.sh on empty state)
echo "Running uninstall.sh again to verify idempotency..."
HOME="$UNINSTALL_HOME" SLS_UNINSTALL_REMOVE_CONFIGS=1 "$REPO_ROOT/uninstall.sh"
pass "Uninstaller is completely idempotent."

step "Test Case 8: Atomic Staging, Directory Merging, and Cancellation Recovery"
echo "Compiling and running test_atomic_staging unit tests..."
if [ ! -x "$REPO_ROOT/bin/test_atomic_staging" ] && command -v g++ >/dev/null 2>&1; then
    g++ -std=c++20 -I"$REPO_ROOT/src/core" "$REPO_ROOT/tests/test_atomic_staging.cpp" -o "$REPO_ROOT/bin/test_atomic_staging"
fi
"$REPO_ROOT/bin/test_atomic_staging"
pass "Atomic staging, merge, and cancellation safety unit tests passed successfully."

step "Test Case 9: Live Steam UI Flow & Native Hook Integration Test"
set_legacy_paths "$HOME"
PYTHON_CMD=""
for cand in "$HOME/deck_venv/bin/python" python3 /usr/bin/python3 "$LEGACY_VENV_PYTHON"; do
    if command -v "$cand" >/dev/null 2>&1 && "$cand" -c "import websocket" >/dev/null 2>&1; then
        PYTHON_CMD="$cand"
        break
    fi
done
if [ -z "$PYTHON_CMD" ]; then
    PYTHON_CMD="python3"
fi

if pgrep -f "steam" >/dev/null 2>&1 && [ -d "$HOME/.local/share/Steam/steamapps" ] && curl -s --connect-timeout 2 "http://127.0.0.1:8080/json" >/dev/null 2>&1; then
    echo "Steam CEF remote debugging active on port 8080. Running UI automation flow test..."
    $PYTHON_CMD "$REPO_ROOT/tests/test_steam_ui_flow.py" --skip-if-no-steam
    pass "Live Steam UI flow test passed successfully."
else
    echo -e "${YELLOW}[SKIP]${NC} Steam is not currently running with CEF remote debugging on port 8080."
fi

step "ALL TEST CASES PASSED SUCCESSFULLY!"
echo -e "${GREEN}>>> All Test Cases passed with 0 errors.${NC}"
exit 0
