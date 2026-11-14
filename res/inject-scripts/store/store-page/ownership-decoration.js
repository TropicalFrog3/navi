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

    var legacyFlags = document.querySelectorAll('.sls-in-library-flag');
    for (var fi = 0; fi < legacyFlags.length; fi++) {
        var flagEl = legacyFlags[fi];
        var flagParent = flagEl.parentElement;
        if (flagParent && flagParent.classList) {
            flagParent.classList.remove('ds_flagged', 'ds_owned');
        }
        if (flagEl.parentNode) {
            flagEl.parentNode.removeChild(flagEl);
        }
    }

    var legacyOwnedBanner = document.getElementById('game_area_already_owned');
    if (legacyOwnedBanner && legacyOwnedBanner.parentNode) {
        legacyOwnedBanner.parentNode.removeChild(legacyOwnedBanner);
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

function collectTargetAppIds(testMode) {
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
                var parts = String(cAid).split(',');
                for (var pi = 0; pi < parts.length; pi++) {
                    var parsedAid = parseInt(parts[pi], 10);
                    if (parsedAid) targetAppIds.add(parsedAid);
                }
            }
        }
    }
    return targetAppIds;
}

function mergeOwnedAppIdsIntoUserdata(payload, appIdSet) {
    if (!payload || typeof payload !== 'object' || !Array.isArray(payload.rgOwnedApps)) {
        return false;
    }
    if (!appIdSet || appIdSet.size === 0) return false;
    var existing = new Set();
    for (var i = 0; i < payload.rgOwnedApps.length; i++) {
        existing.add(Number(payload.rgOwnedApps[i]));
    }
    var modified = false;
    appIdSet.forEach(function(aid) {
        var numAid = Number(aid);
        if (numAid && !existing.has(numAid)) {
            existing.add(numAid);
            payload.rgOwnedApps.push(numAid);
            modified = true;
        }
    });
    return modified;
}

function installDynamicStoreUserdataHook() {
    if (typeof window === 'undefined') return;

    if (typeof window.fetch === 'function' && !window.__slsFetchOwnedHooked) {
        window.__slsFetchOwnedHooked = true;
        var origFetch = window.fetch;
        window.fetch = function(input) {
            var url = (typeof input === 'string') ? input : (input && input.url ? input.url : '');
            var fetchPromise = origFetch.apply(this, arguments);
            if (!url || url.indexOf('/dynamicstore/userdata/') === -1) {
                return fetchPromise;
            }
            return fetchPromise.then(function(response) {
                if (!response || !response.ok || typeof response.clone !== 'function') {
                    return response;
                }
                return response.clone().json().then(function(data) {
                    var targetIds = collectTargetAppIds(getIsTesting());
                    if (targetIds.size > 0 && mergeOwnedAppIdsIntoUserdata(data, targetIds)) {
                        var headers = new Headers(response.headers);
                        headers.delete('content-length');
                        return new Response(JSON.stringify(data), {
                            status: response.status,
                            statusText: response.statusText,
                            headers: headers
                        });
                    }
                    return response;
                }).catch(function() {
                    return response;
                });
            });
        };
    }

    var jq = window.$J || window.jQuery;
    if (jq && typeof jq.ajaxPrefilter === 'function' && !window.__slsAjaxOwnedHooked) {
        window.__slsAjaxOwnedHooked = true;
        jq.ajaxPrefilter(function(options) {
            if (options && typeof options.url === 'string' && options.url.indexOf('/dynamicstore/userdata/') !== -1) {
                var origDataFilter = options.dataFilter;
                options.dataFilter = function(data, type) {
                    var filtered = (typeof origDataFilter === 'function')
                        ? origDataFilter.call(this, data, type)
                        : data;
                    var targetIds = collectTargetAppIds(getIsTesting());
                    if (targetIds.size > 0 && typeof filtered === 'string') {
                        try {
                            var parsed = JSON.parse(filtered);
                            if (mergeOwnedAppIdsIntoUserdata(parsed, targetIds)) {
                                return JSON.stringify(parsed);
                            }
                        } catch(e) {}
                    }
                    return filtered;
                };
            }
        });
    }
}

function syncStoreItemCacheOwnership(itemCache, targetAppIds) {
    if (!itemCache || !itemCache.m_mapApps) return;
    targetAppIds.forEach(function(aid) {
        var app = itemCache.m_mapApps.get(Number(aid));
        if (app) {
            if (!app.m_userFilterFailure) app.m_userFilterFailure = {};
            app.m_userFilterFailure.already_owned = true;
            // Preserve normal owned visibility and ribbons without manual hiding
        }
    });
}

function syncQueryClientOwnership(qc, targetAppIds) {
    if (!qc || typeof qc.getQueryCache !== 'function') return;
    var queries = qc.getQueryCache().getAll();
    for (var qi = 0; qi < queries.length; qi++) {
        var q = queries[qi];
        if (!q || !Array.isArray(q.queryKey)) continue;
        var k0 = q.queryKey[0];
        var qData = q.state ? q.state.data : undefined;
        if (k0 === 'AccountOwnedApps' && qData instanceof Set) {
            var changedOwned = false;
            var nextOwned = new Set(qData);
            targetAppIds.forEach(function(aid) {
                var numAid = Number(aid);
                if (!nextOwned.has(numAid)) {
                    nextOwned.add(numAid);
                    changedOwned = true;
                }
            });
            if (changedOwned) {
                targetAppIds.forEach(function(aid) { qData.add(Number(aid)); });
                if (typeof qc.setQueryData === 'function') {
                    qc.setQueryData(q.queryKey, nextOwned);
                }
            }
        } else if (k0 === 'dynamicuserdata' && qData && qData.setOwnedApps instanceof Set) {
            var changedDyn = false;
            var nextDynSet = new Set(qData.setOwnedApps);
            targetAppIds.forEach(function(aid) {
                var numAid = Number(aid);
                if (!nextDynSet.has(numAid)) {
                    nextDynSet.add(numAid);
                    changedDyn = true;
                }
            });
            if (changedDyn) {
                targetAppIds.forEach(function(aid) { qData.setOwnedApps.add(Number(aid)); });
                if (typeof qc.setQueryData === 'function') {
                    qc.setQueryData(q.queryKey, Object.assign({}, qData, { setOwnedApps: nextDynSet }));
                }
            }
        } else if (k0 === 'StoreItem' && q.queryKey[2] === 'apply_user_filters' && typeof q.queryKey[1] === 'string') {
            var m = q.queryKey[1].match(/^app_([0-9]+)$/);
            if (m && targetAppIds.has(Number(m[1]))) {
                if (!qData || !qData.already_owned) {
                    if (typeof qc.setQueryData === 'function') {
                        qc.setQueryData(q.queryKey, Object.assign({}, qData || {}, { already_owned: true }));
                    }
                }
            }
        }
    }
}

function applyViteStoreModule(mod, targetAppIds) {
    if (!mod) return;
    var keys = Object.keys(mod);
    var foundQC = null;
    for (var i = 0; i < keys.length; i++) {
        var v = mod[keys[i]];
        if (!v) continue;
        try {
            if (typeof v.Get === 'function') {
                var srcStr = Function.prototype.toString.call(v);
                if (srcStr.indexOf('BIsGameOwned') !== -1 || srcStr.indexOf('m_boxCacheVersion') !== -1) {
                    var dynStore = v.Get();
                    if (dynStore) {
                        if (dynStore.m_queryClient) foundQC = dynStore.m_queryClient;
                        if (typeof dynStore.ReadData === 'function') {
                            var rData = dynStore.ReadData();
                            if (rData && rData.setOwnedApps instanceof Set) {
                                targetAppIds.forEach(function(aid) {
                                    rData.setOwnedApps.add(Number(aid));
                                });
                            }
                        }
                    }
                } else if (srcStr.indexOf('m_mapApps') !== -1) {
                    var itemCache = v.Get();
                    syncStoreItemCacheOwnership(itemCache, targetAppIds);
                }
            } else if (typeof v.getQueryCache === 'function') {
                foundQC = v;
            }
        } catch(e) {}
    }
    if (foundQC) {
        syncQueryClientOwnership(foundQC, targetAppIds);
    }
}

function syncViteAndWebpackStore(targetAppIds) {
    // Webpack React Query & UserPreferences stores (legacy storefront bundles)
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
                            syncQueryClientOwnership(store.m_queryClient, targetAppIds);
                        }
                    }
                } catch(e) {}
            }
        }
    } catch(e) {}

    // Vite ES module discovery (current store.steampowered.com applications/store/main.js)
    try {
        if (window.__slsViteStoreMod) {
            applyViteStoreModule(window.__slsViteStoreMod, targetAppIds);
            return;
        }
        var mainScript = document.querySelector('script[src*="applications/store/main.js"]');
        if (mainScript && mainScript.src) {
            if (!window.__slsViteStoreModPromise) {
                window.__slsViteStoreModPromise = import(mainScript.src)
                    .then(function(mod) {
                        window.__slsViteStoreMod = mod;
                        applyViteStoreModule(mod, collectTargetAppIds(getIsTesting()));
                        return mod;
                    })
                    .catch(function() { return null; });
            }
        }
    } catch(e) {}
}

function decorateAppPageAlreadyOwned(pageAid) {
    if (!pageAid) return;

    var wishlistArea = document.getElementById('add_to_wishlist_area');
    if (wishlistArea && wishlistArea.style.display !== 'none') {
        wishlistArea.style.display = 'none';
    }

    var queueCtns = document.querySelectorAll('#queueCtn, .queue_ctn');
    for (var qi = 0; qi < queueCtns.length; qi++) {
        if (queueCtns[qi].classList && !queueCtns[qi].classList.contains('already_owned')) {
            queueCtns[qi].classList.add('already_owned');
        }
    }

    var queueAndPlaytime = document.querySelector('.queue_and_playtime');
    if (!queueAndPlaytime) return;

    var insertAnchor = queueAndPlaytime.querySelector('.queue_overflow_ctn');
    var purchaseOptionsContent = queueAndPlaytime.querySelector('#purchaseOptionsContent');

    function insertNativeNode(node) {
        if (insertAnchor && insertAnchor.parentNode === queueAndPlaytime) {
            queueAndPlaytime.insertBefore(node, insertAnchor.nextSibling);
            insertAnchor = node;
        } else if (purchaseOptionsContent && purchaseOptionsContent.parentNode === queueAndPlaytime) {
            queueAndPlaytime.insertBefore(node, purchaseOptionsContent);
        } else {
            queueAndPlaytime.appendChild(node);
        }
    }

    if (!queueAndPlaytime.querySelector('.game_area_already_owned.page_content')) {
        var appNameEl = document.getElementById('appHubAppName') || document.querySelector('.apphub_AppName');
        var appTitle = (appNameEl && appNameEl.textContent) ? appNameEl.textContent.trim() : '';
        if (!appTitle) appTitle = 'This title';

        var ownedDiv = document.createElement('div');
        ownedDiv.className = 'game_area_already_owned page_content';

        var ctnDiv = document.createElement('div');
        ctnDiv.className = 'game_area_already_owned_ctn';

        var flagDiv = document.createElement('div');
        flagDiv.className = 'ds_owned_flag ds_flag';
        flagDiv.innerHTML = 'IN LIBRARY&nbsp;&nbsp;';

        var textDiv = document.createElement('div');
        textDiv.className = 'already_in_library';
        textDiv.textContent = appTitle + ' is already in your Steam library';

        ctnDiv.appendChild(flagDiv);
        ctnDiv.appendChild(textDiv);
        ownedDiv.appendChild(ctnDiv);

        insertNativeNode(ownedDiv);
    }

    if (!queueAndPlaytime.querySelector('.game_area_play_stats')) {
        var playStatsDiv = document.createElement('div');
        playStatsDiv.className = 'game_area_play_stats';

        var panelDiv = document.createElement('div');
        panelDiv.setAttribute('data-panel', '{"flow-children":"row"}');

        var actionsDiv = document.createElement('div');
        actionsDiv.className = 'already_owned_actions';

        var btnDiv = document.createElement('div');
        btnDiv.className = 'game_area_already_owned_btn';

        var launchLink = document.createElement('a');
        launchLink.className = 'btnv6_lightblue_blue btnv6_border_2px btn_medium';
        launchLink.href = 'steam://launch/' + String(pageAid) + '/Dialog';

        var spanEl = document.createElement('span');
        spanEl.textContent = 'Play Now';

        launchLink.appendChild(spanEl);
        btnDiv.appendChild(launchLink);
        actionsDiv.appendChild(btnDiv);
        panelDiv.appendChild(actionsDiv);

        var clearDiv = document.createElement('div');
        clearDiv.style.clear = 'inline-start';

        playStatsDiv.appendChild(panelDiv);
        playStatsDiv.appendChild(clearDiv);

        insertNativeNode(playStatsDiv);
    }
}

function tagMissingDynamicStoreAttributes(targetAppIds, testMode) {
    var taggedAny = false;
    var linkCapsules = document.querySelectorAll('a.search_result_row, a.game_area_dlc_row, a.home_smallcap, a.match');
    var jq = window.$J || window.jQuery;
    for (var li = 0; li < linkCapsules.length; li++) {
        var lEl = linkCapsules[li];
        if (lEl.hasAttribute('data-ds-appid') || lEl.hasAttribute('data-ds-packageid') || lEl.hasAttribute('data-ds-bundleid')) {
            continue;
        }
        var lHref = lEl.getAttribute('href') || lEl.href || '';
        var lm = lHref.match(/\/app\/([0-9]+)/);
        if (lm && lm[1]) {
            var lAid = parseInt(lm[1], 10);
            if (lAid && (testMode || targetAppIds.has(lAid))) {
                lEl.setAttribute('data-ds-appid', String(lAid));
                if (typeof jq === 'function') {
                    try {
                        jq(lEl).data('dsAppid', String(lAid));
                    } catch(e) {}
                }
                taggedAny = true;
            }
        }
    }
    return taggedAny;
}

function hasUndecoratedOwnedElements(targetAppIds) {
    var dsEls = document.querySelectorAll('[data-ds-appid]');
    for (var i = 0; i < dsEls.length; i++) {
        var el = dsEls[i];
        if (!el.classList || el.classList.contains('ds_no_flags') || el.classList.contains('ds_owned')) {
            continue;
        }
        var rawAttr = el.getAttribute('data-ds-appid');
        if (!rawAttr) continue;
        var parts = String(rawAttr).split(',');
        var hasValid = false;
        var allOwned = true;
        for (var pi = 0; pi < parts.length; pi++) {
            var aid = parseInt(parts[pi], 10);
            if (!aid) continue;
            hasValid = true;
            if (!targetAppIds.has(aid) && !(window.GDynamicStore && GDynamicStore.s_rgOwnedApps && GDynamicStore.s_rgOwnedApps[aid])) {
                allOwned = false;
                break;
            }
        }
        if (hasValid && allOwned) return true;
    }
    return false;
}

function applyLuaLibraryOwnership() {
    var testMode = (typeof getIsTesting === 'function') ? getIsTesting() : (typeof isTesting !== 'undefined' ? isTesting : false);
    if (!testMode && (!slsLuaAppIdsLoaded || slsLuaAppIds.size === 0)) return;

    cleanupLegacyHiding();
    installDynamicStoreUserdataHook();

    var targetAppIds = collectTargetAppIds(testMode);

    // 1. Synchronize into GDynamicStore.s_rgOwnedApps & trigger native Steam decoration
    if (window.GDynamicStore && GDynamicStore.s_rgOwnedApps) {
        if (typeof GDynamicStore.OnReady === 'function' && !window.__slsDynamicStoreOnReadyHooked) {
            window.__slsDynamicStoreOnReadyHooked = true;
            GDynamicStore.OnReady(function() {
                applyLuaLibraryOwnership();
            });
        }

        var needsDecorate = false;
        targetAppIds.forEach(function(aid) {
            if (!GDynamicStore.s_rgOwnedApps[aid]) {
                GDynamicStore.s_rgOwnedApps[aid] = true;
                needsDecorate = true;
            }
        });

        if (tagMissingDynamicStoreAttributes(targetAppIds, testMode)) {
            needsDecorate = true;
        }

        if (!needsDecorate && hasUndecoratedOwnedElements(targetAppIds)) {
            needsDecorate = true;
        }

        if (needsDecorate && typeof GDynamicStore.DecorateDynamicItems === 'function') {
            if (typeof GDynamicStore.s_rgPersonalizedBundleData === 'object') {
                GDynamicStore.s_rgPersonalizedBundleData = {};
            }
            var jq = window.$J || window.jQuery;
            if (typeof jq === 'function') {
                try {
                    jq('[data-ds-appid], [data-ds-packageid], [data-ds-bundleid]').each(function() {
                        var $el = jq(this);
                        if (!$el.hasClass('ds_owned')) {
                            $el.children('.ds_options').remove();
                        }
                    });
                } catch(e) {}
            }
            GDynamicStore.DecorateDynamicItems(null, true);
        }
    }

    // 2. Synchronize into StoreItemCache if present (used by modern React content hubs)
    if (window.StoreItemCache) {
        syncStoreItemCacheOwnership(window.StoreItemCache, targetAppIds);
    }

    // 3. Synchronize into modern Vite / Webpack React Query & UserPreferences stores
    syncViteAndWebpackStore(targetAppIds);

    // 4. Native App Product Page (.queue_and_playtime -> .game_area_already_owned.page_content)
    var pageMatch = (typeof window !== 'undefined' && window.location && window.location.href)
        ? window.location.href.match(/\/app\/([0-9]+)/)
        : null;
    if (pageMatch && pageMatch[1]) {
        var pageAid = parseInt(pageMatch[1], 10);
        if (testMode || targetAppIds.has(pageAid)) {
            decorateAppPageAlreadyOwned(pageAid);
        }
    }
}

function initLuaLibraryOwnership() {
    cleanupLegacyHiding();
    installDynamicStoreUserdataHook();

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
        if (typeof GDynamicStore.OnReady === 'function' && !window.__slsDynamicStoreOnReadyHooked) {
            window.__slsDynamicStoreOnReadyHooked = true;
            GDynamicStore.OnReady(function() {
                applyLuaLibraryOwnership();
            });
        }
    }
}

