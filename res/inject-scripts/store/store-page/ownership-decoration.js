/**
 * @file  ownership-decoration.js
 * @brief Lua library ownership synchronization and GDynamicStore 'IN LIBRARY' decoration in store pages
 */
// Test mode flag: when true, every item/capsule on the page receives the "IN LIBRARY" label.
// Can also be toggled at runtime via window.__slsIsTesting.
var isTesting = false;

function getIsTesting() {
    if (typeof window !== 'undefined' && typeof window.__slsIsTesting === 'boolean') {
        return window.__slsIsTesting;
    }
    return (typeof isTesting !== 'undefined') ? isTesting : false;
}

var slsLuaAppIds = (typeof slsLuaAppIds !== 'undefined') ? slsLuaAppIds : new Set();
var slsLuaAppIdsLoaded = (typeof slsLuaAppIdsLoaded !== 'undefined') ? slsLuaAppIdsLoaded : false;

function cleanupLegacyHiding() {
    try {
        sessionStorage.removeItem('sls_steam_hide_owned');
    } catch(e) {}

    var legacyStyle = document.getElementById('sls-hide-lua-library');
    if (legacyStyle && legacyStyle.parentNode) {
        legacyStyle.parentNode.removeChild(legacyStyle);
    }

    var legacyHiddenEls = document.querySelectorAll('.sls-lua-library-hidden');
    for (var i = 0; i < legacyHiddenEls.length; i++) {
        var el = legacyHiddenEls[i];
        if (el.classList && typeof el.classList.remove === 'function') {
            el.classList.remove('sls-lua-library-hidden');
        }
        if (el.style) {
            el.style.removeProperty('display');
            el.style.removeProperty('width');
            el.style.removeProperty('height');
            el.style.removeProperty('min-width');
            el.style.removeProperty('min-height');
            el.style.removeProperty('max-width');
            el.style.removeProperty('max-height');
            el.style.removeProperty('margin');
            el.style.removeProperty('padding');
            el.style.removeProperty('border');
            el.style.removeProperty('outline');
            el.style.removeProperty('box-shadow');
            el.style.removeProperty('position');
            el.style.removeProperty('pointer-events');
            el.style.removeProperty('visibility');
            el.style.removeProperty('opacity');
            el.style.removeProperty('overflow');
        }
    }
}

function applyLuaLibraryOwnership() {
    var testMode = (typeof getIsTesting === 'function') ? getIsTesting() : (typeof isTesting !== 'undefined' ? isTesting : false);
    if (!testMode && (!slsLuaAppIdsLoaded || slsLuaAppIds.size === 0)) return;

    cleanupLegacyHiding();

    // When test mode is active, collect all visible AppIDs from the DOM into the ownership set
    var targetAppIds = new Set(slsLuaAppIds);
    if (testMode) {
        var candidateElements = document.querySelectorAll(
            '[data-ds-appid], [data-ds-itemkey], a[href*="/app/"], .search_result_row'
        );
        for (var ci = 0; ci < candidateElements.length; ci++) {
            var cel = candidateElements[ci];
            var cAid = cel.getAttribute('data-ds-appid');
            if (!cAid) {
                var chref = cel.getAttribute('href') || cel.href || '';
                var cm = chref.match(/\/app\/([0-9]+)/);
                if (cm) cAid = cm[1];
            }
            if (cAid) {
                var parsedAid = parseInt(cAid, 10);
                if (parsedAid) targetAppIds.add(parsedAid);
            }
        }
    }

    // 1. Synchronize into GDynamicStore.s_rgOwnedApps (native "IN LIBRARY" badge on store capsules/rows)
    if (window.GDynamicStore && GDynamicStore.s_rgOwnedApps) {
        var needsDecorate = false;
        targetAppIds.forEach(function(aid) {
            if (!GDynamicStore.s_rgOwnedApps[aid]) {
                GDynamicStore.s_rgOwnedApps[aid] = true;
                needsDecorate = true;
            }
        });

        // If jQuery is present, un-instrument elements so GDynamicStore decorates already-rendered capsules
        if (window.jQuery && (testMode || needsDecorate)) {
            try {
                window.jQuery('[data-ds-appid]').each(function() {
                    var $el = window.jQuery(this);
                    var aid = $el.data('dsAppid') || $el.attr('data-ds-appid');
                    if (testMode || (aid && targetAppIds.has(Number(aid)))) {
                        if (!$el.find('.ds_owned_flag').length) {
                            $el.removeData('dsInstrumented');
                        }
                    }
                });
            } catch(e) {}
        }

        if ((needsDecorate || testMode) && typeof GDynamicStore.DecorateDynamicItems === 'function') {
            GDynamicStore.DecorateDynamicItems();
        }
    }

    // 2. Synchronize into StoreItemCache if present (used by modern React content hubs)
    if (window.StoreItemCache && StoreItemCache.m_mapApps) {
        targetAppIds.forEach(function(aid) {
            var app = StoreItemCache.m_mapApps.get(Number(aid));
            if (app) {
                if (!app.m_userFilterFailure) app.m_userFilterFailure = {};
                app.m_userFilterFailure.already_owned = true;
                // Preserve normal owned visibility and ribbons without manual hiding
            }
        });
    }

    // 3. Synchronize into modern Webpack React Query & UserPreferences stores
    try {
        if (window.webpackChunkstore && Array.isArray(window.webpackChunkstore)) {
            var req = null;
            window.webpackChunkstore.push([['__sls_sync_hook_' + Date.now()], {}, function(r) { req = r; }]);
            if (req) {
                try {
                    var mod6469 = req(6469);
                    if (mod6469 && mod6469.Fm && typeof mod6469.Fm.Get === 'function') {
                        var store = mod6469.Fm.Get();
                        var data = store.ReadData();
                        if (data && data.setOwnedApps) {
                            targetAppIds.forEach(function(aid) {
                                data.setOwnedApps.add(Number(aid));
                            });
                        }
                        if (store.m_queryClient) {
                            var qc = store.m_queryClient;
                            qc.getQueryCache().getAll().forEach(function(q) {
                                if (q.queryKey && Array.isArray(q.queryKey)) {
                                    if (q.queryKey[0] === 'AccountOwnedApps' && q.state && q.state.data instanceof Set) {
                                        targetAppIds.forEach(function(aid) {
                                            q.state.data.add(Number(aid));
                                        });
                                    }
                                    if (q.queryKey[0] === 'dynamicuserdata' && q.state && q.state.data && q.state.data.setOwnedApps instanceof Set) {
                                        targetAppIds.forEach(function(aid) {
                                            q.state.data.setOwnedApps.add(Number(aid));
                                        });
                                    }
                                }
                            });
                        }
                    }
                } catch(e) {}
            }
        }
    } catch(e) {}

    // 4. Direct DOM decoration for any capsules/rows not handled by GDynamicStore
    var targetElements = document.querySelectorAll(
        '[data-ds-appid], .search_result_row, [class*="salecapsule_"], [class*="CapsuleContainer"], [class*="StoreSaleWidget"], a[href*="/app/"]'
    );
    for (var i = 0; i < targetElements.length; i++) {
        var el = targetElements[i];
        var aid = null;
        var dsAid = el.getAttribute('data-ds-appid');
        if (dsAid) {
            aid = parseInt(dsAid, 10);
        } else {
            var href = el.getAttribute('href') || el.href || '';
            var m = href.match(/\/app\/([0-9]+)/);
            if (m) aid = parseInt(m[1], 10);
        }

        if (testMode || (aid && targetAppIds.has(aid))) {
            if (!el.querySelector('.ds_owned_flag, .sls-in-library-flag')) {
                el.classList.add('ds_flagged', 'ds_owned');
                var flag = document.createElement('div');
                flag.className = 'ds_flag ds_owned_flag sls-in-library-flag';
                flag.innerHTML = 'IN LIBRARY&nbsp;&nbsp;';

                var computedPos = window.getComputedStyle ? window.getComputedStyle(el).position : el.style.position;
                if (!computedPos || computedPos === 'static') {
                    el.style.position = 'relative';
                }
                el.appendChild(flag);
            }
        }
    }

    // 5. Product/App Page Banner (#game_area_already_owned)
    var pageMatch = (typeof window !== 'undefined' && window.location && window.location.href)
        ? window.location.href.match(/\/app\/([0-9]+)/)
        : null;
    if (pageMatch && pageMatch[1]) {
        var pageAid = parseInt(pageMatch[1], 10);
        if (testMode || targetAppIds.has(pageAid)) {
            var purchaseArea = document.getElementById('game_area_purchase') || document.querySelector('.game_area_purchase');
            if (purchaseArea && !document.getElementById('game_area_already_owned')) {
                var ownedBanner = document.createElement('div');
                ownedBanner.id = 'game_area_already_owned';
                ownedBanner.className = 'game_area_already_owned';
                ownedBanner.innerHTML = '<div class="ds_flag ds_owned_flag" style="display:inline-block;position:static;margin-right:8px;">IN LIBRARY</div><span>This title is already in your Steam library</span>';
                purchaseArea.insertBefore(ownedBanner, purchaseArea.firstChild);
            }
        }
    }
}

function initLuaLibraryOwnership() {
    cleanupLegacyHiding();

    // If test mode is enabled, immediately apply ownership decoration to all items
    if (getIsTesting()) {
        applyLuaLibraryOwnership();
    }

    // Fetch Lua AppIDs from local Navi server
    naviFetch('http://127.0.0.1:9001/lua_apps')
        .then(function(r) { return r.json(); })
        .then(function(data) {
            if (data && Array.isArray(data.appids)) {
                data.appids.forEach(function(id) {
                    slsLuaAppIds.add(Number(id));
                });
                slsLuaAppIdsLoaded = true;
                applyLuaLibraryOwnership();
            }
        })
        .catch(function(err) {
            ping('Failed to fetch /lua_apps: ' + err);
        });

    // If GDynamicStore is loading asynchronously, hook OnReady
    if (window.GDynamicStore) {
        if (typeof GDynamicStore.OnReady === 'function') {
            GDynamicStore.OnReady(function() {
                applyLuaLibraryOwnership();
            });
        }
    }
}

