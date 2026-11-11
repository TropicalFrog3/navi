"""
@file  test_uj10_navi_menu_drilldown.py
@brief User journey test for the Navi Menu drill-down sections

UJ-10: Navi Menu Section Drill-Down.

Scenarios:
  1. Main view lists only the section categories (Provider / Game / Cloud Saves).
  2. Opening a section replaces the list with that section's items and updates the title.
  3. Back navigation returns to the category list.

"""

from __future__ import annotations

from pathlib import Path
import pytest

from tests.integration.ui.harness.browser_driver import BrowserDriver
from tests.integration.ui.harness.callback_server_stub import CallbackServerStub
from tests.integration.ui.pages.library_details_page import LibraryDetailsPage


@pytest.fixture
def library_page(driver: BrowserDriver) -> LibraryDetailsPage:
    return LibraryDetailsPage(driver)


@pytest.mark.ui_p1
def test_uj10_navi_menu_drilldown_sections(
    library_page: LibraryDetailsPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Categories open their own item list; Back returns to the main menu."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    library_page.navigate_library(shell_path, appid=1443430)
    library_page.enable_desktop_ui(appid=1443430)
    library_page.inject_app_details_script(repo_paths["scripts"] / "app-details-script.js")
    library_page.wait_for_injected_buttons()

    navi = library_page.click_navi_button()
    navi.wait_until_open()

    # 1. Main view: categories only, no section items.
    assert navi.get_title() == "Navi Menu"
    assert navi.is_element_visible(navi.nav_provider_btn)
    assert navi.is_element_visible(navi.nav_game_btn)
    assert navi.is_element_visible(navi.nav_cloud_btn)
    assert not navi.is_element_visible(navi.config_btn)
    assert not navi.is_element_visible(navi.cloud_toggle_btn)
    assert not navi.is_element_visible(navi.back_btn)

    # 2. Game section replaces the list with game items.
    navi.open_section("game")
    driver.wait_for_condition(
        lambda: navi.get_title() == "Game",
        timeout=5.0,
        error_message="Game section title did not appear",
    )
    assert navi.is_element_visible(navi.config_btn)
    assert navi.is_element_visible(navi.remove_btn)
    assert navi.is_element_visible(navi.back_btn)
    assert not navi.is_element_visible(navi.nav_game_btn)
    assert not navi.is_element_visible(navi.cloud_toggle_btn)

    # 3. Back returns to the category list.
    navi.click_back()
    driver.wait_for_condition(
        lambda: navi.get_title() == "Navi Menu",
        timeout=5.0,
        error_message="Back button did not return to the main menu",
    )
    assert navi.is_element_visible(navi.nav_game_btn)
    assert not navi.is_element_visible(navi.config_btn)

    # 4. Cloud section shows cloud items only.
    navi.open_section("cloud")
    driver.wait_for_condition(
        lambda: navi.get_title() == "Cloud Saves",
        timeout=5.0,
        error_message="Cloud Saves section title did not appear",
    )
    assert navi.is_element_visible(navi.cloud_toggle_btn)
    assert navi.is_element_visible(navi.cloud_backup_btn)
    assert navi.is_element_visible(navi.cloud_restore_btn)
    assert navi.is_element_visible(navi.cloud_reset_btn)
    assert not navi.is_element_visible(navi.config_btn)
    assert not navi.is_element_visible(navi.nav_cloud_btn)

    # 5. Gamepad B backs out of a section first, then closes from the main menu.
    navi.dispatch_cancel()
    driver.wait_for_condition(
        lambda: navi.get_title() == "Navi Menu",
        timeout=5.0,
        error_message="Gamepad cancel did not return to the main menu",
    )
    assert navi.is_in_dom()
    navi.dispatch_cancel()
    navi.wait_until_closed()
