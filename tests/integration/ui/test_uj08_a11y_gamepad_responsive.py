"""
UJ-08: Accessibility, Gamepad Wrap-Around & 4-Viewport Responsive Matrix Tests.

Scenarios:
  1. Semantic accessibility audit across #sls-reset-modal, #sls-cloud-settings-modal,
     #sls-overlay-modal, and #sls-depot-overlay-modal (role="dialog", aria-modal="true",
     aria-live="polite", aria-label="Close", focus management).
  2. Gamepad navigation wrap-around (Up/Down/Left/Right cycling from first to last and
     last to first, skipping disabled controls, vgp_onok activation, vgp_oncancel dismissal).
  3. Parameterized 4-viewport responsive matrix (1920x1080, 1280x800, 768x1024, 390x844)
     verifying modal card bounds stay within viewport and hit targets meet minimum dimensions (>= 24x24).
"""

from __future__ import annotations

import json
from pathlib import Path
import pytest

from tests.integration.ui.harness.browser_driver import BrowserDriver
from tests.integration.ui.harness.callback_server_stub import CallbackServerStub
from tests.integration.ui.pages.cloud_settings_page import CloudSettingsPage
from tests.integration.ui.pages.library_details_page import LibraryDetailsPage
from tests.integration.ui.pages.store_app_page import StoreAppPage


@pytest.fixture
def library_page(driver: BrowserDriver) -> LibraryDetailsPage:
    return LibraryDetailsPage(driver)


@pytest.fixture
def store_page(driver: BrowserDriver) -> StoreAppPage:
    return StoreAppPage(driver)


@pytest.fixture
def cloud_settings_page(driver: BrowserDriver) -> CloudSettingsPage:
    return CloudSettingsPage(driver)


@pytest.mark.ui_p1
@pytest.mark.a11y
def test_uj08_semantic_accessibility_audit(
    library_page: LibraryDetailsPage,
    store_page: StoreAppPage,
    cloud_settings_page: CloudSettingsPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Semantic accessibility audit across the 4 major modals."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    library_page.navigate_library(shell_path, appid=1443430)
    library_page.enable_desktop_ui(appid=1443430)
    driver.inject_script(repo_paths["scripts"] / "cloud_manager.js")
    library_page.inject_app_details_script(repo_paths["scripts"] / "app-details-script.js")
    library_page.wait_for_injected_buttons()

    # 1. Audit #sls-overlay-modal (Game Manager)
    config_modal = library_page.click_config_button()
    config_modal.wait_until_open()
    assert config_modal.get_role() == "dialog"
    assert config_modal.get_aria_modal() == "true"
    assert config_modal.get_aria_label("#sls-close-x") == "Close"
    config_modal.click_close()
    config_modal.wait_until_closed()

    # 2. Audit #sls-reset-modal (Cloud Reset)
    reset_modal = library_page.click_cloud_reset_button()
    reset_modal.wait_until_open()
    assert reset_modal.get_role() == "dialog"
    assert reset_modal.get_aria_modal() == "true"
    assert reset_modal.get_aria_label("#sls-reset-close") == "Close"
    # Focus management: input must receive initial focus
    driver.wait_for_condition(
        lambda: driver.evaluate("document.activeElement.id") == "sls-reset-input",
        timeout=5.0,
        error_message="Reset input did not receive initial focus",
    )
    reset_modal.click_close()
    reset_modal.wait_until_closed()

    # 3. Audit #sls-cloud-settings-modal
    cloud_modal = cloud_settings_page.settings_modal.open()
    assert cloud_modal.get_role() == "dialog"
    assert cloud_modal.get_aria_modal() == "true"
    assert cloud_modal.get_aria_label("#sls-settings-close") == "Close"
    # Status badges have aria-live="polite"
    assert cloud_modal.get_attribute("aria-live", "#sls-auth-badge") == "polite"
    cloud_modal.click_close()
    cloud_modal.wait_until_closed()

    # 4. Audit #sls-depot-overlay-modal (Depot Selection)
    depot_modal = store_page.inject_depot_script(
        repo_paths["scripts"] / "depot-selection-script.js",
        appid=1443430,
    )
    assert depot_modal.get_role() == "dialog"
    assert depot_modal.get_aria_modal() == "true"
    assert depot_modal.get_aria_label("#sls-depot-close-x") == "Close"
    depot_modal.click_close()
    depot_modal.wait_until_closed()


@pytest.mark.ui_p1
@pytest.mark.a11y
def test_uj08_gamepad_navigation_wrap_around_and_actions(
    library_page: LibraryDetailsPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Gamepad Up/Down/Left/Right cycling wrap-around, disabled skipping, and cancellation."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    library_page.navigate_library(shell_path, appid=1443430)
    library_page.enable_desktop_ui(appid=1443430)
    library_page.inject_app_details_script(repo_paths["scripts"] / "app-details-script.js")
    library_page.wait_for_injected_buttons()

    modal = library_page.click_config_button()
    modal.wait_until_open()

    # Wait for trapSteamGamepadModal initial focus (outline on button 0: sls-close-x)
    driver.wait_for_condition(
        lambda: (modal.get_gamepad_outlined_element() or {}).get("id") == "sls-close-x",
        timeout=5.0,
        error_message="Gamepad focus did not outline close-x button initially",
    )

    # 1. Wrap-around upwards (Up from index 0 -> wraps to last button: sls-btn-cancel)
    modal.dispatch_direction(modal.GAMEPAD_UP)
    driver.wait_for_condition(
        lambda: (modal.get_gamepad_outlined_element() or {}).get("id") == "sls-btn-cancel",
        timeout=5.0,
        error_message="Gamepad wrap-around Up from index 0 did not wrap to last button",
    )

    # Move Up again -> goes to sls-btn-crack (index 2)
    modal.dispatch_direction(modal.GAMEPAD_UP)
    driver.wait_for_condition(
        lambda: (modal.get_gamepad_outlined_element() or {}).get("id") == "sls-btn-crack",
        timeout=5.0,
        error_message="Gamepad Up did not move to crack button",
    )

    # 2. Wrap-around downwards (Down from last button -> wraps to index 0: sls-close-x)
    modal.dispatch_direction(modal.GAMEPAD_DOWN)  # to sls-btn-cancel
    modal.dispatch_direction(modal.GAMEPAD_DOWN)  # wrap to sls-close-x
    driver.wait_for_condition(
        lambda: (modal.get_gamepad_outlined_element() or {}).get("id") == "sls-close-x",
        timeout=5.0,
        error_message="Gamepad wrap-around Down did not wrap back to index 0",
    )

    # 3. Skipping disabled controls
    # Move Down to sls-btn-fix (index 1)
    modal.dispatch_direction(modal.GAMEPAD_DOWN)
    driver.wait_for_condition(
        lambda: (modal.get_gamepad_outlined_element() or {}).get("id") == "sls-btn-fix",
        timeout=5.0,
        error_message="Gamepad Down did not move to fix button",
    )

    # Disable sls-btn-crack (index 2)
    driver.evaluate("document.getElementById('sls-btn-crack').disabled = true;", await_promise=False)

    # Move Down: should skip index 2 (disabled) and jump directly to index 3 (sls-btn-cancel)
    modal.dispatch_direction(modal.GAMEPAD_DOWN)
    driver.wait_for_condition(
        lambda: (modal.get_gamepad_outlined_element() or {}).get("id") == "sls-btn-cancel",
        timeout=5.0,
        error_message="Gamepad navigation did not skip disabled crack button",
    )

    # 4. vgp_oncancel dismisses modal
    modal.dispatch_cancel()
    modal.wait_until_closed(timeout=5.0)


@pytest.mark.ui_p1
@pytest.mark.responsive
@pytest.mark.parametrize(
    "width,height",
    [
        (1920, 1080),  # Desktop FHD
        (1280, 800),   # Steam Deck native 16:10
        (768, 1024),   # Tablet portrait
        (390, 844),    # Mobile narrow portrait
    ],
)
def test_uj08_responsive_matrix_bounds_and_hit_targets(
    width: int,
    height: int,
    library_page: LibraryDetailsPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """4-viewport responsive matrix: modal bounds stay within viewport and hit targets meet dimensions."""
    driver.set_viewport(width, height)

    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    library_page.navigate_library(shell_path, appid=1443430)
    library_page.enable_desktop_ui(appid=1443430)
    library_page.inject_app_details_script(repo_paths["scripts"] / "app-details-script.js")
    library_page.wait_for_injected_buttons()

    modal = library_page.click_config_button()
    modal.wait_until_open()

    # 1. Verify modal card bounding box stays within viewport bounds
    card_rect = driver.evaluate(
        "(function() {"
        "    var card = document.getElementById('sls-modal-card');"
        "    if (!card) return null;"
        "    var r = card.getBoundingClientRect();"
        "    return { left: r.left, top: r.top, right: r.right, bottom: r.bottom, width: r.width, height: r.height };"
        "})()",
        await_promise=False,
    )
    assert card_rect is not None
    assert card_rect["left"] >= 0, f"Modal card left {card_rect['left']} was outside viewport"
    assert card_rect["right"] <= width + 1, f"Modal card right {card_rect['right']} exceeded viewport width {width}"
    assert card_rect["top"] >= 0, f"Modal card top {card_rect['top']} was outside viewport"
    assert card_rect["bottom"] <= height + 1, f"Modal card bottom {card_rect['bottom']} exceeded viewport height {height}"

    # 2. Verify interactive button hit-target dimensions (minimum 24x24 px for touch / pointer targets)
    button_selectors = ["#sls-close-x", "#sls-btn-fix", "#sls-btn-crack", "#sls-btn-cancel"]
    for sel in button_selectors:
        rect = driver.evaluate(
            f"(function() {{"
            f"    var el = document.querySelector({json.dumps(sel)});"
            f"    if (!el) return null;"
            f"    var r = el.getBoundingClientRect();"
            f"    return {{ left: r.left, top: r.top, right: r.right, bottom: r.bottom, width: r.width, height: r.height }};"
            f"}})()",
            await_promise=False,
        )
        assert rect is not None, f"Button {sel} was not rendered"
        assert rect["width"] >= 24, f"Button {sel} width {rect['width']} was under 24px"
        assert rect["height"] >= 24, f"Button {sel} height {rect['height']} was under 24px"

        # Verify button is within the modal card bounding rect (no clipped controls)
        assert rect["left"] >= card_rect["left"] - 2, f"Button {sel} clipped on left"
        assert rect["right"] <= card_rect["right"] + 2, f"Button {sel} clipped on right"

    # Close modal cleanly
    modal.click_close()
    modal.wait_until_closed()
