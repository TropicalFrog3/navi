"""
@file  test_phase1_harness_smoke.py
@brief Smoke test verifying browser harness initialization and teardown


Phase 1 Smoke Tests for Real-Time User-Input UI Integration Test Harness.

Tests:
  1. Library shell script injection + ARIA attributes (role="dialog", aria-modal="true", aria-label="Close").
  2. Real-time character-by-character typing in #sls-reset-input ("reset" -> disabled, "  RESET " -> enabled,
     "  RESET !" -> disabled, Backspace -> enabled), holding POST /cloud/apps/1443430/reset via barrier to
     assert intermediate "Resetting..." disabled button state, and verifying double-click only sends 1 request.
  3. Store shell CSP enforcement + window.__naviQueue bridge round-trip for /check?id=1443430.
"""

from __future__ import annotations

import json
from pathlib import Path
import pytest

from tests.integration.ui.harness.browser_driver import BrowserDriver
from tests.integration.ui.harness.callback_server_stub import CallbackServerStub
from tests.integration.ui.harness.navi_queue_bridge import NaviQueueBridge


@pytest.mark.ui_p0
@pytest.mark.a11y
def test_library_shell_injection_and_aria_attributes(
    driver: BrowserDriver,
    callback_server: CallbackServerStub,
    repo_paths: dict[str, Path],
) -> None:
    """Verify library shell fixture loads, scripts inject cleanly, and ARIA attributes are present."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)

    # Verify native Steam shell DOM anchors
    assert driver.evaluate("document.querySelector('._2D64jIEK7wpUR_NlObDW76') !== null")
    assert driver.evaluate("document.querySelector('._1Ky59qmywxOUtNcI1cgmkX') !== null")
    assert driver.evaluate("document.querySelector('div[aria-label=\"Manage\"]') !== null")

    # Inject cloud_manager.js
    driver.inject_script(repo_paths["scripts"] / "cloud_manager.js")
    assert driver.evaluate("typeof window.NaviCloudManager !== 'undefined'")

    # Open Settings Modal
    driver.evaluate("window.NaviCloudManager.openSettingsModal();")
    driver.wait_for_selector("#sls-cloud-settings-modal", timeout=5.0)

    # 1. Assert Modal Overlay ARIA attributes
    assert driver.evaluate("document.getElementById('sls-cloud-settings-modal').getAttribute('role')") == "dialog"
    assert driver.evaluate("document.getElementById('sls-cloud-settings-modal').getAttribute('aria-modal')") == "true"
    assert driver.evaluate("document.getElementById('sls-cloud-settings-modal').getAttribute('data-testid')") == "sls-cloud-settings-modal"

    # 2. Assert Close Button ARIA attributes
    close_btn = driver.evaluate("document.getElementById('sls-settings-close') !== null")
    assert close_btn is True
    assert driver.evaluate("document.getElementById('sls-settings-close').getAttribute('aria-label')") == "Close"

    # 3. Assert Status Container ARIA live region
    badge_el = driver.evaluate("document.getElementById('sls-auth-badge') !== null")
    assert badge_el is True
    assert driver.evaluate("document.getElementById('sls-auth-badge').getAttribute('role')") == "status"
    assert driver.evaluate("document.getElementById('sls-auth-badge').getAttribute('aria-live')") == "polite"

    # Close modal and verify it is removed from DOM
    driver.click("#sls-settings-close")
    driver.wait_for_condition(
        lambda: driver.evaluate("document.getElementById('sls-cloud-settings-modal') === null"),
        timeout=5.0,
        error_message="Settings modal was not removed after close click",
    )


@pytest.mark.ui_p0
def test_library_reset_input_realtime_typing_and_request_barrier(
    driver: BrowserDriver,
    callback_server: CallbackServerStub,
    repo_paths: dict[str, Path],
) -> None:
    """
    Test real-time character-by-character typing verification in #sls-reset-input,
    intermediate disabled/text assertion via deterministic barrier, and double-click suppression.
    """
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)

    # Inject cloud_manager.js
    driver.inject_script(repo_paths["scripts"] / "cloud_manager.js")

    # Open Reset Modal for AppID 1443430
    driver.evaluate("window.NaviCloudManager.resetAppSave(1443430);")
    driver.wait_for_selector("#sls-reset-modal", timeout=5.0)

    # Assert ARIA attributes on reset modal
    assert driver.evaluate("document.getElementById('sls-reset-modal').getAttribute('role')") == "dialog"
    assert driver.evaluate("document.getElementById('sls-reset-modal').getAttribute('aria-modal')") == "true"
    assert driver.evaluate("document.getElementById('sls-reset-modal').getAttribute('data-testid')") == "sls-reset-modal"
    assert driver.evaluate("document.getElementById('sls-reset-close').getAttribute('aria-label')") == "Close"

    # Confirm button initially disabled
    assert driver.evaluate("document.getElementById('sls-modal-confirm').disabled") is True

    # Step 1: Type "reset" (lowercase) -> Should remain disabled
    driver.type_text("#sls-reset-input", "reset")
    driver.wait_for_condition(
        lambda: driver.evaluate("document.getElementById('sls-reset-input').value") == "reset",
        timeout=5.0,
    )
    assert driver.evaluate("document.getElementById('sls-modal-confirm').disabled") is True

    # Step 2: Clear and type "  RESET " (uppercase with surrounding whitespace) -> Should become enabled
    driver.clear_and_type("#sls-reset-input", "  RESET ")
    driver.wait_for_condition(
        lambda: not driver.evaluate("document.getElementById('sls-modal-confirm').disabled"),
        timeout=5.0,
        error_message="Confirm button did not enable for '  RESET '",
    )
    assert driver.evaluate("document.getElementById('sls-modal-confirm').disabled") is False

    # Step 3: Type "!" ("  RESET !") -> Should become disabled
    driver.type_text("#sls-reset-input", "!")
    driver.wait_for_condition(
        lambda: driver.evaluate("document.getElementById('sls-modal-confirm').disabled"),
        timeout=5.0,
        error_message="Confirm button did not disable for '  RESET !'",
    )
    assert driver.evaluate("document.getElementById('sls-modal-confirm').disabled") is True

    # Step 4: Press Backspace -> Should revert to "  RESET " and become enabled
    driver.press_key("Backspace")
    driver.wait_for_condition(
        lambda: not driver.evaluate("document.getElementById('sls-modal-confirm').disabled"),
        timeout=5.0,
        error_message="Confirm button did not re-enable after Backspace",
    )
    assert driver.evaluate("document.getElementById('sls-modal-confirm').disabled") is False

    # Step 5: Deterministic Request Barrier on POST /cloud/apps/1443430/reset
    barrier = callback_server.hold_endpoint("/cloud/apps/1443430/reset")

    # Click confirm button
    driver.click("#sls-modal-confirm")

    # Wait until the HTTP request has reached callback_server
    barrier.wait_until_reached(timeout=5.0)

    # Intermediate state assertions: button must be disabled and display "Resetting..."
    btn_text = driver.evaluate("document.getElementById('sls-modal-confirm').innerText")
    is_disabled = driver.evaluate("document.getElementById('sls-modal-confirm').disabled")
    assert btn_text == "Resetting..."
    assert is_disabled is True

    # Step 6: Double-click simulation while request is held
    driver.click("#sls-modal-confirm")
    driver.click("#sls-modal-confirm")

    # Step 7: Release the barrier to let the backend request complete
    barrier.release()

    # Wait for modal to close upon success
    driver.wait_for_condition(
        lambda: driver.evaluate("document.getElementById('sls-reset-modal') === null"),
        timeout=5.0,
        error_message="Reset modal did not close after backend response was released",
    )

    # Step 8: Assert that callback_server received exactly 1 request (double clicks ignored)
    requests = callback_server.get_requests("/cloud/apps/1443430/reset", method="POST")
    assert len(requests) == 1, f"Expected exactly 1 reset request, but found {len(requests)}"
    assert requests[0]["method"] == "POST"


@pytest.mark.ui_p0
def test_store_shell_csp_enforcement_and_navi_queue_bridge(
    driver: BrowserDriver,
    callback_server: CallbackServerStub,
    queue_bridge: NaviQueueBridge,
    repo_paths: dict[str, Path],
) -> None:
    """
    Test that store shell CSP blocks direct http://127.0.0.1:9001 fetch,
    and that window.__naviQueue bridge round-trip functions correctly.
    """
    store_shell_path = repo_paths["fixtures"] / "steam_store_app.html"
    driver.navigate_to_file(store_shell_path, query="/app/1443430")

    # Step 1: Verify CSP blocks direct fetch
    direct_fetch_result = driver.evaluate(
        "(function() {"
        "    return fetch('http://127.0.0.1:9001/check?id=1443430')"
        "        .then(function() { return { blocked: false }; })"
        "        .catch(function(e) { return { blocked: true, err: String(e) }; });"
        "})()",
        await_promise=True,
    )
    assert direct_fetch_result["blocked"] is True, "Direct fetch should be blocked by CSP connect-src https:!"

    # Step 2: Inject store-page-script.js
    driver.inject_script(repo_paths["scripts"] / "store-page-script.js")

    # Step 3: Start background NaviQueueBridge
    queue_bridge.start(interval=0.03)

    # Step 4: Execute naviFetch('/check?id=1443430') through the bridge
    resolved_data = driver.evaluate(
        "(function() {"
        "    return window.naviFetch('http://127.0.0.1:9001/check?id=1443430')"
        "        .then(function(res) { return res.json(); });"
        "})()",
        await_promise=True,
    )

    assert resolved_data is not None
    assert resolved_data.get("exists") is True
    assert resolved_data.get("appid") == 1443430
    assert resolved_data.get("provider") == "HubcapDB"

    # Step 5: Verify request was recorded by the server stub
    req = callback_server.last_request("/check")
    assert req is not None
    assert req["query"].get("id") == ["1443430"]
