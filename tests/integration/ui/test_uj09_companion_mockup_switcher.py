"""
@file  test_uj09_companion_mockup_switcher.py
@brief UI journey test verifying companion mockup switcher functionality


Integration test for Grouped Sections file viewer in View Steam Cloud drill-down
inside import-lua-script.js (Filter Tabs and Tree Inspector removed; categories collapsed by default).
"""

from __future__ import annotations

from pathlib import Path
import pytest

from tests.integration.ui.harness.browser_driver import BrowserDriver
from tests.integration.ui.harness.callback_server_stub import CallbackServerStub
from tests.integration.ui.pages.import_config_page import ConfigEditorPage


@pytest.fixture
def config_page(driver: BrowserDriver) -> ConfigEditorPage:
    return ConfigEditorPage(driver)


@pytest.mark.ui_p0
def test_cloud_drilldown_grouped_file_viewer_collapsed_by_default(
    config_page: ConfigEditorPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Validate Grouped Sections file viewer in View Steam Cloud drill-down with categories collapsed by default."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)
    driver.inject_script(repo_paths["scripts"] / "import-lua-script.js")

    # 1. Open Navi Config
    config_page.open()
    assert config_page.is_open() is True

    # Verify 'Add Game' button and add-game suggestions dropdown are completely removed
    assert driver.evaluate("document.getElementById('sls-cfg-add-cloud-app-btn') === null") is True
    assert driver.evaluate("document.getElementById('sls-cfg-cs-search-suggestions') === null") is True
    driver.wait_for_condition(
        lambda: driver.evaluate("document.getElementById('sls-cfg-add-cloud-app-input') !== null") is True,
        timeout=5.0,
        error_message="sls-cfg-add-cloud-app-input not rendered",
    )

    # 2. Wait for games table in View Steam Cloud and drill down into first game
    driver.wait_for_condition(
        lambda: driver.evaluate("document.querySelector('.navi-steam-cloud-link') !== null") is True,
        timeout=5.0,
        error_message="Games table in View Steam Cloud not rendered",
    )

    # Click 'Show Files' on the first game
    driver.evaluate("document.querySelector('.navi-steam-cloud-link').click()")

    # 3. Verify drill-down header and Grouped Sections container render directly
    driver.wait_for_condition(
        lambda: driver.evaluate(
            "document.getElementById('sls-cloud-drilldown-header') !== null && "
            "document.getElementById('sls-cloud-grouped-container') !== null && "
            "document.querySelectorAll('.sls-cloud-group-card').length >= 1"
        ) is True,
        timeout=5.0,
        error_message="Cloud drill-down grouped sections container not found",
    )

    # 4. Verify Filter Tabs and Tree Inspector listings are removed
    assert driver.evaluate("document.getElementById('sls-cloud-view-tabs') === null") is True
    assert driver.evaluate("document.getElementById('sls-cloud-view-tree') === null") is True
    assert driver.evaluate("document.getElementById('sls-cloud-tabs-bar') === null") is True
    assert driver.evaluate("document.getElementById('sls-cloud-tree-inspector-container') === null") is True

    # 5. Verify all file category sections are collapsed by default
    group_headers_count = driver.evaluate("document.querySelectorAll('.sls-cloud-group-header').length")
    assert group_headers_count >= 1

    all_collapsed_initially = driver.evaluate("""
        Array.from(document.querySelectorAll('.sls-cloud-group-card')).every(function (card) {
            var header = card.querySelector('.sls-cloud-group-header');
            var body = card.querySelector('.sls-cloud-group-body');
            var chevron = card.querySelector('.sls-cloud-group-chevron');
            return header && header.getAttribute('aria-expanded') === 'false' &&
                   body && body.style.display === 'none' &&
                   chevron && chevron.textContent === '►';
        })
    """)
    assert all_collapsed_initially is True

    # 6. Click first header to expand
    driver.evaluate("document.querySelector('.sls-cloud-group-header').click()")
    first_expanded = driver.evaluate(
        "document.querySelector('.sls-cloud-group-body').style.display !== 'none' && "
        "document.querySelector('.sls-cloud-group-header').getAttribute('aria-expanded') === 'true'"
    )
    assert first_expanded is True

    # 7. Click first header again to collapse
    driver.evaluate("document.querySelector('.sls-cloud-group-header').click()")
    first_collapsed_again = driver.evaluate(
        "document.querySelector('.sls-cloud-group-body').style.display === 'none' && "
        "document.querySelector('.sls-cloud-group-header').getAttribute('aria-expanded') === 'false'"
    )
    assert first_collapsed_again is True

    # 7b. Test Back button returns to the games directory overview
    back_btn_visible = driver.evaluate(
        "document.getElementById('sls-cfg-cs-back-btn') !== null && "
        "document.getElementById('sls-cfg-cs-back-btn').style.display !== 'none'"
    )
    assert back_btn_visible is True
    driver.evaluate("document.getElementById('sls-cfg-cs-back-btn').click()")
    driver.wait_for_condition(
        lambda: driver.evaluate(
            "document.getElementById('sls-cloud-drilldown-header') === null && "
            "document.querySelector('.navi-steam-cloud-link') !== null && "
            "document.getElementById('sls-cfg-cs-back-btn').style.display === 'none'"
        ) is True,
        timeout=5.0,
        error_message="Clicking back button failed to return to View Steam Cloud games list",
    )

    # 8. Test that prototype banner in Manual Installation page is completely removed
    driver.evaluate("window.__naviShowImportLuaPage()")
    driver.wait_for_condition(
        lambda: driver.evaluate("document.getElementById('sls-manual-dropzone') !== null") is True,
        timeout=3.0,
    )
    assert driver.evaluate("document.getElementById('sls-companion-prototype-banner') === null") is True

