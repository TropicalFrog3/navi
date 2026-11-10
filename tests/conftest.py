"""
@file  conftest.py
@brief Shared pytest fixtures and test environment setup configuration


Pytest configuration and shared fixtures for Navi integration tests.
"""

import os
import sys
import subprocess
from pathlib import Path
import pytest

REPO_ROOT = Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))


def pytest_addoption(parser):
    parser.addoption(
        "--live",
        action="store_true",
        default=False,
        help="Run live Steam UI flow tests against a running Steam client with CEF debugging",
    )


def pytest_configure(config):
    config.addinivalue_line(
        "markers",
        "live: marks tests as requiring a running Steam client with CEF remote debugging",
    )


def pytest_collection_modifyitems(config, items):
    if config.getoption("--live"):
        return
    # Run live tests against the real Steam client when port 8080 is reachable or Steam is installed
    import shutil
    import urllib.request
    try:
        urllib.request.urlopen("http://127.0.0.1:8080/json/version", timeout=1)
        return
    except Exception:
        pass
    if os.path.isfile(os.path.expanduser("~/.local/bin/steam")) or shutil.which("steam"):
        return
    skip_live = pytest.mark.skip(reason="Real Steam client is not available on this host")
    for item in items:
        if "live" in item.keywords:
            item.add_marker(skip_live)


@pytest.fixture
def fake_home(tmp_path, monkeypatch):
    """
    Creates an isolated temporary HOME environment with a mock native Steam launcher.
    Sets HOME and NAVI_IGNORE_RUNNING_STEAM=1 by default.
    """
    home = tmp_path / "home"
    home.mkdir(parents=True, exist_ok=True)

    # Create mock native Steam launcher script
    steam_dir = home / ".local" / "share" / "Steam"
    steam_dir.mkdir(parents=True, exist_ok=True)
    mock_steam_sh = steam_dir / "steam.sh"
    mock_steam_sh.write_text("#!/bin/sh\nexec true\n")
    mock_steam_sh.chmod(0o755)

    # Create mock shell startup files
    (home / ".profile").write_text("# mock .profile\n")
    (home / ".bashrc").write_text("# mock .bashrc\n")

    monkeypatch.setenv("HOME", str(home))
    monkeypatch.setenv("NAVI_IGNORE_RUNNING_STEAM", "1")
    monkeypatch.setenv("NAVI_OFFLINE_INSTALL", "1")
    monkeypatch.setenv("REPO_ROOT", str(REPO_ROOT))

    return home


@pytest.fixture
def run_installer(fake_home):
    """
    Helper fixture to execute install.sh within the isolated fake_home environment.
    """
    def _run(*args, env=None, check=False):
        cmd_env = os.environ.copy()
        if env:
            cmd_env.update(env)
        cmd = [str(REPO_ROOT / "install.sh"), *args]
        return subprocess.run(
            cmd,
            cwd=str(REPO_ROOT),
            env=cmd_env,
            capture_output=True,
            text=True,
            check=check,
        )

    return _run
