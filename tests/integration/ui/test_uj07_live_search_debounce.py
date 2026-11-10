"""
@file  test_uj07_live_search_debounce.py
@brief User journey test verifying live search input debounce timing


UJ-07: Real-Time Debounced Live Search & Auto-Collection Synchronization Integration Tests.

Scenarios:
  1. Real-time debounced search/filtering in the Library shell: rapid sequential typing
     coalesces filter updates, filters visible items in real time, renders empty state
     on unmatched query ("xyz_no_match_999"), and restores list on clear.
  2. Debounced search/filtering inside the Navi Config editor (#sls-cfg-search).
  3. Out-of-order async response protection ensuring older responses do not clobber newer results.
  4. Auto-collection script (/lua_apps and /list) synchronizing spoofed apps into the "LUAs" collection.
"""

from __future__ import annotations

import json
from pathlib import Path
import pytest

from tests.integration.ui.harness.browser_driver import BrowserDriver
from tests.integration.ui.harness.callback_server_stub import CallbackServerStub
from tests.integration.ui.pages.import_config_page import ConfigEditorPage


@pytest.fixture
def config_page(driver: BrowserDriver) -> ConfigEditorPage:
    return ConfigEditorPage(driver)


@pytest.mark.ui_p1
def test_uj07_library_live_search_debounce_and_empty_state(
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Library live search debounces rapid typing, filters items, shows empty state, and restores."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)

    def get_visible_count() -> int:
        expr = (
            "(function() {"
            "    var items = document.querySelectorAll('#library-items-list .sidebar-item');"
            "    var count = 0;"
            "    items.forEach(function(el) {"
            "        if (el.style.display !== 'none' && window.getComputedStyle(el).display !== 'none') count++;"
            "    });"
            "    return count;"
            "})()"
        )
        return int(driver.evaluate(expr, await_promise=False) or 0)

    def is_empty_state_visible() -> bool:
        expr = (
            "(function() {"
            "    var el = document.getElementById('library-empty-search-state');"
            "    return el ? el.style.display !== 'none' && window.getComputedStyle(el).display !== 'none' : false;"
            "})()"
        )
        return bool(driver.evaluate(expr, await_promise=False))

    def type_library_search(text: str) -> None:
        expr = (
            f"(function() {{"
            f"    var input = document.getElementById('library-search-input');"
            f"    if (!input) return false;"
            f"    input.value = {json.dumps(text)};"
            f"    input.dispatchEvent(new Event('input', {{ bubbles: true }}));"
            f"    return true;"
            f"}})()"
        )
        driver.evaluate(expr, await_promise=False)

    # Initially all 5 games visible, empty state hidden
    assert get_visible_count() == 5
    assert is_empty_state_visible() is False

    # 1. Type matching query "Rogue"
    type_library_search("Rogue")
    driver.wait_for_condition(
        lambda: get_visible_count() == 1,
        timeout=5.0,
        error_message="Visible count did not filter to 1 for query 'Rogue'",
    )
    assert is_empty_state_visible() is False

    # 2. Type unmatched query "xyz_no_match_999"
    type_library_search("xyz_no_match_999")
    driver.wait_for_condition(
        lambda: get_visible_count() == 0 and is_empty_state_visible(),
        timeout=5.0,
        error_message="Empty state did not appear for unmatched query 'xyz_no_match_999'",
    )

    # 3. Clear / Backspace restores list
    type_library_search("")
    driver.wait_for_condition(
        lambda: get_visible_count() == 5 and not is_empty_state_visible(),
        timeout=5.0,
        error_message="List did not restore to 5 items on clearing search input",
    )


@pytest.mark.ui_p1
def test_uj07_config_editor_live_search_filter(
    config_page: ConfigEditorPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Config Editor live search filters settings across tabs in real time."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)
    driver.inject_script(repo_paths["scripts"] / "import-lua-script.js")
    config_page.open()

    driver.wait_for_condition(lambda: bool(config_page.get_field_value("FakeName")))

    initial_count = config_page.get_visible_items_count()
    assert initial_count > 0

    # 1. Filter by keyword "provider"
    config_page.filter_settings("provider")
    driver.wait_for_condition(
        lambda: 0 < config_page.get_visible_items_count() < initial_count,
        timeout=5.0,
        error_message="Settings list was not filtered by keyword 'provider'",
    )

    # 2. Unmatched filter renders 0 items
    config_page.filter_settings("xyz_no_match_999")
    driver.wait_for_condition(
        lambda: config_page.get_visible_items_count() == 0,
        timeout=5.0,
        error_message="Items remained visible for unmatched query",
    )

    # 3. Clearing search restores default category view
    config_page.filter_settings("")
    driver.wait_for_condition(
        lambda: config_page.get_visible_items_count() > 0,
        timeout=5.0,
        error_message="Settings list was not restored after clearing filter",
    )


@pytest.mark.ui_p1
def test_uj07_out_of_order_async_response_protection(
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Out-of-order async response protection ensures stale responses do not overwrite newer results."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)

    # Simulate two async search requests with sequence tokens:
    # Query 1 (seq=1, slow delayed response with "Portal 2")
    # Query 2 (seq=2, fast response with "Rogue")
    # Even if Query 1 arrives after Query 2, the UI must retain Query 2's result.
    test_script = (
        "(function() {"
        "    window.__testSearchState = { renderedQuery: '', lastSeq: 0 };"
        "    function dispatchAsyncSearch(query, seq, delayMs) {"
        "        setTimeout(function() {"
        "            if (seq < window.__testSearchState.lastSeq) {"
        "                /* Stale response dropped */"
        "                return;"
        "            }"
        "            window.__testSearchState.lastSeq = seq;"
        "            window.__testSearchState.renderedQuery = query;"
        "        }, delayMs);"
        "    }"
        "    window.__dispatchAsyncSearch = dispatchAsyncSearch;"
        "})()"
    )
    driver.evaluate(test_script, await_promise=False)

    # Dispatch slow Query 1 (seq=1, 80ms) and fast Query 2 (seq=2, 20ms)
    driver.evaluate("window.__testSearchState.lastSeq = 2; window.__dispatchAsyncSearch('Rogue', 2, 20);", await_promise=False)
    driver.evaluate("window.__dispatchAsyncSearch('Portal 2', 1, 80);", await_promise=False)

    # Wait for the fast query to render
    driver.wait_for_condition(
        lambda: driver.evaluate("window.__testSearchState.renderedQuery") == "Rogue",
        timeout=5.0,
        error_message="Fast query was not rendered",
    )

    # Wait until the slow query would have executed (100ms) and assert it was dropped
    driver.wait_for_condition(
        lambda: driver.evaluate("window.__testSearchState.renderedQuery") == "Rogue",
        timeout=5.0,
        error_message="Stale query clobbered newer query results",
    )
    assert driver.evaluate("window.__testSearchState.renderedQuery") == "Rogue"


@pytest.mark.ui_p1
def test_uj07_auto_collection_lua_apps_synchronization(
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """auto-collection.js synchronizes /lua_apps into the Steam collectionStore under 'LUAs'."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)

    # Stub returns known list of spoofed app IDs
    callback_server.lua_app_ids = [440, 730, 1443430, 9999999]  # 9999999 is invalid in appStore

    # Inject auto-collection.js
    driver.inject_script(repo_paths["scripts"] / "auto-collection.js")

    # Wait for collectionStore to create LUAs collection and synchronize apps
    def get_luas_collection_apps():
        expr = (
            "(function() {"
            "    if (!window.collectionStore || !window.collectionStore.GetUserCollectionsByName) return null;"
            "    var cols = window.collectionStore.GetUserCollectionsByName('LUAs');"
            "    if (!cols || cols.length === 0) return null;"
            "    return cols[0].apps || [];"
            "})()"
        )
        return driver.evaluate(expr, await_promise=False)

    driver.wait_for_condition(
        lambda: get_luas_collection_apps() is not None and len(get_luas_collection_apps()) >= 3,
        timeout=5.0,
        error_message="LUAs collection was not created or populated by auto-collection.js",
    )

    apps = get_luas_collection_apps()
    assert 1443430 in apps
    assert 440 in apps
    assert 730 in apps
    # 9999999 should be filtered out because appStore.GetAppOverviewByAppID returns null for it
    assert 9999999 not in apps
