"""
Integration test for 3 File Viewer Variations (Filter Tabs, Grouped Sections, Tree Inspector)
in View Steam Cloud drill-down file viewer inside import-lua-script.js.
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
def test_cloud_drilldown_three_file_viewer_variations(
    config_page: ConfigEditorPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Validate 3 interactive file viewer variations and switcher in View Steam Cloud drill-down."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)
    driver.inject_script(repo_paths["scripts"] / "import-lua-script.js")

    # 1. Open Navi Config
    config_page.open()
    assert config_page.is_open() is True

    # 2. Wait for games table in View Steam Cloud and drill down into App 107410
    driver.wait_for_condition(
        lambda: driver.evaluate("document.querySelector('.navi-steam-cloud-link') !== null") is True,
        timeout=5.0,
        error_message="Games table in View Steam Cloud not rendered",
    )

    # Click 'Show Files' on the first game
    driver.evaluate("document.querySelector('.navi-steam-cloud-link').click()")

    # 3. Verify drill-down header and 3 view switcher pills exist
    driver.wait_for_condition(
        lambda: driver.evaluate(
            "document.getElementById('sls-cloud-drilldown-header') !== null && "
            "document.querySelectorAll('.sls-cloud-view-pill').length === 3"
        ) is True,
        timeout=5.0,
        error_message="Cloud drill-down header and 3 view pills not found",
    )

    # 4. Test Variation 1 (Filter Tabs)
    driver.evaluate("window.__slsCloudFileViewer.switchVariation('tabs')")
    driver.wait_for_condition(
        lambda: driver.evaluate("document.getElementById('sls-cloud-tabs-bar') !== null") is True,
        timeout=3.0,
    )

    tabs_count = driver.evaluate("document.querySelectorAll('.sls-cloud-filter-tab').length")
    assert tabs_count >= 2

    # Verify initial file rows count in tabs mode
    initial_rows_count = driver.evaluate("document.querySelectorAll('.navi-cloud-file-row').length")
    assert initial_rows_count >= 1

    # Click a specific tab (e.g. second tab)
    driver.evaluate("""
        var tabs = document.querySelectorAll('.sls-cloud-filter-tab');
        if (tabs.length > 1) { tabs[1].click(); }
    """)
    active_tab = driver.evaluate("window.__slsCloudFileViewer.getState().activeTab")
    assert active_tab != "all"

    # 5. Test Variation 2 (Grouped Sections)
    driver.evaluate("window.__slsCloudFileViewer.switchVariation('grouped')")
    driver.wait_for_condition(
        lambda: driver.evaluate("document.querySelectorAll('.sls-cloud-group-card').length >= 1") is True,
        timeout=3.0,
    )

    # Verify section header exists
    group_headers_count = driver.evaluate("document.querySelectorAll('.sls-cloud-group-header').length")
    assert group_headers_count >= 1

    # Test collapse / expand toggle on the first group
    body_visible_before = driver.evaluate(
        "document.querySelector('.sls-cloud-group-body').style.display !== 'none'"
    )
    assert body_visible_before is True

    # Click header to collapse
    driver.evaluate("document.querySelector('.sls-cloud-group-header').click()")
    body_visible_after = driver.evaluate(
        "document.querySelector('.sls-cloud-group-body').style.display !== 'none'"
    )
    assert body_visible_after is False

    # Click header again to re-expand
    driver.evaluate("document.querySelector('.sls-cloud-group-header').click()")
    body_visible_reopened = driver.evaluate(
        "document.querySelector('.sls-cloud-group-body').style.display !== 'none'"
    )
    assert body_visible_reopened is True

    # 6. Test Variation 3 (Tree Inspector)
    driver.evaluate("window.__slsCloudFileViewer.switchVariation('tree')")
    driver.wait_for_condition(
        lambda: driver.evaluate(
            "document.getElementById('sls-cloud-tree-pane') !== null && "
            "document.getElementById('sls-cloud-tree-inspector-panel') !== null"
        ) is True,
        timeout=3.0,
    )

    # Verify tree file items exist
    tree_items_count = driver.evaluate("document.querySelectorAll('.sls-cloud-tree-item').length")
    assert tree_items_count >= 1

    # Check inspector path display
    inspect_path_initial = driver.evaluate("document.getElementById('sls-tree-inspect-path').textContent")
    assert inspect_path_initial != ""

    # Verify download button in inspector panel exists
    assert driver.evaluate("document.getElementById('sls-tree-inspect-dl-btn') !== null") is True

    # Click an unselected tree item if available and verify inspector update
    if tree_items_count > 1:
        driver.evaluate("""
            var items = document.querySelectorAll('.sls-cloud-tree-item');
            for (var i = 0; i < items.length; i++) {
                if (!items[i].classList.contains('active')) {
                    items[i].click();
                    break;
                }
            }
        """)
        inspect_path_updated = driver.evaluate("document.getElementById('sls-tree-inspect-path').textContent")
        assert inspect_path_updated != inspect_path_initial

    # 7. Test that prototype banner in Manual Installation page is completely removed
    driver.evaluate("window.__naviShowImportLuaPage()")
    driver.wait_for_condition(
        lambda: driver.evaluate("document.getElementById('sls-manual-dropzone') !== null") is True,
        timeout=3.0,
    )
    assert driver.evaluate("document.getElementById('sls-companion-prototype-banner') === null") is True
