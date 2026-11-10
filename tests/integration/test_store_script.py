"""
@file  test_store_script.py
@brief Integration test for store page script DOM manipulation


Unit and syntax tests for res/inject-scripts/store-page-script.js
Verifies that store hiding logic has been replaced with native ownership decoration ("IN LIBRARY" label).
"""

import subprocess
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent.parent
STORE_SCRIPT_PATH = REPO_ROOT / "res" / "inject-scripts" / "store" / "store-page-script.js"
if not STORE_SCRIPT_PATH.exists():
    STORE_SCRIPT_PATH = REPO_ROOT / "res" / "inject-scripts" / "store-page-script.js"


def test_store_page_script_syntax():
    """Verify that store-page-script.js is syntactically valid JavaScript."""
    assert STORE_SCRIPT_PATH.is_file(), f"Script not found at {STORE_SCRIPT_PATH}"
    res = subprocess.run(
        ["node", "--check", str(STORE_SCRIPT_PATH)],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0, f"node --check failed:\n{res.stderr}"


def test_hiding_functions_completely_removed():
    """
    Ensure all manual DOM-collapsing, CSS hiding, and carousel-reflow functions
    have been stripped from the store script.
    """
    content = STORE_SCRIPT_PATH.read_text(encoding="utf-8")

    forbidden_identifiers = [
        "collapseSingleGameContainers",
        "reflowCarouselTrays",
        "isHideLibraryEnabled",
        "filterApplicationConfigData",
        "isMultiItemContainer",
        "slsHideLibraryStyleEl",
        "sls-hide-lua-library",  # As an active stylesheet ID creation
        "app.m_bVisible = false",
    ]

    for identifier in forbidden_identifiers:
        # Note: 'sls-hide-lua-library' may be referenced in cleanupLegacyHiding for removal,
        # but document.createElement('style') + id = 'sls-hide-lua-library' must not exist.
        if identifier == "sls-hide-lua-library":
            assert "slsHideLibraryStyleEl = document.createElement('style')" not in content
            assert "slsHideLibraryStyleEl.id = 'sls-hide-lua-library'" not in content
        else:
            assert identifier not in content, f"Forbidden identifier '{identifier}' still present in store-page-script.js"


def test_ownership_decoration_functions_present():
    """
    Verify that ownership synchronization and legacy cleanup are properly structured.
    """
    content = STORE_SCRIPT_PATH.read_text(encoding="utf-8")

    assert "var isTesting" in content, "Variable 'var isTesting' missing from store-page-script.js"

    required_snippets = [
        "function cleanupLegacyHiding()",
        "function applyLuaLibraryOwnership()",
        "function initLuaLibraryOwnership()",
        "GDynamicStore.s_rgOwnedApps",
        "GDynamicStore.DecorateDynamicItems()",
        "sessionStorage.removeItem('sls_steam_hide_owned')",
        "app.m_userFilterFailure.already_owned = true",
        "setOwnedApps",
        "AccountOwnedApps",
        "dynamicuserdata",
    ]

    for snippet in required_snippets:
        assert snippet in content, f"Required snippet '{snippet}' missing from store-page-script.js"


def test_node_execution_simulation():
    """
    Run a simulated Node.js execution environment of the ownership logic
    to ensure it populates GDynamicStore, clears legacy session keys,
    and preserves element visibility without throwing exceptions.
    """
    node_eval_code = """
    const fs = require('fs');
    const content = fs.readFileSync(process.argv[1], 'utf8');

    // Minimal browser mock
    global.window = global;
    let sessionStorageData = { 'sls_steam_hide_owned': '1' };
    global.sessionStorage = {
        getItem: (k) => sessionStorageData[k] || null,
        setItem: (k, v) => { sessionStorageData[k] = String(v); },
        removeItem: (k) => { delete sessionStorageData[k]; }
    };

    let decorateCalled = false;
    global.GDynamicStore = {
        s_rgOwnedApps: {},
        DecorateDynamicItems: () => { decorateCalled = true; }
    };

    let cachedApps = new Map();
    cachedApps.set(480, { m_userFilterFailure: {}, m_bVisible: true });
    global.StoreItemCache = {
        m_mapApps: cachedApps
    };

    let legacyElementRemoved = false;
    let appendedFlags = [];
    global.document = {
        getElementById: (id) => {
            if (id === 'sls-hide-lua-library') {
                return {
                    parentNode: {
                        removeChild: () => { legacyElementRemoved = true; }
                    }
                };
            }
            return null;
        },
        querySelectorAll: (sel) => [],
        createElement: (tag) => {
            let el = {
                className: '',
                innerHTML: '',
                style: {},
                classList: { add: () => {} },
                appendChild: (ch) => appendedFlags.push(ch)
            };
            return el;
        },
        head: { appendChild: () => {} },
        documentElement: { style: {} },
        addEventListener: () => {}
    };

    // Extract ownership definitions and verify they run cleanly
    eval(`
        var slsLuaAppIds = new Set([480, 730]);
        var slsLuaAppIdsLoaded = true;

        ${content.substring(content.indexOf('var isTesting'), content.indexOf('function initLuaLibraryOwnership()'))}

        applyLuaLibraryOwnership();
    `);

    // Assertions
    if (sessionStorage.getItem('sls_steam_hide_owned') !== null) {
        throw new Error('sls_steam_hide_owned was not cleared from sessionStorage');
    }
    if (!legacyElementRemoved) {
        throw new Error('Legacy style element was not removed from DOM');
    }
    if (global.GDynamicStore.s_rgOwnedApps[480] !== true || global.GDynamicStore.s_rgOwnedApps[730] !== true) {
        throw new Error('AppIDs were not marked owned in GDynamicStore.s_rgOwnedApps');
    }
    if (!decorateCalled) {
        throw new Error('GDynamicStore.DecorateDynamicItems was not called');
    }
    const app480 = global.StoreItemCache.m_mapApps.get(480);
    if (!app480.m_userFilterFailure.already_owned) {
        throw new Error('StoreItemCache app missing already_owned flag');
    }
    if (app480.m_bVisible !== true) {
        throw new Error('StoreItemCache app visibility was mutated to false!');
    }

    console.log('SIMULATION_PASSED');
    """

    res = subprocess.run(
        ["node", "-e", node_eval_code, str(STORE_SCRIPT_PATH)],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0, f"Node simulation failed:\n{res.stderr}\n{res.stdout}"
    assert "SIMULATION_PASSED" in res.stdout


def test_testing_mode_decorates_all_items():
    """
    Verify that when isTesting is true, even unowned items found on the page
    are decorated with the 'IN LIBRARY' label and synchronized to GDynamicStore.
    """
    node_eval_code = """
    const fs = require('fs');
    const content = fs.readFileSync(process.argv[1], 'utf8');

    global.window = global;
    global.window.__slsIsTesting = true;
    global.sessionStorage = { getItem: () => null, setItem: () => {}, removeItem: () => {} };

    let decorateCount = 0;
    global.GDynamicStore = {
        s_rgOwnedApps: {},
        DecorateDynamicItems: () => { decorateCount++; }
    };

    // Simulate 3 store capsules on page: 10, 20, 30
    let elements = [
        {
            attrs: { 'data-ds-appid': '10' },
            getAttribute: function(a) { return this.attrs[a]; },
            querySelector: () => null,
            classList: { add: (c) => {} },
            style: { removeProperty: () => {} },
            children: [],
            appendChild: function(c) { this.children.push(c); }
        },
        {
            attrs: { 'data-ds-appid': '20' },
            getAttribute: function(a) { return this.attrs[a]; },
            querySelector: () => null,
            classList: { add: (c) => {} },
            style: { removeProperty: () => {} },
            children: [],
            appendChild: function(c) { this.children.push(c); }
        },
        {
            attrs: { 'href': '/app/30' },
            getAttribute: function(a) { return this.attrs[a]; },
            querySelector: () => null,
            classList: { add: (c) => {} },
            style: { removeProperty: () => {} },
            children: [],
            appendChild: function(c) { this.children.push(c); }
        }
    ];

    global.document = {
        getElementById: () => null,
        querySelectorAll: (sel) => (sel.includes('.sls-lua-library-hidden') ? [] : elements),
        createElement: (tag) => ({ className: '', innerHTML: '', style: {} }),
        head: { appendChild: () => {} },
        documentElement: { style: {} },
        addEventListener: () => {}
    };

    eval(`
        var slsLuaAppIds = new Set(); // No Lua apps loaded
        var slsLuaAppIdsLoaded = true;

        ${content.substring(content.indexOf('var isTesting'), content.indexOf('function initLuaLibraryOwnership()'))}

        applyLuaLibraryOwnership();
    `);

    // Verify all 3 items (10, 20, 30) were marked as owned and decorated
    if (global.GDynamicStore.s_rgOwnedApps[10] !== true ||
        global.GDynamicStore.s_rgOwnedApps[20] !== true ||
        global.GDynamicStore.s_rgOwnedApps[30] !== true) {
        throw new Error('Testing mode failed to mark all items owned in GDynamicStore');
    }

    for (let el of elements) {
        if (el.children.length === 0 || !el.children[0].innerHTML.includes('IN LIBRARY')) {
            throw new Error('Testing mode failed to append IN LIBRARY flag to element');
        }
    }

    console.log('TESTING_MODE_PASSED');
    """

    res = subprocess.run(
        ["node", "-e", node_eval_code, str(STORE_SCRIPT_PATH)],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0, f"Testing mode node simulation failed:\n{res.stderr}\n{res.stdout}"
    assert "TESTING_MODE_PASSED" in res.stdout


def test_testing_mode_toggle_off():
    """
    Verify that when window.__slsIsTesting is false, items not in slsLuaAppIds
    are NOT marked as owned.
    """
    node_eval_code = """
    const fs = require('fs');
    const content = fs.readFileSync(process.argv[1], 'utf8');

    global.window = global;
    global.window.__slsIsTesting = false;
    global.sessionStorage = { getItem: () => null, setItem: () => {}, removeItem: () => {} };

    global.GDynamicStore = {
        s_rgOwnedApps: {},
        DecorateDynamicItems: () => {}
    };

    let elements = [
        {
            attrs: { 'data-ds-appid': '99999' },
            getAttribute: function(a) { return this.attrs[a]; },
            querySelector: () => null,
            classList: { add: (c) => {} },
            style: { removeProperty: () => {} },
            children: [],
            appendChild: function(c) { this.children.push(c); }
        }
    ];

    global.document = {
        getElementById: () => null,
        querySelectorAll: (sel) => (sel.includes('.sls-lua-library-hidden') ? [] : elements),
        createElement: (tag) => ({ className: '', innerHTML: '', style: {} }),
        head: { appendChild: () => {} },
        documentElement: { style: {} },
        addEventListener: () => {}
    };

    eval(`
        var slsLuaAppIds = new Set([480]); // 99999 is NOT in the Lua library
        var slsLuaAppIdsLoaded = true;

        ${content.substring(content.indexOf('var isTesting'), content.indexOf('function initLuaLibraryOwnership()'))}

        applyLuaLibraryOwnership();
    `);

    // Verify 99999 was NOT marked as owned
    if (global.GDynamicStore.s_rgOwnedApps['99999'] === true) {
        throw new Error('Item 99999 was marked owned when testing mode was disabled!');
    }
    if (elements[0].children.length > 0) {
        throw new Error('Element received flag when testing mode was disabled!');
    }

    console.log('TOGGLE_OFF_PASSED');
    """

    res = subprocess.run(
        ["node", "-e", node_eval_code, str(STORE_SCRIPT_PATH)],
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0, f"Toggle off node simulation failed:\n{res.stderr}\n{res.stdout}"
    assert "TOGGLE_OFF_PASSED" in res.stdout
