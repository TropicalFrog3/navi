"""
UJ-03: Manual Installation Drag-and-Drop and File Staging Integration Tests.

Scenarios:
  1. Opening #sls-top-tab (#sls-page-container) and drag-and-drop visual states (dragover/dragleave).
  2. Validation on missing AppID and corrupt .zip response, asserting inline error states and recovery.
  3. Multi-file staging, staged file toggle/removal, EndpointBarrier in-flight verification, and final success message.
"""

from __future__ import annotations

import json
from pathlib import Path
import pytest

from tests.integration.ui.harness.browser_driver import BrowserDriver
from tests.integration.ui.harness.callback_server_stub import CallbackServerStub
from tests.integration.ui.pages.import_config_page import ImportConfigPage


@pytest.fixture
def setup_import_page(driver: BrowserDriver, repo_paths: dict[str, Path]) -> ImportConfigPage:
    """Load steam library shell and inject jszip and import-lua-script.js."""
    driver.navigate_to_file(repo_paths["fixtures"] / "steam_library_sp.html")
    page = ImportConfigPage(driver)
    page.inject_scripts(
        import_script_path=repo_paths["scripts"] / "import-lua-script.js",
        jszip_path=repo_paths["scripts"] / "jszip.min.js",
    )
    return page


@pytest.mark.ui_p0
def test_uj03_tab_and_dragover_visual_state(setup_import_page: ImportConfigPage) -> None:
    """Opening #sls-top-tab opens #sls-page-container; dragover and dragleave update border styling."""
    page = setup_import_page
    page.open()
    assert page.is_open() is True

    # Initial dropzone border
    initial_style = page.get_dropzone_style()
    assert initial_style["borderColor"] != ""

    # Dragover activates blue highlight border
    page.dispatch_drag_over()
    over_style = page.get_dropzone_style()
    assert "26, 159, 255" in over_style["borderColor"] or "#1a9fff" in over_style["borderColor"]

    # Dragleave restores default border
    page.dispatch_drag_leave()
    leave_style = page.get_dropzone_style()
    assert "26, 159, 255" not in leave_style["borderColor"]


@pytest.mark.ui_p0
def test_uj03_validation_missing_appid_and_corrupt_zip(
    setup_import_page: ImportConfigPage,
    callback_server: CallbackServerStub,
) -> None:
    """Missing AppID validation error, followed by corrupt .zip error and form recovery without reload."""
    page = setup_import_page
    page.open()

    # Step 1: Missing / empty AppID
    page.stage_files([{"name": "invalid.lua", "content": "-- no appid in this script"}])
    page.wait_for_file_list_visible()
    assert page.is_submit_disabled() is True

    # Trigger submit attempt
    page.click_submit(force=True)
    page.wait_for_status_containing("Could not detect AppID")
    assert "255, 77, 77" in page.get_status_color() or "#ff4d4d" in page.get_status_color()

    # Step 2: Corrupt .zip backend error handling
    callback_server.set_response(
        "/manual-install",
        {
            "success": False,
            "message": "Uploaded file bad.zip is not a valid zip archive.",
        },
        status_code=200,
    )

    page.stage_files([
        {"name": "1443430.lua", "content": "addappid(1443430)"},
    ])
    page.wait_for_submit_enabled()

    page.click_submit()
    page.wait_for_status_containing("Uploaded file bad.zip is not a valid zip archive")

    # Form recovery: submit button re-enables without page reload
    page.wait_for_submit_enabled()
    assert page.is_submit_disabled() is False


@pytest.mark.ui_p0
def test_uj03_multifile_staging_removal_and_barrier(
    setup_import_page: ImportConfigPage,
    callback_server: CallbackServerStub,
) -> None:
    """Multi-file staging, removing a staged file before submit, and asserting barrier in-flight state."""
    page = setup_import_page
    page.open()
    callback_server.clear_requests()

    # Stage Lua and manifest files
    page.stage_files([
        {"name": "1443430.lua", "content": "addappid(1443430)"},
        {"name": "1443431.manifest", "content": "depotcache_manifest_bytes"},
    ])

    page.wait_for_file_list_visible()
    assert page.is_file_list_visible() is True
    assert page.get_staged_file_count() >= 1
    page.wait_for_submit_enabled()

    # Toggle / uncheck manifest file
    page.toggle_file("1443431")

    # Hold /manual-install on barrier
    barrier = callback_server.hold_endpoint("/manual-install")

    page.click_submit()
    barrier.wait_until_reached(timeout=5.0)

    # In-flight assertions
    assert page.is_submit_disabled() is True
    page.wait_for_submit_text("Processing")

    # Release barrier
    barrier.release()

    # Verify final success state
    page.wait_for_status_containing("Files installed successfully")

    # Assert request was recorded
    reqs = callback_server.get_requests("/manual-install", method="POST")
    assert len(reqs) == 1
    req_body = json.loads(reqs[0]["body"].decode("utf-8"))
    assert str(req_body.get("appid")) == "1443430"
    assert len(req_body.get("files", [])) > 0
