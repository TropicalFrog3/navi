"""
@file  test_store_script.py
@brief Integration test for store page script DOM manipulation


Unit and syntax tests for res/inject-scripts/store-page-script.js
Verifies that store hiding logic has been replaced with native ownership decoration ("IN LIBRARY" label).
"""

import subprocess
from pathlib import Path
from tests.integration.ui.harness.browser_driver import BrowserDriver


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
        "GDynamicStore.DecorateDynamicItems",
        "game_area_already_owned page_content",
        "already_in_library",
        "sessionStorage.removeItem('sls_steam_hide_owned')",
        "app.m_userFilterFailure.already_owned = true",
        "setOwnedApps",
        "AccountOwnedApps",
        "dynamicuserdata",
    ]

    for snippet in required_snippets:
        assert snippet in content, f"Required snippet '{snippet}' missing from store-page-script.js"


def test_steam_cef_execution():
    """
    Run the ownership logic directly inside the real Steam Store CEF runtime
    to ensure it populates GDynamicStore, clears legacy session keys,
    and preserves element visibility without throwing exceptions.
    """
    driver = BrowserDriver().start()
    try:
        driver.navigate_to_store(1443430)
        content = STORE_SCRIPT_PATH.read_text(encoding="utf-8")
        code = f"""
        (() => {{
            const origGDynamic = window.GDynamicStore;
            const origCache = window.StoreItemCache;
            const tempWrap = document.createElement('div');
            tempWrap.id = 'sls-test-cef-exec-wrap';
            tempWrap.innerHTML = `
                <div class="home_content_reason">Since you recently played <a id="sls-test-reason-link" href="https://store.steampowered.com/app/480/Spacewar/">Spacewar</a></div>
                <div class="home_content single_buttonbar"><a id="sls-test-btn-link" class="btnv6_white_transparent btn_small_tall" href="https://store.steampowered.com/app/480/Spacewar/"><span>Visit Product Page</span></a></div>
                <div id="sls-test-parent-ds" class="gamelink home_content_items" data-ds-appid="480"><a id="sls-test-child-cap" class="capsule_image_ctn" href="https://store.steampowered.com/app/480/Spacewar/"></a></div>
            `;
            document.body.appendChild(tempWrap);
            try {{
                sessionStorage.setItem('sls_steam_hide_owned', '1');
                const mockGDynamic = origGDynamic ? Object.create(origGDynamic) : {{}};
                mockGDynamic.s_rgOwnedApps = {{}};
                mockGDynamic.m_bLoadComplete = true;
                mockGDynamic.DecorateDynamicItems = ($sel, bForce) => {{
                    window.decorateCalled = true;
                    if (origGDynamic && typeof origGDynamic.DecorateDynamicItems === 'function') {{
                        return origGDynamic.DecorateDynamicItems($sel, bForce);
                    }}
                }};
                window.GDynamicStore = mockGDynamic;
                let cachedApps = new Map();
                cachedApps.set(480, {{ m_userFilterFailure: {{}}, m_bVisible: true }});
                window.StoreItemCache = {{ m_mapApps: cachedApps }};

                let legacy = document.createElement('div');
                legacy.id = 'sls-hide-lua-library';
                document.body.appendChild(legacy);

                var slsLuaAppIds = new Set([480, 730]);
                var slsLuaAppIdsLoaded = true;

                {content[content.find('var isTesting'):content.find('function initLuaLibraryOwnership()')]}

                applyLuaLibraryOwnership();

                const reasonLink = document.getElementById('sls-test-reason-link');
                const btnLink = document.getElementById('sls-test-btn-link');
                const childCap = document.getElementById('sls-test-child-cap');
                const parentDs = document.getElementById('sls-test-parent-ds');

                return {{
                    sessionStorageCleared: sessionStorage.getItem('sls_steam_hide_owned') === null,
                    legacyRemoved: document.getElementById('sls-hide-lua-library') === null,
                    rgOwned480: window.GDynamicStore.s_rgOwnedApps[480] === true,
                    rgOwned730: window.GDynamicStore.s_rgOwnedApps[730] === true,
                    decorateCalled: !!window.decorateCalled,
                    app480Owned: window.StoreItemCache.m_mapApps.get(480).m_userFilterFailure.already_owned === true,
                    app480Visible: window.StoreItemCache.m_mapApps.get(480).m_bVisible === true,
                    reasonLinkFlagged: !!(reasonLink && reasonLink.querySelector('.ds_owned_flag, .sls-in-library-flag')),
                    btnLinkFlagged: !!(btnLink && btnLink.querySelector('.ds_owned_flag, .sls-in-library-flag')),
                    childCapFlagged: !!(childCap && childCap.querySelector('.ds_owned_flag, .sls-in-library-flag')),
                    parentDsFlagged: !!(parentDs && parentDs.querySelector('.ds_owned_flag'))
                }};
            }} finally {{
                tempWrap.remove();
                window.GDynamicStore = origGDynamic;
                window.StoreItemCache = origCache;
            }}
        }})()
        """
        res = driver.evaluate(code)
        assert res.get("sessionStorageCleared") is True, "sls_steam_hide_owned was not cleared from sessionStorage"
        assert res.get("legacyRemoved") is True, "Legacy style element was not removed from DOM"
        assert res.get("rgOwned480") is True, "AppID 480 was not marked owned in GDynamicStore"
        assert res.get("rgOwned730") is True, "AppID 730 was not marked owned in GDynamicStore"
        assert res.get("decorateCalled") is True, "GDynamicStore.DecorateDynamicItems was not called"
        assert res.get("app480Owned") is True, "StoreItemCache app missing already_owned flag"
        assert res.get("app480Visible") is True, "StoreItemCache app visibility was mutated to false"
        assert res.get("reasonLinkFlagged") is False, "Text link in home_content_reason should not receive IN LIBRARY flag"
        assert res.get("btnLinkFlagged") is False, "Action button link in single_buttonbar should not receive IN LIBRARY flag"
        assert res.get("childCapFlagged") is False, "Nested capsule link inside [data-ds-appid] should not receive duplicate IN LIBRARY flag"
        assert res.get("parentDsFlagged") is True, "Capsule container [data-ds-appid] missing native .ds_owned_flag"
    finally:
        driver.stop()


def test_testing_mode_decorates_all_items():
    """
    Verify in real Steam Store CEF that when isTesting is true, even unowned items found on the page
    are decorated with Steam's native 'ds_owned_flag' via GDynamicStore.DecorateDynamicItems.
    """
    driver = BrowserDriver().start()
    try:
        driver.navigate_to_store(1443430)
        content = STORE_SCRIPT_PATH.read_text(encoding="utf-8")
        code = f"""
        (() => {{
            const origGDynamic = window.GDynamicStore;
            const origTesting = window.__slsIsTesting;
            const tempWrap = document.createElement('div');
            tempWrap.id = 'sls-test-store-wrap';
            tempWrap.innerHTML = `
                <div id="item10" data-ds-appid="10"></div>
                <div id="item20" data-ds-appid="20"></div>
                <a id="item30" class="search_result_row" href="/app/30"></a>
                <div class="home_content_reason">Since you recently played <a id="reasonLink" href="/app/30">Hydroneer</a></div>
            `;
            document.body.appendChild(tempWrap);
            try {{
                window.__slsIsTesting = true;
                const mockGDynamic = origGDynamic ? Object.create(origGDynamic) : {{}};
                mockGDynamic.s_rgOwnedApps = {{}};
                mockGDynamic.m_bLoadComplete = true;
                mockGDynamic.DecorateDynamicItems = ($sel, bForce) => {{
                    window.decorateCount = (window.decorateCount || 0) + 1;
                    if (origGDynamic && typeof origGDynamic.DecorateDynamicItems === 'function') {{
                        return origGDynamic.DecorateDynamicItems($sel, bForce);
                    }}
                }};
                window.GDynamicStore = mockGDynamic;

                var slsLuaAppIds = new Set();
                var slsLuaAppIdsLoaded = true;

                {content[content.find('var isTesting'):content.find('function initLuaLibraryOwnership()')]}

                applyLuaLibraryOwnership();

                const el10 = document.getElementById('item10');
                const el20 = document.getElementById('item20');
                const el30 = document.getElementById('item30');
                const reasonEl = document.getElementById('reasonLink');

                return {{
                    owned10: window.GDynamicStore.s_rgOwnedApps[10] === true,
                    owned20: window.GDynamicStore.s_rgOwnedApps[20] === true,
                    owned30: window.GDynamicStore.s_rgOwnedApps[30] === true,
                    flag10: !!(el10 && el10.querySelector('.ds_owned_flag') && el10.innerHTML.includes('IN LIBRARY')),
                    flag20: !!(el20 && el20.querySelector('.ds_owned_flag') && el20.innerHTML.includes('IN LIBRARY')),
                    flag30: !!(el30 && el30.querySelector('.ds_owned_flag') && el30.innerHTML.includes('IN LIBRARY')),
                    reasonLinkHasFlag: !!(reasonEl && reasonEl.innerHTML.includes('IN LIBRARY'))
                }};
            }} finally {{
                tempWrap.remove();
                window.GDynamicStore = origGDynamic;
                window.__slsIsTesting = origTesting;
            }}
        }})()
        """
        res = driver.evaluate(code)
        assert res.get("owned10") is True, "Item 10 not marked owned in GDynamicStore"
        assert res.get("owned20") is True, "Item 20 not marked owned in GDynamicStore"
        assert res.get("owned30") is True, "Item 30 not marked owned in GDynamicStore"
        assert res.get("flag10") is True, "Item 10 missing native ds_owned_flag IN LIBRARY badge"
        assert res.get("flag20") is True, "Item 20 missing native ds_owned_flag IN LIBRARY badge"
        assert res.get("flag30") is True, "Item 30 missing native ds_owned_flag IN LIBRARY badge"
        assert res.get("reasonLinkHasFlag") is False, "Plain text reason link received IN LIBRARY flag"
    finally:
        driver.stop()


def test_app_page_native_queue_and_playtime_banner():
    """
    Verify on an /app/<appid>/ product page in real Steam Store CEF that applyLuaLibraryOwnership()
    renders Steam's native .game_area_already_owned.page_content banner and .game_area_play_stats
    inside .queue_and_playtime and hides #add_to_wishlist_area.
    """
    driver = BrowserDriver().start()
    try:
        driver.navigate_to_store(1443430)
        content = STORE_SCRIPT_PATH.read_text(encoding="utf-8")
        code = f"""
        (() => {{
            const origGDynamic = window.GDynamicStore;
            const origTesting = window.__slsIsTesting;
            const origUrl = window.location.href;
            const stashed = [];
            for (const el of document.querySelectorAll('.queue_and_playtime, #appHubAppName, .apphub_AppName, #add_to_wishlist_area, #queueCtn')) {{
                if (el.parentNode) {{
                    stashed.push({{ el, parent: el.parentNode, next: el.nextSibling }});
                    el.parentNode.removeChild(el);
                }}
            }}
            const tempWrap = document.createElement('div');
            tempWrap.id = 'sls-test-app-page-wrap';
            tempWrap.innerHTML = `
                <div id="appHubAppName" class="apphub_AppName">Test Spoofed Game</div>
                <div class="queue_and_playtime">
                    <div class="queue_overflow_ctn">
                        <div id="queueCtn" class="queue_ctn">
                            <div id="add_to_wishlist_area"><a>Add to your wishlist</a></div>
                        </div>
                    </div>
                    <div id="purchaseOptionsContent" class="purchase_options_content"></div>
                </div>
                <div id="game_area_purchase" class="game_area_purchase"></div>
            `;
            document.body.appendChild(tempWrap);
            try {{
                history.replaceState(null, '', '/app/987654/Test_Spoofed_Game/');
                window.__slsIsTesting = false;
                window.GDynamicStore = {{
                    s_rgOwnedApps: {{}},
                    DecorateDynamicItems: () => {{}}
                }};

                var slsLuaAppIds = new Set([987654]);
                var slsLuaAppIdsLoaded = true;

                {content[content.find('var isTesting'):content.find('function initLuaLibraryOwnership()')]}

                applyLuaLibraryOwnership();

                const ownedBanner = tempWrap.querySelector('.queue_and_playtime .game_area_already_owned.page_content');
                const flagEl = ownedBanner ? ownedBanner.querySelector('.game_area_already_owned_ctn .ds_owned_flag.ds_flag') : null;
                const textEl = ownedBanner ? ownedBanner.querySelector('.already_in_library') : null;
                const playLink = tempWrap.querySelector('.queue_and_playtime .game_area_play_stats a[href="steam://launch/987654/Dialog"]');
                const wishlistArea = tempWrap.querySelector('#add_to_wishlist_area');
                const queueCtn = tempWrap.querySelector('#queueCtn');
                const legacyPurchaseBanner = tempWrap.querySelector('#game_area_purchase #game_area_already_owned');

                return {{
                    hasBanner: !!ownedBanner,
                    hasNativeFlag: !!(flagEl && flagEl.textContent.includes('IN LIBRARY')),
                    bannerText: textEl ? textEl.textContent : '',
                    hasPlayNow: !!(playLink && playLink.textContent.trim() === 'Play Now'),
                    wishlistHidden: !!(wishlistArea && wishlistArea.style.display === 'none'),
                    queueHasClass: !!(queueCtn && queueCtn.classList.contains('already_owned')),
                    noLegacyPurchaseBanner: !legacyPurchaseBanner
                }};
            }} finally {{
                history.replaceState(null, '', origUrl);
                tempWrap.remove();
                for (const item of stashed) {{
                    item.parent.insertBefore(item.el, item.next);
                }}
                window.GDynamicStore = origGDynamic;
                window.__slsIsTesting = origTesting;
            }}
        }})()
        """
        res = driver.evaluate(code)
        assert res.get("hasBanner") is True, "Missing native .game_area_already_owned.page_content inside .queue_and_playtime"
        assert res.get("hasNativeFlag") is True, "Missing .ds_owned_flag.ds_flag inside .game_area_already_owned_ctn"
        assert res.get("bannerText") == "Test Spoofed Game is already in your Steam library", f"Unexpected bannerText: {res.get('bannerText')}"
        assert res.get("hasPlayNow") is True, "Missing .game_area_play_stats 'Play Now' launch button"
        assert res.get("wishlistHidden") is True, "#add_to_wishlist_area was not hidden"
        assert res.get("queueHasClass") is True, "#queueCtn missing already_owned class"
        assert res.get("noLegacyPurchaseBanner") is True, "Legacy #game_area_already_owned banner should not be injected inside #game_area_purchase"
    finally:
        driver.stop()


def test_testing_mode_toggle_off():
    """
    Verify in real Steam Store CEF that when window.__slsIsTesting is false, items not in slsLuaAppIds
    are NOT marked as owned.
    """
    driver = BrowserDriver().start()
    try:
        driver.navigate_to_store(1443430)
        content = STORE_SCRIPT_PATH.read_text(encoding="utf-8")
        code = f"""
        (() => {{
            const origGDynamic = window.GDynamicStore;
            const origTesting = window.__slsIsTesting;
            const tempWrap = document.createElement('div');
            tempWrap.id = 'sls-test-store-wrap-off';
            tempWrap.innerHTML = `<div id="item99999" data-ds-appid="99999"></div>`;
            document.body.appendChild(tempWrap);
            try {{
                window.__slsIsTesting = false;
                window.GDynamicStore = {{
                    s_rgOwnedApps: {{}},
                    DecorateDynamicItems: () => {{}}
                }};

                var slsLuaAppIds = new Set([480]);
                var slsLuaAppIdsLoaded = true;

                {content[content.find('var isTesting'):content.find('function initLuaLibraryOwnership()')]}

                applyLuaLibraryOwnership();

                const el99999 = document.getElementById('item99999');

                return {{
                    owned99999: window.GDynamicStore.s_rgOwnedApps['99999'] === true,
                    hasFlag: el99999 && el99999.children.length > 0
                }};
            }} finally {{
                tempWrap.remove();
                window.GDynamicStore = origGDynamic;
                window.__slsIsTesting = origTesting;
            }}
        }})()
        """
        res = driver.evaluate(code)
        assert res.get("owned99999") is False, "Item 99999 was marked owned when testing mode was disabled!"
        assert res.get("hasFlag") is False, "Element received flag when testing mode was disabled!"
    finally:
        driver.stop()
