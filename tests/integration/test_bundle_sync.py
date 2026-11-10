"""
@file  test_bundle_sync.py
@brief Tests verifying bundle-app-details.py, bundle-cloud-manager.py, bundle-navi-inject.py, bundle-import-lua.py, bundle-depot-selection.py, and bundle-store-page.py stay in sync with sources

Integration test for modular CEF inject script bundlers:
- res/inject-scripts/library/app-details-script.js (from library/app-details/)
- res/inject-scripts/cloud/cloud_manager.js (from cloud/cloud-manager/)
- res/inject-scripts/core/navi_inject.js (from core/navi-inject/)
- res/inject-scripts/shell/import-lua-script.js (from shell/import-lua/)
- res/inject-scripts/store/depot-selection-script.js (from store/depot-selection/)
- res/inject-scripts/store/store-page-script.js (from store/store-page/)
Ensures that the bundled scripts remain 100% in sync with modular sources
and pass JavaScript syntax checks.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path
import pytest


REPO_ROOT = Path(__file__).resolve().parent.parent.parent

# App Details Bundler
BUNDLE_APP_DETAILS_SCRIPT = REPO_ROOT / "tools" / "bundle-app-details.py"
APP_DETAILS_MODULES_DIR = REPO_ROOT / "res" / "inject-scripts" / "library" / "app-details"
APP_DETAILS_BUNDLED_SCRIPT = REPO_ROOT / "res" / "inject-scripts" / "library" / "app-details-script.js"

APP_DETAILS_EXPECTED_FILES = [
    "styles.css",
    "gamepad.js",
    "appid.js",
    "cloud-api.js",
    "game-manager-modal.html",
    "game-manager-modal.js",
    "provider-modal.html",
    "provider-modal.js",
    "remove-modal.html",
    "remove-modal.js",
    "restore-modal.html",
    "restore-modal.js",
    "reset-modal.html",
    "reset-modal.js",
    "navi-modal.html",
    "navi-modal.js",
    "buttons.js",
    "main.js",
]

# Cloud Manager Bundler
BUNDLE_CLOUD_MANAGER_SCRIPT = REPO_ROOT / "tools" / "bundle-cloud-manager.py"
CLOUD_MANAGER_MODULES_DIR = REPO_ROOT / "res" / "inject-scripts" / "cloud" / "cloud-manager"
CLOUD_MANAGER_BUNDLED_SCRIPT = REPO_ROOT / "res" / "inject-scripts" / "cloud" / "cloud_manager.js"

CLOUD_MANAGER_EXPECTED_FILES = [
    "styles.css",
    "state.js",
    "security.js",
    "gamepad.js",
    "pending-banner.js",
    "api.js",
    "reset-modal.html",
    "reset-modal.js",
    "restore-modal.html",
    "restore-modal.js",
    "settings-modal.html",
    "settings-modal.js",
    "utils.js",
    "cloud-files-modal.html",
    "cloud-files-modal.js",
    "main.js",
]

# Navi Inject Bundler
BUNDLE_NAVI_INJECT_SCRIPT = REPO_ROOT / "tools" / "bundle-navi-inject.py"
NAVI_INJECT_MODULES_DIR = REPO_ROOT / "res" / "inject-scripts" / "core" / "navi-inject"
NAVI_INJECT_BUNDLED_SCRIPT = REPO_ROOT / "res" / "inject-scripts" / "core" / "navi_inject.js"

NAVI_INJECT_EXPECTED_FILES = [
    "pending-banner.html",
    "pending-banner.js",
    "cloud-fallback.js",
    "disk-space-api.js",
    "steam-apps-patch.js",
    "main.js",
]

# Import Lua Bundler
BUNDLE_IMPORT_LUA_SCRIPT = REPO_ROOT / "tools" / "bundle-import-lua.py"
IMPORT_LUA_MODULES_DIR = REPO_ROOT / "res" / "inject-scripts" / "shell" / "import-lua"
IMPORT_LUA_BUNDLED_SCRIPT = REPO_ROOT / "res" / "inject-scripts" / "shell" / "import-lua-script.js"

IMPORT_LUA_EXPECTED_FILES = [
    "styles.css",
    "manual-install.html",
    "utils.js",
    "styles.js",
    "cloud-file-viewer.js",
    "config-helpers.js",
    "config-sections.js",
    "config-dialogs.js",
    "config-controller.js",
    "manual-install.js",
    "supernav.js",
    "main.js",
]

# Depot Selection Bundler
BUNDLE_DEPOT_SELECTION_SCRIPT = REPO_ROOT / "tools" / "bundle-depot-selection.py"
DEPOT_SELECTION_MODULES_DIR = REPO_ROOT / "res" / "inject-scripts" / "store" / "depot-selection"
DEPOT_SELECTION_BUNDLED_SCRIPT = REPO_ROOT / "res" / "inject-scripts" / "store" / "depot-selection-script.js"

DEPOT_SELECTION_EXPECTED_FILES = [
    "styles.css",
    "modal.html",
    "gamepad.js",
    "utils.js",
    "styles.js",
    "depot-resolver.js",
    "main.js",
]

# Store Page Bundler
BUNDLE_STORE_PAGE_SCRIPT = REPO_ROOT / "tools" / "bundle-store-page.py"
STORE_PAGE_MODULES_DIR = REPO_ROOT / "res" / "inject-scripts" / "store" / "store-page"
STORE_PAGE_BUNDLED_SCRIPT = REPO_ROOT / "res" / "inject-scripts" / "store" / "store-page-script.js"

STORE_PAGE_EXPECTED_FILES = [
    "styles.css",
    "provider-modal.html",
    "remove-modal.html",
    "config-modal.html",
    "styles.js",
    "cdp-bridge.js",
    "gamepad.js",
    "provider-config.js",
    "download-tracker.js",
    "modals.js",
    "buttons.js",
    "ownership-decoration.js",
    "main.js",
]


def test_bundle_app_details_sync():
    """Verify that app-details-script.js matches tools/bundle-app-details.py output with zero drift."""
    assert BUNDLE_APP_DETAILS_SCRIPT.is_file(), f"Bundler script not found at {BUNDLE_APP_DETAILS_SCRIPT}"
    assert APP_DETAILS_BUNDLED_SCRIPT.is_file(), f"Bundled script not found at {APP_DETAILS_BUNDLED_SCRIPT}"

    res = subprocess.run(
        [sys.executable, str(BUNDLE_APP_DETAILS_SCRIPT), "--check"],
        capture_output=True,
        text=True,
        cwd=str(REPO_ROOT),
    )
    assert res.returncode == 0, (
        f"bundle-app-details.py --check failed (returncode {res.returncode}):\n"
        f"STDOUT:\n{res.stdout}\nSTDERR:\n{res.stderr}\n"
        "Run 'make bundle-scripts' or 'python3 tools/bundle-app-details.py' to regenerate."
    )


def test_all_app_details_modular_files_exist():
    """Verify that all 18 modular decomposition files exist in app-details directory."""
    assert APP_DETAILS_MODULES_DIR.is_dir(), f"Modules directory not found at {APP_DETAILS_MODULES_DIR}"

    missing = [f for f in APP_DETAILS_EXPECTED_FILES if not (APP_DETAILS_MODULES_DIR / f).is_file()]
    assert not missing, f"Missing modular files in {APP_DETAILS_MODULES_DIR}: {missing}"


def test_app_details_bundled_script_node_syntax():
    """Verify that bundled app-details-script.js is syntactically valid JavaScript."""
    if not shutil.which("node"):
        pytest.skip("node is not installed on this system")

    res = subprocess.run(
        ["node", "--check", str(APP_DETAILS_BUNDLED_SCRIPT)],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0, f"node --check failed for {APP_DETAILS_BUNDLED_SCRIPT}:\n{res.stderr}"


@pytest.mark.parametrize("filename", [f for f in APP_DETAILS_EXPECTED_FILES if f.endswith(".js")])
def test_app_details_modular_js_node_syntax(filename: str):
    """Verify that each individual app-details modular .js file is syntactically valid JavaScript."""
    if not shutil.which("node"):
        pytest.skip("node is not installed on this system")

    filepath = APP_DETAILS_MODULES_DIR / filename
    res = subprocess.run(
        ["node", "--check", str(filepath)],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0, f"node --check failed for {filepath}:\n{res.stderr}"


def test_bundle_cloud_manager_sync():
    """Verify that cloud_manager.js matches tools/bundle-cloud-manager.py output with zero drift."""
    assert BUNDLE_CLOUD_MANAGER_SCRIPT.is_file(), f"Bundler script not found at {BUNDLE_CLOUD_MANAGER_SCRIPT}"
    assert CLOUD_MANAGER_BUNDLED_SCRIPT.is_file(), f"Bundled script not found at {CLOUD_MANAGER_BUNDLED_SCRIPT}"

    res = subprocess.run(
        [sys.executable, str(BUNDLE_CLOUD_MANAGER_SCRIPT), "--check"],
        capture_output=True,
        text=True,
        cwd=str(REPO_ROOT),
    )
    assert res.returncode == 0, (
        f"bundle-cloud-manager.py --check failed (returncode {res.returncode}):\n"
        f"STDOUT:\n{res.stdout}\nSTDERR:\n{res.stderr}\n"
        "Run 'make bundle-scripts' or 'python3 tools/bundle-cloud-manager.py' to regenerate."
    )


def test_all_cloud_manager_modular_files_exist():
    """Verify that all 16 modular decomposition files exist in cloud-manager directory."""
    assert CLOUD_MANAGER_MODULES_DIR.is_dir(), f"Modules directory not found at {CLOUD_MANAGER_MODULES_DIR}"

    missing = [f for f in CLOUD_MANAGER_EXPECTED_FILES if not (CLOUD_MANAGER_MODULES_DIR / f).is_file()]
    assert not missing, f"Missing modular files in {CLOUD_MANAGER_MODULES_DIR}: {missing}"


def test_cloud_manager_bundled_script_node_syntax():
    """Verify that bundled cloud_manager.js is syntactically valid JavaScript."""
    if not shutil.which("node"):
        pytest.skip("node is not installed on this system")

    res = subprocess.run(
        ["node", "--check", str(CLOUD_MANAGER_BUNDLED_SCRIPT)],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0, f"node --check failed for {CLOUD_MANAGER_BUNDLED_SCRIPT}:\n{res.stderr}"


@pytest.mark.parametrize("filename", [f for f in CLOUD_MANAGER_EXPECTED_FILES if f.endswith(".js")])
def test_cloud_manager_modular_js_node_syntax(filename: str):
    """Verify that each individual cloud-manager modular .js file is syntactically valid JavaScript."""
    if not shutil.which("node"):
        pytest.skip("node is not installed on this system")

    filepath = CLOUD_MANAGER_MODULES_DIR / filename
    res = subprocess.run(
        ["node", "--check", str(filepath)],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0, f"node --check failed for {filepath}:\n{res.stderr}"


def test_bundle_navi_inject_sync():
    """Verify that navi_inject.js matches tools/bundle-navi-inject.py output with zero drift."""
    assert BUNDLE_NAVI_INJECT_SCRIPT.is_file(), f"Bundler script not found at {BUNDLE_NAVI_INJECT_SCRIPT}"
    assert NAVI_INJECT_BUNDLED_SCRIPT.is_file(), f"Bundled script not found at {NAVI_INJECT_BUNDLED_SCRIPT}"

    res = subprocess.run(
        [sys.executable, str(BUNDLE_NAVI_INJECT_SCRIPT), "--check"],
        capture_output=True,
        text=True,
        cwd=str(REPO_ROOT),
    )
    assert res.returncode == 0, (
        f"bundle-navi-inject.py --check failed (returncode {res.returncode}):\n"
        f"STDOUT:\n{res.stdout}\nSTDERR:\n{res.stderr}\n"
        "Run 'make bundle-scripts' or 'python3 tools/bundle-navi-inject.py' to regenerate."
    )


def test_all_navi_inject_modular_files_exist():
    """Verify that all 6 modular decomposition files exist in navi-inject directory."""
    assert NAVI_INJECT_MODULES_DIR.is_dir(), f"Modules directory not found at {NAVI_INJECT_MODULES_DIR}"

    missing = [f for f in NAVI_INJECT_EXPECTED_FILES if not (NAVI_INJECT_MODULES_DIR / f).is_file()]
    assert not missing, f"Missing modular files in {NAVI_INJECT_MODULES_DIR}: {missing}"


def test_navi_inject_bundled_script_node_syntax():
    """Verify that bundled navi_inject.js is syntactically valid JavaScript."""
    if not shutil.which("node"):
        pytest.skip("node is not installed on this system")

    res = subprocess.run(
        ["node", "--check", str(NAVI_INJECT_BUNDLED_SCRIPT)],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0, f"node --check failed for {NAVI_INJECT_BUNDLED_SCRIPT}:\n{res.stderr}"


@pytest.mark.parametrize("filename", [f for f in NAVI_INJECT_EXPECTED_FILES if f.endswith(".js")])
def test_navi_inject_modular_js_node_syntax(filename: str):
    """Verify that each individual navi-inject modular .js file is syntactically valid JavaScript."""
    if not shutil.which("node"):
        pytest.skip("node is not installed on this system")

    filepath = NAVI_INJECT_MODULES_DIR / filename
    res = subprocess.run(
        ["node", "--check", str(filepath)],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0, f"node --check failed for {filepath}:\n{res.stderr}"


def test_bundle_import_lua_sync():
    """Verify that import-lua-script.js matches tools/bundle-import-lua.py output with zero drift."""
    assert BUNDLE_IMPORT_LUA_SCRIPT.is_file(), f"Bundler script not found at {BUNDLE_IMPORT_LUA_SCRIPT}"
    assert IMPORT_LUA_BUNDLED_SCRIPT.is_file(), f"Bundled script not found at {IMPORT_LUA_BUNDLED_SCRIPT}"

    res = subprocess.run(
        [sys.executable, str(BUNDLE_IMPORT_LUA_SCRIPT), "--check"],
        capture_output=True,
        text=True,
        cwd=str(REPO_ROOT),
    )
    assert res.returncode == 0, (
        f"bundle-import-lua.py --check failed (returncode {res.returncode}):\n"
        f"STDOUT:\n{res.stdout}\nSTDERR:\n{res.stderr}\n"
        "Run 'make bundle-scripts' or 'python3 tools/bundle-import-lua.py' to regenerate."
    )


def test_all_import_lua_modular_files_exist():
    """Verify that all 12 modular decomposition files exist in import-lua directory."""
    assert IMPORT_LUA_MODULES_DIR.is_dir(), f"Modules directory not found at {IMPORT_LUA_MODULES_DIR}"

    missing = [f for f in IMPORT_LUA_EXPECTED_FILES if not (IMPORT_LUA_MODULES_DIR / f).is_file()]
    assert not missing, f"Missing modular files in {IMPORT_LUA_MODULES_DIR}: {missing}"


def test_import_lua_bundled_script_node_syntax():
    """Verify that bundled import-lua-script.js is syntactically valid JavaScript."""
    if not shutil.which("node"):
        pytest.skip("node is not installed on this system")

    res = subprocess.run(
        ["node", "--check", str(IMPORT_LUA_BUNDLED_SCRIPT)],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0, f"node --check failed for {IMPORT_LUA_BUNDLED_SCRIPT}:\n{res.stderr}"


@pytest.mark.parametrize("filename", [f for f in IMPORT_LUA_EXPECTED_FILES if f.endswith(".js")])
def test_import_lua_modular_js_node_syntax(filename: str):
    """Verify that each individual import-lua modular .js file is syntactically valid JavaScript."""
    if not shutil.which("node"):
        pytest.skip("node is not installed on this system")

    filepath = IMPORT_LUA_MODULES_DIR / filename
    res = subprocess.run(
        ["node", "--check", str(filepath)],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0, f"node --check failed for {filepath}:\n{res.stderr}"


def test_bundle_depot_selection_sync():
    """Verify that depot-selection-script.js matches tools/bundle-depot-selection.py output with zero drift."""
    assert BUNDLE_DEPOT_SELECTION_SCRIPT.is_file(), f"Bundler script not found at {BUNDLE_DEPOT_SELECTION_SCRIPT}"
    assert DEPOT_SELECTION_BUNDLED_SCRIPT.is_file(), f"Bundled script not found at {DEPOT_SELECTION_BUNDLED_SCRIPT}"

    res = subprocess.run(
        [sys.executable, str(BUNDLE_DEPOT_SELECTION_SCRIPT), "--check"],
        capture_output=True,
        text=True,
        cwd=str(REPO_ROOT),
    )
    assert res.returncode == 0, (
        f"bundle-depot-selection.py --check failed (returncode {res.returncode}):\n"
        f"STDOUT:\n{res.stdout}\nSTDERR:\n{res.stderr}\n"
        "Run 'make bundle-scripts' or 'python3 tools/bundle-depot-selection.py' to regenerate."
    )


def test_all_depot_selection_modular_files_exist():
    """Verify that all 7 modular decomposition files exist in depot-selection directory."""
    assert DEPOT_SELECTION_MODULES_DIR.is_dir(), f"Modules directory not found at {DEPOT_SELECTION_MODULES_DIR}"

    missing = [f for f in DEPOT_SELECTION_EXPECTED_FILES if not (DEPOT_SELECTION_MODULES_DIR / f).is_file()]
    assert not missing, f"Missing modular files in {DEPOT_SELECTION_MODULES_DIR}: {missing}"


def test_depot_selection_bundled_script_node_syntax():
    """Verify that bundled depot-selection-script.js is syntactically valid JavaScript."""
    if not shutil.which("node"):
        pytest.skip("node is not installed on this system")

    res = subprocess.run(
        ["node", "--check", str(DEPOT_SELECTION_BUNDLED_SCRIPT)],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0, f"node --check failed for {DEPOT_SELECTION_BUNDLED_SCRIPT}:\n{res.stderr}"


@pytest.mark.parametrize("filename", [f for f in DEPOT_SELECTION_EXPECTED_FILES if f.endswith(".js")])
def test_depot_selection_modular_js_node_syntax(filename: str):
    """Verify that each individual depot-selection modular .js file is syntactically valid JavaScript."""
    if not shutil.which("node"):
        pytest.skip("node is not installed on this system")

    filepath = DEPOT_SELECTION_MODULES_DIR / filename
    res = subprocess.run(
        ["node", "--check", str(filepath)],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0, f"node --check failed for {filepath}:\n{res.stderr}"


def test_bundle_store_page_sync():
    """Verify that store-page-script.js matches tools/bundle-store-page.py output with zero drift."""
    assert BUNDLE_STORE_PAGE_SCRIPT.is_file(), f"Bundler script not found at {BUNDLE_STORE_PAGE_SCRIPT}"
    assert STORE_PAGE_BUNDLED_SCRIPT.is_file(), f"Bundled script not found at {STORE_PAGE_BUNDLED_SCRIPT}"

    res = subprocess.run(
        [sys.executable, str(BUNDLE_STORE_PAGE_SCRIPT), "--check"],
        capture_output=True,
        text=True,
        cwd=str(REPO_ROOT),
    )
    assert res.returncode == 0, (
        f"bundle-store-page.py --check failed (returncode {res.returncode}):\n"
        f"STDOUT:\n{res.stdout}\nSTDERR:\n{res.stderr}\n"
        "Run 'make bundle-scripts' or 'python3 tools/bundle-store-page.py' to regenerate."
    )


def test_all_store_page_modular_files_exist():
    """Verify that all 13 modular decomposition files exist in store-page directory."""
    assert STORE_PAGE_MODULES_DIR.is_dir(), f"Modules directory not found at {STORE_PAGE_MODULES_DIR}"

    missing = [f for f in STORE_PAGE_EXPECTED_FILES if not (STORE_PAGE_MODULES_DIR / f).is_file()]
    assert not missing, f"Missing modular files in {STORE_PAGE_MODULES_DIR}: {missing}"


def test_store_page_bundled_script_node_syntax():
    """Verify that bundled store-page-script.js is syntactically valid JavaScript."""
    if not shutil.which("node"):
        pytest.skip("node is not installed on this system")

    res = subprocess.run(
        ["node", "--check", str(STORE_PAGE_BUNDLED_SCRIPT)],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0, f"node --check failed for {STORE_PAGE_BUNDLED_SCRIPT}:\n{res.stderr}"


@pytest.mark.parametrize("filename", [f for f in STORE_PAGE_EXPECTED_FILES if f.endswith(".js")])
def test_store_page_modular_js_node_syntax(filename: str):
    """Verify that each individual store-page modular .js file is syntactically valid JavaScript."""
    if not shutil.which("node"):
        pytest.skip("node is not installed on this system")

    filepath = STORE_PAGE_MODULES_DIR / filename
    res = subprocess.run(
        ["node", "--check", str(filepath)],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0, f"node --check failed for {filepath}:\n{res.stderr}"



