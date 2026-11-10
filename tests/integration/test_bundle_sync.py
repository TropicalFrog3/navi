"""
@file  test_bundle_sync.py
@brief Tests verifying bundle-app-details.py stays in sync with sources


Integration test for res/inject-scripts/library/app-details-script.js bundler.
Ensures that the bundled script remains 100% in sync with modular sources in
res/inject-scripts/library/app-details/ and passes JavaScript syntax checks.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path
import pytest


REPO_ROOT = Path(__file__).resolve().parent.parent.parent
BUNDLE_SCRIPT = REPO_ROOT / "tools" / "bundle-app-details.py"
MODULES_DIR = REPO_ROOT / "res" / "inject-scripts" / "library" / "app-details"
BUNDLED_SCRIPT = REPO_ROOT / "res" / "inject-scripts" / "library" / "app-details-script.js"

EXPECTED_FILES = [
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


def test_bundle_app_details_sync():
    """Verify that app-details-script.js matches tools/bundle-app-details.py output with zero drift."""
    assert BUNDLE_SCRIPT.is_file(), f"Bundler script not found at {BUNDLE_SCRIPT}"
    assert BUNDLED_SCRIPT.is_file(), f"Bundled script not found at {BUNDLED_SCRIPT}"

    res = subprocess.run(
        [sys.executable, str(BUNDLE_SCRIPT), "--check"],
        capture_output=True,
        text=True,
        cwd=str(REPO_ROOT),
    )
    assert res.returncode == 0, (
        f"bundle-app-details.py --check failed (returncode {res.returncode}):\n"
        f"STDOUT:\n{res.stdout}\nSTDERR:\n{res.stderr}\n"
        "Run 'make bundle-scripts' or 'python3 tools/bundle-app-details.py' to regenerate."
    )


def test_all_modular_files_exist():
    """Verify that all 18 modular decomposition files exist in app-details directory."""
    assert MODULES_DIR.is_dir(), f"Modules directory not found at {MODULES_DIR}"

    missing = [f for f in EXPECTED_FILES if not (MODULES_DIR / f).is_file()]
    assert not missing, f"Missing modular files in {MODULES_DIR}: {missing}"


def test_bundled_script_node_syntax():
    """Verify that bundled app-details-script.js is syntactically valid JavaScript."""
    if not shutil.which("node"):
        pytest.skip("node is not installed on this system")

    res = subprocess.run(
        ["node", "--check", str(BUNDLED_SCRIPT)],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0, f"node --check failed for {BUNDLED_SCRIPT}:\n{res.stderr}"


@pytest.mark.parametrize("filename", [f for f in EXPECTED_FILES if f.endswith(".js")])
def test_modular_js_node_syntax(filename: str):
    """Verify that each individual modular .js file is syntactically valid JavaScript."""
    if not shutil.which("node"):
        pytest.skip("node is not installed on this system")

    filepath = MODULES_DIR / filename
    res = subprocess.run(
        ["node", "--check", str(filepath)],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0, f"node --check failed for {filepath}:\n{res.stderr}"

