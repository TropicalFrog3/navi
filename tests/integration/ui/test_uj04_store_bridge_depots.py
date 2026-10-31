"""
UJ-04: Steam Store Page Bridge and Depot Selection Integration Tests.

Scenarios:
  1. Store page .sls-lua-btn injection and /check?id=1443430 via window.__naviQueue under strict CSP.
  2. Depot selection modal: selecting/deselecting depot checkboxes updates UI state in real time; submitting triggers /start-download.
  3. Gamepad vgp_ondirection inside modal blurs focused <input> (OSK dismissal) and moves outline while skipping disabled buttons.
  4. Error handling when /start-download returns failure (e.g. no decryption keys available).
"""

from __future__ import annotations

from pathlib import Path
import pytest

from tests.integration.ui.harness.browser_driver import BrowserDriver
from tests.integration.ui.harness.callback_server_stub import CallbackServerStub
from tests.integration.ui.harness.navi_queue_bridge import NaviQueueBridge
from tests.integration.ui.pages.store_app_page import StoreAppPage


@pytest.fixture
def store_page(driver: BrowserDriver) -> StoreAppPage:
    return StoreAppPage(driver)


@pytest.mark.ui_p0
def test_uj04_store_page_button_injection_and_queue_bridge(
    store_page: StoreAppPage,
    callback_server: CallbackServerStub,
    queue_bridge: NaviQueueBridge,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Store page CSP enforcement, .sls-lua-btn injection, and window.__naviQueue round-trip."""
    store_shell_path = repo_paths["fixtures"] / "steam_store_app.html"
    store_page.navigate_store_app(store_shell_path, appid=1443430)

    # Step 1: Verify CSP blocks direct loopback fetch
    direct_fetch = driver.evaluate(
        "(function() {"
        "    return fetch('http://127.0.0.1:9001/check?id=1443430')"
        "        .then(function() { return { blocked: false }; })"
        "        .catch(function(e) { return { blocked: true, err: String(e) }; });"
        "})()",
        await_promise=True,
    )
    assert direct_fetch.get("blocked") is True

    # Step 2: Start background queue bridge
    queue_bridge.start(interval=0.03)

    # Step 3: Inject store-page-script.js
    store_page.inject_scripts(repo_paths["scripts"] / "store-page-script.js")

    # Step 4: Button is injected and resolved via bridge
    store_page.wait_for_lua_button(1443430)
    driver.wait_for_condition(
        lambda: "installed" in store_page.get_lua_button_text(1443430).lower(),
        timeout=5.0,
        error_message="Store button text did not resolve to installed status",
    )

    # Step 5: Verify request was recorded by the server stub
    req = callback_server.last_request("/check")
    assert req is not None
    assert req["query"].get("id") == ["1443430"]


@pytest.mark.ui_p0
@pytest.mark.a11y
def test_uj04_depot_selection_checkboxes_and_start_download(
    store_page: StoreAppPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Depot selection modal: checkbox state toggles Install button and submit triggers /start-download."""
    driver.navigate_to_file(repo_paths["fixtures"] / "steam_library_sp.html")
    depot_modal = store_page.inject_depot_script(
        repo_paths["scripts"] / "depot-selection-script.js",
        appid=1443430,
    )

    # Assert ARIA semantics
    assert depot_modal.get_role() == "dialog"
    assert depot_modal.get_aria_modal() == "true"
    assert depot_modal.get_data_testid() == "sls-depot-overlay-modal"

    # Wait for depots to render
    driver.wait_for_condition(lambda: depot_modal.get_checkbox_count() > 0)
    assert depot_modal.get_checkbox_count() >= 1
    assert depot_modal.is_checkbox_checked(0) is True
    assert depot_modal.is_download_enabled() is True

    # Step 1: Uncheck depot -> button becomes disabled
    depot_modal.toggle_checkbox(0)
    assert depot_modal.is_checkbox_checked(0) is False
    assert depot_modal.is_download_enabled() is False

    # Step 2: Re-check depot -> button re-enables
    depot_modal.toggle_checkbox(0)
    assert depot_modal.is_checkbox_checked(0) is True
    assert depot_modal.is_download_enabled() is True

    # Step 3: Click download -> dispatches /start-download
    callback_server.clear_requests()
    depot_modal.click_download()

    driver.wait_for_condition(
        lambda: len(callback_server.get_requests("/start-download")) > 0,
        timeout=5.0,
        error_message="GET /start-download was not dispatched",
    )

    req = callback_server.last_request("/start-download")
    assert req is not None
    assert "1443430" in req["path"]
    assert "1443431" in req["path"]

    depot_modal.wait_until_closed()


@pytest.mark.ui_p0
def test_uj04_gamepad_navigation_osk_dismissal_and_disabled_skip(
    store_page: StoreAppPage,
    callback_server: CallbackServerStub,
    queue_bridge: NaviQueueBridge,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Gamepad vgp_ondirection blurs active input (OSK dismissal) and skips disabled buttons."""
    # Configure /check to return exists: false so button displays 'Download Lua'
    callback_server.set_response(
        "/check",
        {"exists": False, "appid": 1443430, "provider": "HubcapDB", "installed": False},
        status_code=200,
    )
    queue_bridge.start(interval=0.03)

    store_shell_path = repo_paths["fixtures"] / "steam_store_app.html"
    store_page.navigate_store_app(store_shell_path, appid=1443430)
    store_page.inject_scripts(repo_paths["scripts"] / "store-page-script.js")

    store_page.wait_for_lua_button(1443430)
    driver.wait_for_condition(
        lambda: "download" in store_page.get_lua_button_text(1443430).lower(),
        timeout=5.0,
        error_message="Button did not transition to Download Lua",
    )

    # Click Lua button to open provider modal
    store_page.click_lua_button(1443430)
    provider_modal = store_page.provider_modal
    provider_modal.wait_until_open()

    # Step 1: Focus an input element
    provider_modal.focus_api_key(0)
    active_el_before = driver.evaluate("document.activeElement.tagName")
    assert str(active_el_before).upper() == "INPUT"

    # Step 2: Dispatch gamepad directional movement
    provider_modal.dispatch_direction(provider_modal.GAMEPAD_DOWN)

    # Step 3: Assert input was blurred cleanly (OSK dismissal)
    driver.wait_for_condition(
        lambda: driver.evaluate("document.activeElement.tagName") != "INPUT",
        timeout=5.0,
        error_message="Active element was not blurred upon gamepad direction event",
    )

    # Step 4: Assert outline is on a focusable button and disabled buttons were skipped
    outlined = provider_modal.get_gamepad_outlined_element()
    assert outlined is not None
    assert outlined.get("disabled") is False

    # Step 5: Gamepad cancel closes modal
    provider_modal.dispatch_cancel()
    provider_modal.wait_until_closed()


@pytest.mark.ui_p0
def test_uj04_start_download_decryption_key_failure_handling(
    store_page: StoreAppPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Error handling when /start-download returns failure (e.g. no decryption keys available)."""
    callback_server.set_response(
        "/start-download",
        {
            "success": False,
            "message": "Could not start install (no decryption keys available)",
        },
        status_code=200,
    )

    driver.navigate_to_file(repo_paths["fixtures"] / "steam_library_sp.html")
    depot_modal = store_page.inject_depot_script(
        repo_paths["scripts"] / "depot-selection-script.js",
        appid=1443430,
    )

    driver.wait_for_condition(lambda: depot_modal.get_checkbox_count() > 0)
    depot_modal.click_download()

    # Button transitions to Failed
    depot_modal.wait_for_download_text("Failed")

    # Modal must remain open
    assert depot_modal.is_open() is True

    # User can cancel out safely once pointer-events are restored
    depot_modal.wait_for_cancel_enabled(timeout=5.0)
    depot_modal.click_cancel()
    depot_modal.wait_until_closed()
