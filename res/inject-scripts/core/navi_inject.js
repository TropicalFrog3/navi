/**
 * @file  navi_inject.js
 * @brief Primary CEF JavaScript injection bootstrap and event coordinator
 */
/**
 * Navi CEF Injection Script
 * 
 * This script monkey-patches SteamClient.Apps.RegisterForAppDetails
 * to inject correct disk space information for Navi-unlocked games
 * that would otherwise show "0 B" in the install dialog.
 *
 * The C++ backend returns lDiskSpaceRequiredBytes=0 for unlocked games
 * because it has no depot metadata. This script intercepts the callback
 * and fetches the real size from the Steam API.
 */
(function() {
    'use strict';

    // Guard against double-injection
    if (window.__slsInjected) return;
    window.__slsInjected = true;

    // -------------------------------------------------------------------------
    // Navi Cloud Manager Integration (Phase 5)
    // -------------------------------------------------------------------------
    function setupCloudManager() {
        if (window.NaviCloudManager && typeof window.NaviCloudManager.init === 'function') {
            window.NaviCloudManager.init();
            return;
        }

        var BACKEND_ORIGIN = 'http://127.0.0.1:9001';
        var POLL_INTERVAL_MS = 5000;
        var pollTimer = null;
        var state = {
            enabled: false,
            provider: 'local-only',
            managedAppsCount: 0,
            pendingOpsCount: 0,
            hasPending: false,
            operations: []
        };

        function isOriginCompliant() {
            try {
                var origin = window.location.origin;
                if (!origin || origin === 'null') {
                    var href = window.location.href || '';
                    if (href.indexOf('steamloopback.host') !== -1 ||
                        href.indexOf('steampowered.com') !== -1 ||
                        href.indexOf('chrome-extension:') === 0 ||
                        href.indexOf('devtools:') === 0 ||
                        href.indexOf('file:') === 0) {
                        return true;
                    }
                }
                var host = window.location.hostname || '';
                if (host === 'steamloopback.host' ||
                    host === '127.0.0.1' ||
                    host === 'localhost' ||
                    host === 'store.steampowered.com' ||
                    host === 'steamcommunity.com' ||
                    host.endsWith('.steampowered.com')) {
                    return true;
                }
            } catch (e) {
                return false;
            }
            return false;
        }

        function validateTargetUrl(pathOrUrl) {
            if (typeof pathOrUrl !== 'string' || !pathOrUrl) return null;
            var parsed;
            try {
                parsed = new URL(pathOrUrl, BACKEND_ORIGIN);
            } catch (e) {
                return null;
            }
            if ((parsed.hostname !== '127.0.0.1' && parsed.hostname !== 'localhost') ||
                parsed.port !== '9001') {
                return null;
            }
            return parsed.href;
        }

        function secureFetch(pathOrUrl, options) {
            if (!isOriginCompliant()) {
                return Promise.reject(new Error('Security check failed: untrusted origin ' + window.location.origin));
            }
            var targetUrl = validateTargetUrl(pathOrUrl);
            if (!targetUrl) {
                return Promise.reject(new Error('Security check failed: non-loopback backend target'));
            }

            var opts = Object.assign({}, options || {});
            var headers = Object.assign({}, opts.headers || {});
            headers['X-Navi-Auth'] = window.__naviAuthToken || '';
            if (opts.body && typeof opts.body === 'string' && !headers['Content-Type']) {
                headers['Content-Type'] = 'application/json';
            }
            opts.headers = headers;

            if (typeof window.naviFetch === 'function' && window.location.hostname.indexOf('steampowered.com') !== -1) {
                return window.naviFetch(targetUrl, opts);
            }

            return fetch(targetUrl, opts);
        }

        function updatePendingBanner(hasPending) {
            var existingBanner = document.getElementById('navi-cloud-pending-banner');
            if (!hasPending) {
                if (existingBanner) existingBanner.remove();
                return;
            }
            if (existingBanner) return;

            if (!document.body) {
                setTimeout(function() { updatePendingBanner(hasPending); }, 250);
                return;
            }

            var banner = document.createElement('div');
            banner.id = 'navi-cloud-pending-banner';
            banner.style.cssText = 'position: fixed; top: 0; left: 0; right: 0; z-index: 999998; ' +
                'background: linear-gradient(90deg, #78350f, #92400e); border-bottom: 2px solid #f59e0b; ' +
                'color: #fef3c7; padding: 10px 18px; display: flex; align-items: center; justify-content: space-between; ' +
                'font-family: "Motiva Sans", -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; ' +
                'box-shadow: 0 4px 14px rgba(0,0,0,0.6); box-sizing: border-box;';

            var textContainer = document.createElement('div');
            textContainer.style.cssText = 'display: flex; align-items: center; gap: 10px; font-size: 14px; font-weight: 500;';

            var icon = document.createElement('span');
            icon.style.cssText = 'font-size: 18px; color: #fbbf24; flex-shrink: 0;';
            icon.innerText = '⚠️';

            var text = document.createElement('span');
            text.innerText = 'Pending cloud save operation waiting for Steam restart. Please restart Steam via Navi to safely apply changes.';

            textContainer.appendChild(icon);
            textContainer.appendChild(text);

            var restartBtn = document.createElement('button');
            restartBtn.id = 'navi-banner-restart-btn';
            restartBtn.className = 'Focusable';
            restartBtn.setAttribute('tabindex', '-1');
            restartBtn.setAttribute('role', 'button');
            restartBtn.style.cssText = 'background: #f59e0b; color: #1e1e1e; border: none; border-radius: 3px; ' +
                'padding: 6px 16px; font-size: 13px; font-weight: 700; font-family: "Motiva Sans", sans-serif; ' +
                'cursor: pointer; white-space: nowrap; transition: background 0.15s ease; flex-shrink: 0; margin-left: 12px;';
            restartBtn.innerText = 'Restart Steam Now';

            restartBtn.addEventListener('mouseenter', function() { restartBtn.style.background = '#fbbf24'; });
            restartBtn.addEventListener('mouseleave', function() { restartBtn.style.background = '#f59e0b'; });
            restartBtn.onclick = function(e) {
                e.preventDefault();
                e.stopPropagation();
                restartBtn.innerText = 'Restarting...';
                restartBtn.disabled = true;
                restartBtn.style.opacity = '0.7';
                restartBtn.style.cursor = 'default';
                secureFetch('/restart', { method: 'POST', mode: 'no-cors' }).catch(function(err) {
                    console.warn('[NaviCloudManager] Restart error:', err);
                });
            };

            banner.appendChild(textContainer);
            banner.appendChild(restartBtn);
            document.body.appendChild(banner);
        }

        function fetchStatus() {
            return secureFetch('/cloud/status')
                .then(function(res) { return res.json(); })
                .then(function(data) {
                    state.enabled = !!data.enabled;
                    state.provider = data.provider || 'local-only';
                    state.managedAppsCount = data.managedAppsCount || 0;
                    state.pendingOpsCount = data.pendingOpsCount || 0;
                    return data;
                });
        }

        function fetchPendingOps() {
            return secureFetch('/cloud/pending-ops')
                .then(function(res) { return res.json(); })
                .then(function(data) {
                    state.operations = data.operations || [];
                    var hasPending = !!data.hasPending;
                    if (!hasPending && state.operations.length > 0) {
                        for (var i = 0; i < state.operations.length; i++) {
                            var st = String(state.operations[i].status || '').toUpperCase();
                            if (st === 'PENDING') {
                                hasPending = true;
                                break;
                            }
                        }
                    }
                    state.hasPending = hasPending;
                    updatePendingBanner(hasPending || state.pendingOpsCount > 0);
                    return data;
                });
        }

        function toggleMaster(enable) {
            var targetState = !!enable;
            var msg = targetState
                ? 'Enabling Cloud Saves requires a Steam restart to mount virtual save filesystems. Continue?'
                : 'Disabling Cloud Saves requires a Steam restart to detach virtual save filesystems. Continue?';
            var confirmed = window.confirm(msg);
            if (!confirmed) {
                return Promise.reject(new Error('User cancelled Cloud Saves toggle'));
            }

            return secureFetch('/cloud/toggle', {
                method: 'POST',
                body: JSON.stringify({ enable: targetState })
            })
                .then(function(res) { return res.json(); })
                .then(function(data) {
                    state.enabled = targetState;
                    poll();
                    return data;
                });
        }

        function fetchProviderStatus(provider) {
            var p = provider || state.provider || 'local-only';
            return secureFetch('/cloud/provider/status?provider=' + encodeURIComponent(p))
                .then(function(res) { return res.json(); });
        }

        function signInProvider(provider) {
            var p = provider || state.provider || 'gdrive';
            return secureFetch('/cloud/provider/signin', {
                method: 'POST',
                body: JSON.stringify({ provider: p })
            })
                .then(function(res) { return res.json(); })
                .then(function(data) {
                    if (data.authUrl) {
                        window.open(data.authUrl, '_blank');
                    }
                    return data;
                });
        }

        function signOutProvider(provider) {
            var p = provider || state.provider || 'gdrive';
            return secureFetch('/cloud/provider/signout', {
                method: 'POST',
                body: JSON.stringify({ provider: p })
            })
                .then(function(res) { return res.json(); });
        }

        function restartSteam() {
            return secureFetch('/restart', { method: 'POST', mode: 'no-cors' });
        }

        function fetchAppStatus(appId) {
            return secureFetch('/cloud/apps/' + appId + '/status')
                .then(function(res) { return res.json(); });
        }

        function toggleAppRedirect(appId, redirect) {
            return secureFetch('/cloud/apps/' + appId + '/toggle', {
                method: 'POST',
                body: JSON.stringify({ redirect: !!redirect })
            })
                .then(function(res) { return res.json(); });
        }

        function backupAppSave(appId) {
            return secureFetch('/cloud/apps/' + appId + '/backup', {
                method: 'POST'
            })
                .then(function(res) { return res.json(); });
        }

        function restoreAppSave(appId, backupFile) {
            return secureFetch('/cloud/apps/' + appId + '/restore', {
                method: 'POST',
                body: JSON.stringify({ backupFile: backupFile })
            })
                .then(function(res) { return res.json(); })
                .then(function(data) {
                    poll();
                    return data;
                });
        }

        function executeResetAppSave(appId) {
            return secureFetch('/cloud/apps/' + appId + '/reset', {
                method: 'POST',
                body: JSON.stringify({ confirm: 'RESET' })
            })
                .then(function(res) { return res.json(); })
                .then(function(data) {
                    poll();
                    return data;
                });
        }

        function resetAppSave(appId, onCompleted) {
            var confirmed = window.prompt('Type RESET to confirm cloud save reset for AppID ' + appId + ':');
            if (confirmed === 'RESET') {
                return executeResetAppSave(appId).then(function() {
                    alert('Cloud save reset enqueued. Please restart Steam to complete.');
                    if (onCompleted) onCompleted(true);
                });
            }
            return Promise.resolve(false);
        }

        function poll() {
            return Promise.all([
                fetchStatus().catch(function() {}),
                fetchPendingOps().catch(function() {})
            ]);
        }

        function startPolling(intervalMs) {
            stopPolling();
            poll();
            pollTimer = setInterval(poll, intervalMs || POLL_INTERVAL_MS);
        }

        function stopPolling() {
            if (pollTimer) {
                clearInterval(pollTimer);
                pollTimer = null;
            }
        }

        function init() {
            startPolling(POLL_INTERVAL_MS);
        }

        window.NaviCloudManager = {
            fetchStatus: fetchStatus,
            fetchPendingOps: fetchPendingOps,
            toggleMaster: toggleMaster,
            fetchProviderStatus: fetchProviderStatus,
            signInProvider: signInProvider,
            signOutProvider: signOutProvider,
            restartSteam: restartSteam,
            fetchAppStatus: fetchAppStatus,
            toggleAppRedirect: toggleAppRedirect,
            backupAppSave: backupAppSave,
            restoreAppSave: restoreAppSave,
            resetAppSave: resetAppSave,
            executeResetAppSave: executeResetAppSave,
            openResetModal: resetAppSave,
            updatePendingBanner: updatePendingBanner,
            secureFetch: secureFetch,
            startPolling: startPolling,
            stopPolling: stopPolling,
            getState: function() { return Object.assign({}, state); },
            init: init
        };

        if (document.readyState === 'loading') {
            document.addEventListener('DOMContentLoaded', init);
        } else {
            init();
        }
    }

    setupCloudManager();

    // Size cache: appId -> size in bytes
    var sizeCache = {};
    // Pending fetches: appId -> [callbacks]
    var pendingFetches = {};

    /**
     * Fetch the real install size for an app from the Steam CDN metadata.
     * Uses the steamcmd.net API as a reliable public source for depot sizes.
     */
    function fetchAppSize(appId) {
        if (sizeCache[appId] !== undefined) {
            return Promise.resolve(sizeCache[appId]);
        }
        if (pendingFetches[appId]) {
            return new Promise(function(resolve) {
                pendingFetches[appId].push(resolve);
            });
        }
        pendingFetches[appId] = [];

        return new Promise(function(resolve) {
            var xhr = new XMLHttpRequest();
            xhr.open('GET', 'https://api.steamcmd.net/v1/info/' + appId, true);
            xhr.timeout = 8000;
            xhr.onload = function() {
                var totalSize = 0;
                try {
                    var data = JSON.parse(xhr.responseText);
                    var appData = data.data && data.data[appId];
                    if (appData && appData.depots) {
                        var depots = appData.depots;
                        for (var depotId in depots) {
                            if (depots.hasOwnProperty(depotId)) {
                                var depot = depots[depotId];
                                if (depot && depot.manifests && depot.manifests.public) {
                                    var size = parseInt(depot.manifests.public.size || '0', 10);
                                    if (!isNaN(size)) totalSize += size;
                                }
                            }
                        }
                    }
                } catch (e) {
                    console.warn('[SLS] Failed to parse size for app ' + appId + ':', e);
                }
                sizeCache[appId] = totalSize;
                resolve(totalSize);
                // Resolve pending callbacks
                if (pendingFetches[appId]) {
                    pendingFetches[appId].forEach(function(cb) { cb(totalSize); });
                    delete pendingFetches[appId];
                }
            };
            xhr.onerror = xhr.ontimeout = function() {
                console.warn('[SLS] Network error fetching size for app ' + appId);
                sizeCache[appId] = 0;
                resolve(0);
                if (pendingFetches[appId]) {
                    pendingFetches[appId].forEach(function(cb) { cb(0); });
                    delete pendingFetches[appId];
                }
            };
            xhr.send();
        });
    }

    /**
     * Read the Navi config to get the list of unlocked app IDs.
     * Falls back to detecting apps with lDiskSpaceRequiredBytes==0 at runtime.
     */
    function getUnlockedAppIds() {
        // The Navi config is at ~/.local/share/navi/path/config.json
        // But we can't read files from JS in CEF. Instead, we rely on
        // runtime detection: if an app is "subscribed" but has 0 disk space
        // and 0 build ID, it's likely Navi-unlocked.
        return null; // Will use runtime detection
    }

    /**
     * Check if an app details object looks like a Navi-unlocked game
     * that needs size injection.
     */
    function needsSizeInjection(details) {
        return details &&
            details.lDiskSpaceRequiredBytes === 0 &&
            details.nBuildID === 0 &&
            details.bIsSubscribedTo === true &&
            !details.bHasAnyLocalContent;
    }

    // Wait for SteamClient to be available
    function waitForSteamClient(callback) {
        if (typeof SteamClient !== 'undefined' && SteamClient.Apps &&
            typeof SteamClient.Apps.RegisterForAppDetails === 'function') {
            callback();
            return;
        }
        var attempts = 0;
        var interval = setInterval(function() {
            attempts++;
            if (typeof SteamClient !== 'undefined' && SteamClient.Apps &&
                typeof SteamClient.Apps.RegisterForAppDetails === 'function') {
                clearInterval(interval);
                callback();
            } else if (attempts > 100) {
                clearInterval(interval);
                console.warn('[SLS] Timed out waiting for SteamClient');
            }
        }, 100);
    }

    waitForSteamClient(function() {
        console.log('[SLS] Patching SteamClient.Apps.RegisterForAppDetails');

        var originalRegister = SteamClient.Apps.RegisterForAppDetails;

        SteamClient.Apps.RegisterForAppDetails = function(appId, callback) {
            var wrappedCallback = function(details) {
                if (needsSizeInjection(details)) {
                    // Async fetch size, but apply immediately if cached
                    if (sizeCache[details.unAppID] > 0) {
                        details.lDiskSpaceRequiredBytes = sizeCache[details.unAppID];
                        console.log('[SLS] Injected cached size for app ' + details.unAppID + ': ' + details.lDiskSpaceRequiredBytes);
                    } else {
                        // Start fetch, will apply on next callback
                        fetchAppSize(details.unAppID).then(function(size) {
                            if (size > 0 && typeof appDetailsStore !== 'undefined') {
                                // Update the cached details object directly
                                var appData = appDetailsStore.GetAppData(details.unAppID);
                                if (appData && appData.details) {
                                    appData.details.lDiskSpaceRequiredBytes = size;
                                    console.log('[SLS] Async injected size for app ' + details.unAppID + ': ' + size);
                                    // Notify listeners to refresh UI
                                    if (appData.listeners) {
                                        appData.listeners.forEach(function(listener) {
                                            try { listener(appData.details); } catch(e) {}
                                        });
                                    }
                                }
                            }
                        });
                    }
                }
                return callback(details);
            };
            return originalRegister.call(this, appId, wrappedCallback);
        };

        // Also patch any already-registered apps
        if (typeof appDetailsStore !== 'undefined') {
            var mapAppData = appDetailsStore.m_mapAppData;
            if (mapAppData) {
                mapAppData.forEach(function(data, appId) {
                    if (data.details && needsSizeInjection(data.details)) {
                        fetchAppSize(appId).then(function(size) {
                            if (size > 0 && data.details) {
                                data.details.lDiskSpaceRequiredBytes = size;
                                console.log('[SLS] Patched existing details for app ' + appId + ': ' + size);
                                if (data.listeners) {
                                    data.listeners.forEach(function(listener) {
                                        try { listener(data.details); } catch(e) {}
                                    });
                                }
                            }
                        });
                    }
                });
            }
        }

        console.log('[SLS] SteamClient.Apps.RegisterForAppDetails patched successfully');
    });
})();


