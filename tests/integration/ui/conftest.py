"""
Pytest configuration and shared fixtures for UI integration tests.
"""

from __future__ import annotations

import os
from pathlib import Path
from typing import Generator
import pytest

from tests.integration.ui.harness.browser_driver import BrowserDriver
from tests.integration.ui.harness.callback_server_stub import CallbackServerStub
from tests.integration.ui.harness.navi_queue_bridge import NaviQueueBridge


REPO_ROOT = Path(__file__).resolve().parents[3]
FIXTURES_DIR = REPO_ROOT / "tests" / "fixtures" / "ui_shells"
SCRIPTS_DIR = REPO_ROOT / "res" / "inject-scripts"


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item: pytest.Item, call: pytest.CallInfo) -> Generator[None, pytest.TestReport, None]:
    outcome = yield
    report = outcome.get_result()
    setattr(item, f"rep_{report.when}", report)


@pytest.fixture(scope="session")
def repo_paths() -> dict[str, Path]:
    return {
        "root": REPO_ROOT,
        "fixtures": FIXTURES_DIR,
        "scripts": SCRIPTS_DIR,
    }


@pytest.fixture
def callback_server() -> Generator[CallbackServerStub, None, None]:
    """Start an ephemeral-port callback server stub for emulating Navi port 9001."""
    server = CallbackServerStub(port=0).start()
    try:
        yield server
    finally:
        server.stop()


@pytest.fixture
def driver(callback_server: CallbackServerStub, request: pytest.FixtureRequest) -> Generator[BrowserDriver, None, None]:
    """Launch headless Chrome with CDP interception linked to callback_server."""
    drv = BrowserDriver(callback_server=callback_server, headless=True).start()
    try:
        yield drv
    finally:
        screenshots_dir = os.environ.get("NAVI_UI_SCREENSHOTS_DIR")
        if screenshots_dir:
            try:
                drv.save_test_screenshot(request.node.name, screenshots_dir)
            except Exception:
                pass
        call_rep = getattr(request.node, "rep_call", None)
        if call_rep and call_rep.failed:
            try:
                drv.capture_failure_artifacts(request.node.name)
            except Exception:
                pass
        drv.stop()


@pytest.fixture
def queue_bridge(driver: BrowserDriver, callback_server: CallbackServerStub) -> Generator[NaviQueueBridge, None, None]:
    """Create a NaviQueueBridge instance for store page testing."""
    bridge = NaviQueueBridge(driver=driver, callback_server=callback_server)
    try:
        yield bridge
    finally:
        bridge.stop()

