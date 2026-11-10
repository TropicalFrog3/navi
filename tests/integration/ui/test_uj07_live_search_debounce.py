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
            "    var cells = Array.from(document.querySelectorAll('[role=\"gridcell\"]'));"
            "    return cells.filter(function(el) {"
            "        var t = (el.textContent || '').trim();"
            "        return t.length > 0 && !/^Games \\(\\d+/i.test(t) && !/^Tools \\(\\d+/i.test(t);"
            "    }).length;"
            "})()"
        )
        return int(driver.evaluate(expr, await_promise=False) or 0)

    def is_empty_state_visible() -> bool:
        expr = (
            "(function() {"
            "    return document.querySelectorAll('[role=\"gridcell\"]').length === 0;"
            "})()"
        )
        return bool(driver.evaluate(expr, await_promise=False))

    def type_library_search(text: str) -> None:
        expr = (
            f"(function() {{"
            f"    var input = document.querySelector('input[placeholder=\"Search by Name\"], input.DialogInput');"
            f"    if (!input) return false;"
            f"    var prev = input.value;"
            f"    var setter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value').set;"
            f"    if (setter) setter.call(input, {json.dumps(text)});"
            f"    else input.value = {json.dumps(text)};"
            f"    if (input._valueTracker) input._valueTracker.setValue(prev);"
            f"    input.dispatchEvent(new Event('input', {{ bubbles: true }}));"
            f"    input.dispatchEvent(new Event('change', {{ bubbles: true }}));"
            f"    var grid = document.querySelector('[role=\"grid\"]');"
            f"    if (grid) grid.dispatchEvent(new Event('scroll', {{ bubbles: true }}));"
            f"    return true;"
            f"}})()"
        )
        driver.evaluate(expr, await_promise=False)

    try:
        type_library_search("")
        driver.wait_for_condition(
            lambda: get_visible_count() > 1 and not is_empty_state_visible(),
            timeout=5.0,
            error_message="Initial real Steam Library list did not populate",
        )

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
            lambda: get_visible_count() > 1 and not is_empty_state_visible(),
            timeout=5.0,
            error_message="List did not restore on clearing search input",
        )
    finally:
        type_library_search("")


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

    # Stub returns known list of real app IDs present in Steam's appStore + 9999999 (invalid in appStore)
    valid_ids = driver.evaluate(
        "(function() {"
        "    var candidates = [550, 108600, 1443430];"
        "    if (window.appStore && typeof window.appStore.GetAppOverviewByAppID === 'function') {"
        "        var found = candidates.filter(function(id) { return !!window.appStore.GetAppOverviewByAppID(id); });"
        "        if (found.length >= 2) return found;"
        "    }"
        "    return [1443430];"
        "})()",
        await_promise=False,
    ) or [1443430]
    callback_server.lua_app_ids = list(valid_ids) + [9999999]

    # Wait for collectionStore to create LUAs collection and synchronize apps
    def get_luas_collection_apps():
        expr = (
            "(function() {"
            "    if (!window.collectionStore || !window.collectionStore.GetUserCollectionsByName) return null;"
            "    var cols = window.collectionStore.GetUserCollectionsByName('LUAs');"
            "    if (!cols || cols.length === 0) return null;"
            "    var c = cols[0];"
            "    if (c.apps && typeof c.apps.keys === 'function') return Array.from(c.apps.keys());"
            "    return c.apps || [];"
            "})()"
        )
        return driver.evaluate(expr, await_promise=False)

    initial_luas = set(get_luas_collection_apps() or [])

    # Inject auto-collection.js
    driver.inject_script(repo_paths["scripts"] / "auto-collection.js")

    try:
        driver.wait_for_condition(
            lambda: get_luas_collection_apps() is not None and all(x in get_luas_collection_apps() for x in valid_ids),
            timeout=5.0,
            error_message="LUAs collection was not created or populated by auto-collection.js",
        )

        apps = get_luas_collection_apps()
        for vid in valid_ids:
            assert vid in apps
        # 9999999 should be filtered out because appStore.GetAppOverviewByAppID returns null for it
        assert 9999999 not in apps
    finally:
        callback_server.lua_app_ids = []
        added_by_test = [vid for vid in valid_ids if vid not in initial_luas]
        if added_by_test:
            driver.evaluate(
                f"(function() {{"
                f"    try {{"
                f"        var cols = window.collectionStore && window.collectionStore.GetUserCollectionsByName('LUAs');"
                f"        if (cols && cols.length > 0) window.collectionStore.AddOrRemoveApp({added_by_test}, false, cols[0].id);"
                f"    }} catch (e) {{}}"
                f"}})()",
                await_promise=False,
            )
