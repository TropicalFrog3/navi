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

    /* ==========================================================================
       Navi Cross-Device Sync UI Module (Launch + Wake-from-Sleep + CEF Modal)
       ========================================================================== */

    function fetchSyncStatus(callback) {
        var xhr = new XMLHttpRequest();
        xhr.open('GET', 'http://127.0.0.1:9001/sync/status', true);
        xhr.timeout = 5000;
        xhr.onload = function() {
            if (xhr.status === 200) {
                try {
                    var data = JSON.parse(xhr.responseText);
                    callback(null, data);
                } catch (e) {
                    callback(e, null);
                }
            } else {
                callback(new Error('Status ' + xhr.status), null);
            }
        };
        xhr.onerror = xhr.ontimeout = function() {
            callback(new Error('Network error or server unreachable'), null);
        };
        xhr.send();
    }

    function escapeHtml(str) {
        return String(str == null ? '' : str)
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;')
            .replace(/'/g, '&#39;');
    }

    var activeModalCleanup = null;

    function renderSyncModal(statusData) {
        if (typeof activeModalCleanup === 'function') {
            activeModalCleanup();
        }
        var existingOverlay = document.getElementById('navi-sync-modal-overlay');
        if (existingOverlay) {
            existingOverlay.remove();
        }

        var overlay = document.createElement('div');
        overlay.id = 'navi-sync-modal-overlay';
        overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(10,12,18,0.88);z-index:2147483647;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;color:#f5f6f8;';

        var triggerLabel = 'Launch';
        if (statusData.lastTriggerReason === 'wake_from_sleep') triggerLabel = 'Wake from Sleep';
        else if (statusData.lastTriggerReason === 'p2p') triggerLabel = 'P2P';
        else if (statusData.lastTriggerReason === 'manual') triggerLabel = 'Manual Check';

        var overriderHost = statusData.overriderHostname || 'Overrider';
        var remoteGen = statusData.remoteGeneration || 0;
        var role = statusData.role || 'disabled';
        var isOverrider = (role === 'overrider');
        var recKey = statusData.recoveryKey || statusData.recoveryKeyMaskedOrFull || '';

        var cloudProv = statusData.cloudProvider || 'none';
        var isAuthed = !!statusData.cloudAuthenticated;
        var provLabel = cloudProv === 'gdrive' ? (isAuthed ? 'GDrive: Authed' : 'GDrive: Auth Needed') : (cloudProv === 'webdav' ? 'WebDAV' : 'Local P2P');
        var provColor = (cloudProv === 'gdrive' && !isAuthed) ? '#f59e0b' : '#34d399';
        var provBg = (cloudProv === 'gdrive' && !isAuthed) ? 'rgba(245,158,11,0.18)' : 'rgba(16,185,129,0.18)';

        var html = '<div id="navi-sync-modal-card" style="background:linear-gradient(145deg, #181c24 0%, #0d0f14 100%);border:1px solid rgba(255,255,255,0.12);border-radius:16px;padding:24px;width:640px;max-width:92vw;max-height:86vh;display:flex;flex-direction:column;box-shadow:0 24px 60px rgba(0,0,0,0.7);">' +
            '<!-- Header -->' +
            '<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:16px;flex-shrink:0;">' +
                '<div>' +
                    '<h2 style="margin:0;font-size:20px;font-weight:700;background:linear-gradient(90deg, #fff 0%, #a5aab6 100%);-webkit-background-clip:text;-webkit-text-fill-color:transparent;">Navi Cross-Device Sync</h2>' +
                    '<div style="margin-top:6px;font-size:12px;color:#9ca3af;display:flex;align-items:center;flex-wrap:wrap;gap:6px;">' +
                        '<span>Host: <strong style="color:#60a5fa;">' + escapeHtml(overriderHost) + '</strong></span>' +
                        '<span style="background:rgba(96,165,250,0.18);color:#93c5fd;padding:2px 8px;border-radius:9999px;font-size:11px;">' + escapeHtml(triggerLabel) + '</span>' +
                        '<span style="background:rgba(16,185,129,0.18);color:#34d399;padding:2px 8px;border-radius:9999px;font-size:11px;">Gen #' + remoteGen + '</span>' +
                        '<span style="background:rgba(59,130,246,0.18);color:#60a5fa;padding:2px 8px;border-radius:9999px;font-size:11px;text-transform:capitalize;">Role: ' + escapeHtml(role) + '</span>' +
                        '<span style="background:' + provBg + ';color:' + provColor + ';padding:2px 8px;border-radius:9999px;font-size:11px;">' + escapeHtml(provLabel) + '</span>' +
                    '</div>' +
                '</div>' +
                '<button id="navi-sync-close-x" style="background:rgba(255,255,255,0.06);border:none;color:#9ca3af;font-size:20px;cursor:pointer;width:36px;height:36px;border-radius:50%;display:flex;align-items:center;justify-content:center;">&times;</button>' +
            '</div>' +

            '<!-- Tab Navigation -->' +
            '<div style="display:flex;border-bottom:1px solid rgba(255,255,255,0.1);margin-bottom:16px;flex-shrink:0;">' +
                '<button id="navi-tab-items-btn" style="background:none;border:none;border-bottom:2px solid #3b82f6;color:#60a5fa;padding:10px 18px;font-size:14px;font-weight:600;cursor:pointer;min-height:40px;">Sync Items &amp; Review</button>' +
                '<button id="navi-tab-settings-btn" style="background:none;border:none;border-bottom:2px solid transparent;color:#9ca3af;padding:10px 18px;font-size:14px;font-weight:600;cursor:pointer;min-height:40px;">Cloud &amp; Pairing Settings</button>' +
            '</div>' +

            '<!-- Tab Content Area -->' +
            '<div id="navi-sync-scroll-body" style="flex-grow:1;overflow-y:auto;padding-right:6px;margin-bottom:16px;">' +
                '<!-- TAB 1: Items -->' +
                '<div id="navi-tab-items-content">' +
                    '<!-- Category 1: Manifests & Lua -->' +
                    '<div style="background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.06);border-radius:8px;padding:12px;margin-bottom:10px;">' +
                        '<label style="display:flex;align-items:center;cursor:pointer;font-weight:600;font-size:14px;">' +
                            '<input type="checkbox" id="navi-sync-all-manifests" checked style="margin-right:10px;accent-color:#3b82f6;width:16px;height:16px;">' +
                            'Manifests &amp; Lua Plugins' +
                        '</label>' +
                        '<div id="navi-manifests-sublist" style="margin-top:8px;padding-left:26px;font-size:12px;color:#9ca3af;">' +
                            'Includes app manifests and decrypted Lua game metadata.' +
                        '</div>' +
                    '</div>' +

                    '<!-- Category 2: Configurations -->' +
                    '<div style="background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.06);border-radius:8px;padding:12px;margin-bottom:10px;">' +
                        '<label style="display:flex;align-items:center;cursor:pointer;font-weight:600;font-size:14px;">' +
                            '<input type="checkbox" id="navi-sync-all-configs" checked style="margin-right:10px;accent-color:#3b82f6;width:16px;height:16px;">' +
                            'Configurations' +
                        '</label>' +
                        '<div style="margin-top:8px;padding-left:26px;font-size:12px;color:#9ca3af;">' +
                            'Sync unlocked game lists, DLC configurations, and client preferences.' +
                        '</div>' +
                    '</div>' +

                    '<!-- Category 3: Steam Cloud Saves -->' +
                    '<div style="background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.06);border-radius:8px;padding:12px;margin-bottom:10px;">' +
                        '<label style="display:flex;align-items:center;cursor:pointer;font-weight:600;font-size:14px;">' +
                            '<input type="checkbox" id="navi-sync-all-saves" checked style="margin-right:10px;accent-color:#3b82f6;width:16px;height:16px;">' +
                            'Steam Cloud Saves' +
                        '</label>' +
                        '<div id="navi-saves-sublist" style="margin-top:8px;padding-left:26px;font-size:12px;color:#9ca3af;">';

        // Add diff items and conflict resolution rows for Cloud Saves
        var hasSavesDiff = false;
        if (statusData.diffItems && statusData.diffItems.length > 0) {
            for (var i = 0; i < statusData.diffItems.length; ++i) {
                var it = statusData.diffItems[i];
                if (it.category === 'cloud_saves' || it.category === 'cloud_save') {
                    hasSavesDiff = true;
                    html += '<div style="display:flex;align-items:center;justify-content:space-between;padding:4px 0;">' +
                        '<span>App ' + escapeHtml(it.appId) + ' (' + escapeHtml(it.title || it.logicalPath) + ')</span>' +
                        '<div>';
                    if (it.isGameRunning) {
                        html += '<span style="background:#ef4444;color:#fff;font-size:10px;padding:2px 6px;border-radius:4px;margin-right:6px;">Game Running — Deferred until exit</span>';
                    }
                    html += '</div></div>';
                }
            }
        }

        // Check for conflicts
        if (statusData.conflicts && statusData.conflicts.length > 0) {
            html += '<div style="margin-top:6px;border-top:1px solid rgba(255,255,255,0.08);padding-top:6px;">' +
                '<strong style="color:#f59e0b;font-size:12px;">Save Conflicts Detected:</strong>';
            for (var c = 0; c < statusData.conflicts.length; ++c) {
                var cf = statusData.conflicts[c];
                html += '<div style="display:flex;align-items:center;justify-content:space-between;margin-top:4px;">' +
                    '<span style="font-family:monospace;font-size:11px;color:#d1d5db;">' + escapeHtml(cf.logicalPath) + '</span>' +
                    '<select class="navi-conflict-choice" data-path="' + escapeHtml(cf.logicalPath) + '" style="background:#202530;color:#fff;border:1px solid #3b82f6;border-radius:4px;padding:2px 6px;font-size:11px;">' +
                        '<option value="keep_remote" selected>Keep Remote</option>' +
                        '<option value="keep_local">Keep Local</option>' +
                        '<option value="backup_both">Backup Both</option>' +
                    '</select>' +
                '</div>';
            }
            html += '</div>';
        } else if (!hasSavesDiff) {
            html += 'No pending save conflicts. All saves are up to date.';
        }

        html +=         '</div>' +
                    '</div>' +

                    '<!-- Category 4: Achievements & Stats -->' +
                    '<div style="background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.06);border-radius:8px;padding:12px;margin-bottom:10px;">' +
                        '<label style="display:flex;align-items:center;cursor:pointer;font-weight:600;font-size:14px;">' +
                            '<input type="checkbox" id="navi-sync-all-achievements" checked style="margin-right:10px;accent-color:#3b82f6;width:16px;height:16px;">' +
                            'Achievements &amp; Stats' +
                            '<span style="background:rgba(139,92,246,0.2);color:#c4b5fd;font-size:11px;padding:2px 8px;border-radius:4px;margin-left:8px;">Safe Set Union (A ∪ B)</span>' +
                        '</label>' +
                        '<div style="margin-top:8px;padding-left:26px;font-size:12px;color:#9ca3af;">' +
                            'Unlocks achieved on either device are merged safely without losing progress.' +
                        '</div>' +
                    '</div>' +

                    '<!-- Category 5: Provider API Keys -->' +
                    '<div style="background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.06);border-radius:8px;padding:12px;">' +
                        '<label style="display:flex;align-items:center;cursor:pointer;font-weight:600;font-size:14px;">' +
                            '<input type="checkbox" id="navi-sync-all-apikeys" checked style="margin-right:10px;accent-color:#3b82f6;width:16px;height:16px;">' +
                            'Provider API Keys' +
                            '<span style="background:rgba(16,185,129,0.2);color:#6ee7b7;font-size:11px;padding:2px 8px;border-radius:4px;margin-left:8px;">E2EE Encrypted</span>' +
                        '</label>' +
                        '<div style="margin-top:8px;padding-left:26px;font-size:12px;color:#9ca3af;">' +
                            'Synchronize Ryuu, DepotBox, and Hubcap API keys end-to-end encrypted with your master key.' +
                        '</div>' +
                    '</div>' +
                '</div>' +

                '<!-- TAB 2: Settings & Pairing -->' +
                '<div id="navi-tab-settings-content" style="display:none;">' +
                    '<div style="background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.06);border-radius:8px;padding:14px;margin-bottom:12px;">' +
                        '<label style="font-weight:600;font-size:13px;display:block;margin-bottom:6px;">Cloud Storage Provider</label>' +
                        '<select id="navi-sync-provider-select" style="background:#202530;color:#fff;border:1px solid rgba(255,255,255,0.2);border-radius:6px;padding:8px 12px;font-size:13px;width:100%;min-height:40px;">' +
                            '<option value="none"' + (statusData.cloudProvider === 'none' ? ' selected' : '') + '>Disabled / Local Only</option>' +
                            '<option value="gdrive"' + (statusData.cloudProvider === 'gdrive' ? ' selected' : '') + '>Google Drive (OAuth2)</option>' +
                            '<option value="webdav"' + (statusData.cloudProvider === 'webdav' ? ' selected' : '') + '>WebDAV / S3 Compatible</option>' +
                        '</select>' +
                    '</div>';

        if (isOverrider) {
            html += '<div style="background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.06);border-radius:8px;padding:14px;margin-bottom:12px;">' +
                '<label style="font-weight:600;font-size:13px;display:block;margin-bottom:4px;">Master Recovery Key (55-char Base32)</label>' +
                '<p style="font-size:11px;color:#9ca3af;margin:0 0 8px 0;">Use this key to set up receiver devices without local network access.</p>' +
                '<div style="display:flex;gap:8px;">' +
                    '<input type="text" id="navi-sync-recovery-key-val" readonly value="' + escapeHtml(recKey) + '" style="flex-grow:1;background:#14171e;border:1px solid rgba(255,255,255,0.15);border-radius:6px;padding:8px 12px;color:#38bdf8;font-family:monospace;font-size:12px;min-height:40px;">' +
                    '<button id="navi-sync-copy-key-btn" style="background:#2563eb;color:#fff;border:none;border-radius:6px;padding:8px 18px;font-size:12px;cursor:pointer;font-weight:600;min-height:40px;">Copy</button>' +
                '</div>' +
                '<div style="margin-top:12px;display:flex;align-items:center;gap:10px;">' +
                    '<button id="navi-sync-gen-pin-btn" style="background:#059669;color:#fff;border:none;border-radius:6px;padding:8px 18px;font-size:12px;cursor:pointer;font-weight:600;min-height:40px;">Generate P2P Pairing PIN</button>' +
                    '<span id="navi-sync-pin-output" style="font-size:16px;font-weight:700;letter-spacing:2px;color:#34d399;"></span>' +
                '</div>' +
            '</div>';
        } else {
            html += '<div style="background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.06);border-radius:8px;padding:14px;margin-bottom:12px;">' +
                '<label style="font-weight:600;font-size:13px;display:block;margin-bottom:4px;">Import Recovery Key (Cloud Setup)</label>' +
                '<div style="display:flex;gap:8px;margin-bottom:12px;">' +
                    '<input type="text" id="navi-sync-import-key-val" placeholder="XXXXX-XXXXX-XXXXX-XXXXX-..." style="flex-grow:1;background:#14171e;border:1px solid rgba(255,255,255,0.15);border-radius:6px;padding:8px 12px;color:#fff;font-family:monospace;font-size:12px;min-height:40px;">' +
                    '<button id="navi-sync-import-key-btn" style="background:#2563eb;color:#fff;border:none;border-radius:6px;padding:8px 18px;font-size:12px;cursor:pointer;font-weight:600;min-height:40px;">Import</button>' +
                '</div>' +
                '<label style="font-weight:600;font-size:13px;display:block;margin-bottom:4px;">Pair via Local P2P PIN</label>' +
                '<div style="display:flex;gap:8px;">' +
                    '<input type="text" id="navi-sync-pin-val" placeholder="6-digit PIN" maxlength="6" style="width:120px;background:#14171e;border:1px solid rgba(255,255,255,0.15);border-radius:6px;padding:8px 10px;color:#fff;font-family:monospace;font-size:12px;text-align:center;min-height:40px;">' +
                    '<button id="navi-sync-pair-pin-btn" style="background:#059669;color:#fff;border:none;border-radius:6px;padding:8px 18px;font-size:12px;cursor:pointer;font-weight:600;min-height:40px;">Pair Device</button>' +
                '</div>' +
            '</div>';
        }

        html +=         '<button id="navi-sync-save-settings-btn" style="background:#3b82f6;color:#fff;border:none;border-radius:6px;padding:10px 20px;font-size:13px;font-weight:600;cursor:pointer;min-height:40px;">Save Configuration</button>' +
                    '</div>' +
                '</div>' +

            '<!-- Status Feedback Box -->' +
            '<div id="navi-sync-status-box" role="status" aria-live="polite" style="display:none;padding:12px 14px;border-radius:8px;margin-bottom:12px;font-size:13px;font-weight:600;"></div>' +

            '<!-- Footer Actions -->' +
            '<div style="display:flex;justify-content:flex-end;align-items:center;gap:10px;flex-shrink:0;">' +
                '<button id="navi-sync-cancel-btn" style="background:rgba(255,255,255,0.08);border:none;color:#9ca3af;padding:10px 18px;border-radius:8px;font-size:13px;font-weight:600;cursor:pointer;min-height:40px;">Cancel</button>' +
                '<button id="navi-sync-push-btn" style="background:rgba(16,185,129,0.2);border:1px solid #10b981;color:#34d399;padding:10px 18px;border-radius:8px;font-size:13px;font-weight:600;cursor:pointer;min-height:40px;">Push to Cloud Now</button>' +
                '<button id="navi-sync-apply-btn" style="background:#2563eb;border:none;color:#fff;padding:10px 22px;border-radius:8px;font-size:13px;font-weight:600;cursor:pointer;min-height:40px;">Apply Sync Changes</button>' +
                '<button id="navi-sync-restart-btn" style="display:none;background:#eab308;border:none;color:#000;padding:10px 20px;border-radius:8px;font-size:13px;font-weight:700;cursor:pointer;min-height:40px;">Restart Steam Now</button>' +
            '</div>' +
        '</div>';

        overlay.innerHTML = html;
        document.body.appendChild(overlay);

        // Tab Switching
        var tabItemsBtn = document.getElementById('navi-tab-items-btn');
        var tabSettingsBtn = document.getElementById('navi-tab-settings-btn');
        var tabItemsContent = document.getElementById('navi-tab-items-content');
        var tabSettingsContent = document.getElementById('navi-tab-settings-content');

        tabItemsBtn.onclick = function() {
            tabItemsBtn.style.borderBottomColor = '#3b82f6';
            tabItemsBtn.style.color = '#60a5fa';
            tabSettingsBtn.style.borderBottomColor = 'transparent';
            tabSettingsBtn.style.color = '#9ca3af';
            tabItemsContent.style.display = 'block';
            tabSettingsContent.style.display = 'none';
        };

        tabSettingsBtn.onclick = function() {
            tabSettingsBtn.style.borderBottomColor = '#3b82f6';
            tabSettingsBtn.style.color = '#60a5fa';
            tabItemsBtn.style.borderBottomColor = 'transparent';
            tabItemsBtn.style.color = '#9ca3af';
            tabSettingsContent.style.display = 'block';
            tabItemsContent.style.display = 'none';
        };

        // Close logic & Escape shortcut
        function closeModal() {
            activeModalCleanup = null;
            overlay.remove();
            cleanupGamepad();
            window.removeEventListener('keydown', onModalKeyDown);
        }
        function onModalKeyDown(e) {
            if (e.key === 'Escape') {
                closeModal();
            }
        }
        window.addEventListener('keydown', onModalKeyDown);
        activeModalCleanup = closeModal;

        document.getElementById('navi-sync-close-x').onclick = closeModal;
        document.getElementById('navi-sync-cancel-btn').onclick = closeModal;

        // Copy Recovery Key button
        var copyBtn = document.getElementById('navi-sync-copy-key-btn');
        if (copyBtn) {
            copyBtn.onclick = function() {
                var input = document.getElementById('navi-sync-recovery-key-val');
                if (input) {
                    input.select();
                    document.execCommand('copy');
                    copyBtn.textContent = 'Copied!';
                    setTimeout(function() { copyBtn.textContent = 'Copy'; }, 2000);
                }
            };
        }

        // Generate P2P PIN button
        var genPinBtn = document.getElementById('navi-sync-gen-pin-btn');
        if (genPinBtn) {
            genPinBtn.onclick = function() {
                genPinBtn.disabled = true;
                genPinBtn.textContent = 'Generating...';
                var xhr = new XMLHttpRequest();
                xhr.open('POST', 'http://127.0.0.1:9001/sync/p2p/pair', true);
                xhr.onload = function() {
                    genPinBtn.disabled = false;
                    genPinBtn.textContent = 'Generate P2P Pairing PIN';
                    try {
                        var res = JSON.parse(xhr.responseText);
                        if (res.pin) {
                            document.getElementById('navi-sync-pin-output').textContent = res.pin;
                        }
                    } catch (e) {}
                };
                xhr.onerror = function() {
                    genPinBtn.disabled = false;
                    genPinBtn.textContent = 'Generate P2P Pairing PIN';
                };
                xhr.send(JSON.stringify({ action: 'generate_pin' }));
            };
        }

        // Import Recovery Key button
        var importKeyBtn = document.getElementById('navi-sync-import-key-btn');
        if (importKeyBtn) {
            importKeyBtn.onclick = function() {
                var keyVal = document.getElementById('navi-sync-import-key-val').value.trim();
                var statusBox = document.getElementById('navi-sync-status-box');
                importKeyBtn.disabled = true;
                importKeyBtn.textContent = 'Importing...';
                var xhr = new XMLHttpRequest();
                xhr.open('POST', 'http://127.0.0.1:9001/sync/recovery-key', true);
                xhr.onload = function() {
                    importKeyBtn.disabled = false;
                    importKeyBtn.textContent = 'Import';
                    statusBox.style.display = 'block';
                    if (xhr.status === 200) {
                        statusBox.style.background = 'rgba(16,185,129,0.2)';
                        statusBox.style.color = '#34d399';
                        statusBox.textContent = 'Recovery Key validated and saved successfully!';
                    } else {
                        statusBox.style.background = 'rgba(239,68,68,0.2)';
                        statusBox.style.color = '#f87171';
                        statusBox.textContent = 'Key validation failed: CRC-16 mismatch or malformed key.';
                    }
                };
                xhr.onerror = function() {
                    importKeyBtn.disabled = false;
                    importKeyBtn.textContent = 'Import';
                    statusBox.style.display = 'block';
                    statusBox.style.background = 'rgba(239,68,68,0.2)';
                    statusBox.style.color = '#f87171';
                    statusBox.textContent = 'Network error importing recovery key.';
                };
                xhr.send(JSON.stringify({ recoveryKey: keyVal }));
            };
        }

        // Pair via Local P2P PIN button
        var pairPinBtn = document.getElementById('navi-sync-pair-pin-btn');
        if (pairPinBtn) {
            pairPinBtn.onclick = function() {
                var pinInput = document.getElementById('navi-sync-pin-val');
                var pinVal = pinInput ? pinInput.value.trim() : '';
                var statusBox = document.getElementById('navi-sync-status-box');
                if (!pinVal || pinVal.length !== 6) {
                    statusBox.style.display = 'block';
                    statusBox.style.background = 'rgba(239,68,68,0.2)';
                    statusBox.style.color = '#f87171';
                    statusBox.textContent = 'Please enter a valid 6-digit PIN.';
                    return;
                }
                pairPinBtn.disabled = true;
                pairPinBtn.textContent = 'Pairing...';
                var xhr = new XMLHttpRequest();
                xhr.open('POST', 'http://127.0.0.1:9001/sync/p2p/pair', true);
                xhr.onload = function() {
                    pairPinBtn.disabled = false;
                    pairPinBtn.textContent = 'Pair Device';
                    statusBox.style.display = 'block';
                    try {
                        var res = JSON.parse(xhr.responseText);
                        if (res.success) {
                            statusBox.style.background = 'rgba(16,185,129,0.2)';
                            statusBox.style.color = '#34d399';
                            statusBox.textContent = 'P2P Pairing handshake successful! Connected to peer.';
                        } else {
                            statusBox.style.background = 'rgba(239,68,68,0.2)';
                            statusBox.style.color = '#f87171';
                            statusBox.textContent = 'P2P Pairing failed: ' + (res.message || 'Invalid PIN or peer unreachable');
                        }
                    } catch (e) {
                        statusBox.style.background = 'rgba(239,68,68,0.2)';
                        statusBox.style.color = '#f87171';
                        statusBox.textContent = 'Failed to process pairing response.';
                    }
                };
                xhr.onerror = function() {
                    pairPinBtn.disabled = false;
                    pairPinBtn.textContent = 'Pair Device';
                    statusBox.style.display = 'block';
                    statusBox.style.background = 'rgba(239,68,68,0.2)';
                    statusBox.style.color = '#f87171';
                    statusBox.textContent = 'Network error connecting to IPC backend.';
                };
                xhr.send(JSON.stringify({ pin: pinVal }));
            };
        }

        // Save settings button
        var saveSettingsBtn = document.getElementById('navi-sync-save-settings-btn');
        if (saveSettingsBtn) {
            saveSettingsBtn.onclick = function() {
                var provider = document.getElementById('navi-sync-provider-select').value;
                var statusBox = document.getElementById('navi-sync-status-box');
                saveSettingsBtn.disabled = true;
                saveSettingsBtn.textContent = 'Saving...';
                var xhr = new XMLHttpRequest();
                xhr.open('POST', 'http://127.0.0.1:9001/sync/config', true);
                xhr.onload = function() {
                    saveSettingsBtn.disabled = false;
                    saveSettingsBtn.textContent = 'Save Configuration';
                    statusBox.style.display = 'block';
                    statusBox.style.background = 'rgba(16,185,129,0.2)';
                    statusBox.style.color = '#34d399';
                    statusBox.textContent = 'Configuration saved!';
                    setTimeout(function() { statusBox.style.display = 'none'; }, 2500);
                };
                xhr.onerror = function() {
                    saveSettingsBtn.disabled = false;
                    saveSettingsBtn.textContent = 'Save Configuration';
                    statusBox.style.display = 'block';
                    statusBox.style.background = 'rgba(239,68,68,0.2)';
                    statusBox.style.color = '#f87171';
                    statusBox.textContent = 'Network error saving configuration.';
                };
                xhr.send(JSON.stringify({ cloudProvider: provider }));
            };
        }

        // Push to Cloud Now button
        var pushBtn = document.getElementById('navi-sync-push-btn');
        if (pushBtn) {
            pushBtn.onclick = function() {
                pushBtn.disabled = true;
                pushBtn.textContent = 'Pushing to Cloud...';
                var statusBox = document.getElementById('navi-sync-status-box');
                statusBox.style.display = 'block';
                statusBox.style.background = 'rgba(59,130,246,0.2)';
                statusBox.style.color = '#60a5fa';
                statusBox.textContent = 'Encrypting snapshot and pushing to cloud provider...';

                var xhr = new XMLHttpRequest();
                xhr.open('POST', 'http://127.0.0.1:9001/sync/push', true);
                xhr.onload = function() {
                    pushBtn.disabled = false;
                    pushBtn.textContent = 'Push to Cloud Now';
                    try {
                        var res = JSON.parse(xhr.responseText);
                        if (res.success) {
                            statusBox.style.background = 'rgba(16,185,129,0.2)';
                            statusBox.style.color = '#34d399';
                            statusBox.textContent = 'Successfully pushed to cloud! Generation: ' + res.generation + ' (' + (res.pushedFiles || 0) + ' items, ETag: ' + (res.etag || 'ok') + ')';
                            setTimeout(function() {
                                closeModal();
                                if (typeof window.__naviShowSyncModal === 'function') {
                                    window.__naviShowSyncModal();
                                }
                            }, 1500);
                        } else {
                            statusBox.style.background = 'rgba(239,68,68,0.2)';
                            statusBox.style.color = '#f87171';
                            statusBox.textContent = 'Cloud push failed: ' + (res.message || 'Unknown error');
                        }
                    } catch (e) {
                        statusBox.style.background = 'rgba(239,68,68,0.2)';
                        statusBox.style.color = '#f87171';
                        statusBox.textContent = 'Failed to process push response.';
                    }
                };
                xhr.onerror = function() {
                    pushBtn.disabled = false;
                    pushBtn.textContent = 'Push to Cloud Now';
                    statusBox.style.background = 'rgba(239,68,68,0.2)';
                    statusBox.style.color = '#f87171';
                    statusBox.textContent = 'Network error connecting to IPC backend.';
                };
                xhr.send();
            };
        }

        // Apply Sync Changes button
        var applyBtn = document.getElementById('navi-sync-apply-btn');
        applyBtn.onclick = function() {
            applyBtn.disabled = true;
            applyBtn.textContent = 'Applying Sync...';

            var filter = {
                allManifestsAndLua: document.getElementById('navi-sync-all-manifests').checked,
                allConfigs: document.getElementById('navi-sync-all-configs').checked,
                allCloudSaves: document.getElementById('navi-sync-all-saves').checked,
                allAchievements: document.getElementById('navi-sync-all-achievements').checked,
                allApiKeys: document.getElementById('navi-sync-all-apikeys').checked
            };

            var conflictChoices = {};
            var selects = overlay.querySelectorAll('.navi-conflict-choice');
            selects.forEach(function(sel) {
                var p = sel.getAttribute('data-path');
                if (p) conflictChoices[p] = sel.value;
            });

            var xhr = new XMLHttpRequest();
            xhr.open('POST', 'http://127.0.0.1:9001/sync/apply', true);
            xhr.onload = function() {
                applyBtn.disabled = false;
                applyBtn.textContent = 'Apply Sync Changes';
                var statusBox = document.getElementById('navi-sync-status-box');
                statusBox.style.display = 'block';

                try {
                    var rep = JSON.parse(xhr.responseText);
                    if (rep.success) {
                        statusBox.style.background = 'rgba(16,185,129,0.2)';
                        statusBox.style.color = '#34d399';
                        statusBox.textContent = 'Sync applied successfully! ' + (rep.appliedLogicalPaths ? rep.appliedLogicalPaths.length : 0) + ' items updated.';

                        if (rep.stagedAcfRestartRequiredAppIds && rep.stagedAcfRestartRequiredAppIds.length > 0) {
                            var rBtn = document.getElementById('navi-sync-restart-btn');
                            rBtn.style.display = 'inline-block';
                            rBtn.onclick = function() {
                                if (typeof SteamClient !== 'undefined' && SteamClient.User && SteamClient.User.ChangeSteamMode) {
                                    SteamClient.User.ChangeSteamMode(0);
                                } else {
                                    alert('Please restart the Steam client to commit updated app manifests.');
                                }
                            };
                        }
                    } else {
                        statusBox.style.background = 'rgba(239,68,68,0.2)';
                        statusBox.style.color = '#f87171';
                        statusBox.textContent = 'Sync apply warning: Some items were deferred or pending conflict resolution.';
                    }
                } catch (e) {
                    statusBox.style.background = 'rgba(239,68,68,0.2)';
                    statusBox.style.color = '#f87171';
                    statusBox.textContent = 'Failed to process sync response.';
                }
            };
            xhr.onerror = function() {
                applyBtn.disabled = false;
                applyBtn.textContent = 'Apply Sync Changes';
                var statusBox = document.getElementById('navi-sync-status-box');
                statusBox.style.display = 'block';
                statusBox.style.background = 'rgba(239,68,68,0.2)';
                statusBox.style.color = '#f87171';
                statusBox.textContent = 'Network error applying sync changes.';
            };
            xhr.send(JSON.stringify({ filter: filter, conflictChoices: conflictChoices }));
        };

        // Gamepad navigation support
        function onDirection(e) {
            e.stopPropagation();
        }
        function onOk(e) {
            e.stopPropagation();
        }
        function onCancel(e) {
            e.stopPropagation();
            closeModal();
        }

        window.addEventListener('vgp_ondirection', onDirection, true);
        window.addEventListener('vgp_onok', onOk, true);
        window.addEventListener('vgp_oncancel', onCancel, true);

        function cleanupGamepad() {
            window.removeEventListener('vgp_ondirection', onDirection, true);
            window.removeEventListener('vgp_onok', onOk, true);
            window.removeEventListener('vgp_oncancel', onCancel, true);
        }
    }

    // Expose global modal opener function
    window.__naviShowSyncModal = function(statusData) {
        if (!statusData) {
            fetchSyncStatus(function(err, data) {
                if (!err && data) {
                    renderSyncModal(data);
                }
            });
            return;
        }
        renderSyncModal(statusData);
    };

    // Auto-poll sync status on launch to show modal if remote generation bump exists
    setTimeout(function() {
        fetchSyncStatus(function(err, data) {
            if (!err && data && data.hasRemoteBump === true) {
                console.log('[Navi Sync] Launch trigger: remote bump detected, rendering modal');
                window.__naviShowSyncModal(data);
            }
        });
    }, 2500);
})();

