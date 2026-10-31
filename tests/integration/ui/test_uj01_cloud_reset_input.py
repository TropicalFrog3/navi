"""
UJ-01: Cloud Reset Input Validation and Lifecycle Integration Tests.

Scenarios:
  1. Happy path: "RESET" enables button, sends POST /cloud/apps/1443430/reset, closes modal, records alert.
  2. Invalid & partial inputs keep Confirm disabled with opacity: 0.4.
  3. Whitespace trimming and real-time Backspace editing.
  4. Concurrent triple-click while held on EndpointBarrier ("Resetting..." state, disabled, exactly 1 POST).
  5. Rollback on HTTP 500 fault: modal stays open, button reverts to "Confirm Reset" and re-enables, error alert captured.
  6. Partial input cancelled via Cancel button, Close X button, and gamepad vgp_oncancel sends 0 POST requests.
"""

from __future__ import annotations

from pathlib import Path
import pytest

from tests.integration.ui.harness.browser_driver import BrowserDriver
from tests.integration.ui.harness.callback_server_stub import CallbackServerStub
from tests.integration.ui.pages.cloud_settings_page import CloudResetModal, CloudSettingsPage


@pytest.fixture
def setup_library_shell(driver: BrowserDriver, repo_paths: dict[str, Path]) -> CloudSettingsPage:
    """Navigate to library fixture shell and inject cloud_manager.js."""
    driver.navigate_to_file(repo_paths["fixtures"] / "steam_library_sp.html")
    page = CloudSettingsPage(driver)
    page.inject_cloud_manager(repo_paths["scripts"] / "cloud_manager.js")
    return page


@pytest.mark.ui_p0
def test_uj01_happy_path_reset(
    setup_library_shell: CloudSettingsPage,
    callback_server: CallbackServerStub,
) -> None:
    """Happy path: Typing 'RESET' enables confirm, dispatches POST, closes modal, records alert."""
    page = setup_library_shell
    modal = page.reset_modal
    modal.open(1443430)

    # Validate ARIA semantics
    assert modal.get_role() == "dialog"
    assert modal.get_aria_modal() == "true"
    assert modal.get_data_testid() == "sls-reset-modal"
    assert modal.is_confirm_disabled() is True

    # Type exact confirmation
    modal.type_input("RESET")
    modal.wait_for_confirm_enabled()

    modal.clear_captured_alerts()
    modal.click_confirm()
    modal.wait_until_closed()

    # Verify backend received the POST
    reqs = callback_server.get_requests("/cloud/apps/1443430/reset", method="POST")
    assert len(reqs) == 1
    assert reqs[0]["method"] == "POST"

    # Verify captured alert
    alert = modal.wait_for_alert_containing("Cloud save reset enqueued")
    assert alert is not None


@pytest.mark.ui_p0
def test_uj01_invalid_and_partial_inputs_keep_disabled(
    setup_library_shell: CloudSettingsPage,
) -> None:
    """Invalid and partial inputs must keep Confirm Reset disabled with opacity: 0.4."""
    page = setup_library_shell
    modal = page.reset_modal
    modal.open(1443430)

    invalid_cases = [
        "",
        "reset",
        "RESE",
        "RESET!",
        "<img src=x onerror=alert(1)>",
        "A" * 1000,
    ]

    for inv in invalid_cases:
        modal.clear_and_type_input(inv)
        assert modal.is_confirm_disabled() is True, f"Button should be disabled for input '{inv[:20]}'"
        assert modal.get_confirm_opacity() <= 0.45, f"Button opacity should be <= 0.4 for input '{inv[:20]}'"

    modal.click_close()
    modal.wait_until_closed()


@pytest.mark.ui_p0
def test_uj01_whitespace_trimming_and_backspace_editing(
    setup_library_shell: CloudSettingsPage,
) -> None:
    """Whitespace trimming accepts '  RESET  ', invalidates on extra char, and recovers on Backspace."""
    page = setup_library_shell
    modal = page.reset_modal
    modal.open(1443430)

    # Leading and trailing whitespace should be trimmed
    modal.clear_and_type_input("  RESET  ")
    modal.wait_for_confirm_enabled()
    assert modal.is_confirm_disabled() is False

    # Appending an exclamation mark should disable it
    modal.type_input("!")
    modal.wait_for_confirm_disabled()
    assert modal.is_confirm_disabled() is True

    # Pressing Backspace removes '!' and re-enables confirm
    modal.press_backspace()
    modal.wait_for_confirm_enabled()
    assert modal.is_confirm_disabled() is False

    modal.click_cancel()
    modal.wait_until_closed()


@pytest.mark.ui_p0
def test_uj01_concurrent_triple_click_held_on_barrier(
    setup_library_shell: CloudSettingsPage,
    callback_server: CallbackServerStub,
) -> None:
    """Concurrent triple clicks while held on barrier asserts 'Resetting...' state and exactly 1 request."""
    page = setup_library_shell
    modal = page.reset_modal
    modal.open(1443430)

    callback_server.clear_requests()
    barrier = callback_server.hold_endpoint("/cloud/apps/1443430/reset")

    modal.type_input("RESET")
    modal.wait_for_confirm_enabled()

    # Initial click sends the request to barrier
    modal.click_confirm()
    barrier.wait_until_reached(timeout=5.0)

    # In-flight assertions
    assert modal.get_confirm_text() == "Resetting..."
    assert modal.is_confirm_disabled() is True

    # Concurrent clicks while held
    modal.click_confirm()
    modal.click_confirm()

    # Release backend request
    barrier.release()
    modal.wait_until_closed()

    # Ensure backend processed only 1 request
    reqs = callback_server.get_requests("/cloud/apps/1443430/reset", method="POST")
    assert len(reqs) == 1, f"Expected 1 POST, got {len(reqs)}"


@pytest.mark.ui_p0
def test_uj01_rollback_on_http_500_fault(
    setup_library_shell: CloudSettingsPage,
    callback_server: CallbackServerStub,
) -> None:
    """Rollback on HTTP 500 fault: modal stays open, button reverts to 'Confirm Reset' and re-enables."""
    page = setup_library_shell
    modal = page.reset_modal
    modal.open(1443430)

    callback_server.clear_requests()
    callback_server.set_fault(
        "/cloud/apps/1443430/reset",
        status_code=500,
        body="Internal Server Error: 500",
    )

    modal.clear_captured_alerts()
    modal.type_input("RESET")
    modal.wait_for_confirm_enabled()

    modal.click_confirm()

    # Wait for error alert
    modal.wait_for_alert_containing("Failed to reset cloud save")

    # Modal must remain open and revert button state
    assert modal.is_open() is True
    modal.wait_for_confirm_text("Confirm Reset")
    modal.wait_for_confirm_enabled()
    assert modal.is_confirm_disabled() is False

    modal.click_close()
    modal.wait_until_closed()


@pytest.mark.ui_p0
def test_uj01_cancellation_modes_send_zero_requests(
    setup_library_shell: CloudSettingsPage,
    callback_server: CallbackServerStub,
) -> None:
    """Partial inputs cancelled via Cancel, Close X, and gamepad vgp_oncancel must dispatch 0 requests."""
    page = setup_library_shell
    modal = page.reset_modal

    # Mode 1: Cancel button
    callback_server.clear_requests()
    modal.open(1443430)
    modal.type_input("RESE")
    modal.click_cancel()
    modal.wait_until_closed()
    assert len(callback_server.get_requests("/cloud/apps/1443430/reset")) == 0

    # Mode 2: Close X button
    callback_server.clear_requests()
    modal.open(1443430)
    modal.type_input("RESE")
    modal.click_close()
    modal.wait_until_closed()
    assert len(callback_server.get_requests("/cloud/apps/1443430/reset")) == 0

    # Mode 3: Gamepad cancel (vgp_oncancel)
    callback_server.clear_requests()
    modal.open(1443430)
    modal.type_input("RESE")
    modal.dispatch_cancel()
    modal.wait_until_closed()
    assert len(callback_server.get_requests("/cloud/apps/1443430/reset")) == 0
