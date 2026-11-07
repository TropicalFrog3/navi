/**
 * Navi UI Studio - Live Reload & Mock Bridge Client.
 *
 * Injected into preview shells to provide:
 * 1. SSE Live Reload connection with automatic hot-script updates.
 * 2. Mock SteamClient environment for browser preview.
 * 3. Transparent port 9001 mock API bridge (fetch redirection + window.__naviQueue drain loop).
 * 4. Cross-frame command dispatch for modal launchers (Config, Cloud, Depot, Import).
 * 5. Console forwarding to Studio Dashboard HUD.
 */

(function () {
    'use strict';

    if (window.__naviLiveReloadClientLoaded) return;
    window.__naviLiveReloadClientLoaded = true;

    console.log('[Navi Studio] Live reload client initialized.');

    // -------------------------------------------------------------------------
    // 1. Mock SteamClient Environment
    // -------------------------------------------------------------------------
    window.SteamClient = window.SteamClient || {
        Apps: {
            RegisterForAppDetails: function (appid, cb) {
                if (typeof cb === 'function') {
                    setTimeout(function () {
                        cb({
                            nAppId: parseInt(appid, 10) || 1443430,
                            strTitle: 'Rogue',
                            lDiskSpaceRequiredBytes: 1542000000,
                        });
                    }, 50);
                }
                return { unregister: function () {} };
            },
            GetCustomImageURL: function () { return ''; },
            SpecifyCompatTool: function () {},
            SetAppHidden: function (appid, hidden) {
                console.log('[SteamClient Mock] SetAppHidden:', appid, hidden);
            },
            RunGame: function (appid) {
                console.log('[SteamClient Mock] RunGame:', appid);
            },
            TerminateApp: function (appid) {
                console.log('[SteamClient Mock] TerminateApp:', appid);
            },
        },
        Storage: {
            GetAppDetails: function () { return {}; },
        },
        System: {
            OpenURL: function (url) {
                console.log('[SteamClient Mock] OpenURL:', url);
                window.open(url, '_blank');
            },
        },
        Window: {
            BringToFront: function () {},
            Minimize: function () {},
            Close: function () {},
        },
        User: {
            GetSteamID: function () { return '76561198000000001'; },
            GetAccountName: function () { return 'deck'; },
        },
    };

    // -------------------------------------------------------------------------
    // 2. Port 9001 Mock API Redirection
    // -------------------------------------------------------------------------
    var origFetch = window.fetch;
    window.fetch = function (resource, init) {
        var url = typeof resource === 'string' ? resource : (resource && resource.url ? resource.url : '');
        if (url) {
            // Rewrite port 9001 loopback targets to relative paths on preview server
            if (url.indexOf('http://127.0.0.1:9001') === 0 || url.indexOf('http://localhost:9001') === 0) {
                var cleanPath = url.replace(/^http:\/\/(127\.0\.0\.1|localhost):9001/, '');
                if (typeof resource === 'string') {
                    resource = cleanPath;
                } else if (resource && resource.url) {
                    resource = new Request(cleanPath, init);
                }
            }
        }
        return origFetch.call(this, resource, init);
    };

    // -------------------------------------------------------------------------
    // 3. Store Page window.__naviQueue Drain Bridge
    // -------------------------------------------------------------------------
    window.__naviQueue = window.__naviQueue || [];
    setInterval(function () {
        if (!window.__naviQueue || window.__naviQueue.length === 0) return;
        var pending = window.__naviQueue.splice(0);
        pending.forEach(function (item) {
            if (!item) return;
            var spaceIdx = item.indexOf(' ');
            if (spaceIdx === -1) return;
            var reqId = item.substring(0, spaceIdx);
            var reqPath = item.substring(spaceIdx + 1);

            fetch(reqPath)
                .then(function (res) {
                    return res.text().then(function (text) {
                        return { status: res.status, body: text };
                    });
                })
                .then(function (result) {
                    if (typeof window.__naviResolve === 'function') {
                        window.__naviResolve(reqId, result.status, result.body);
                    }
                })
                .catch(function (err) {
                    if (typeof window.__naviResolve === 'function') {
                        window.__naviResolve(reqId, 500, JSON.stringify({ error: err.message }));
                    }
                });
        });
    }, 40);

    // -------------------------------------------------------------------------
    // 4. Console Forwarding to Studio Dashboard HUD
    // -------------------------------------------------------------------------
    function forwardLog(level, args) {
        try {
            var msg = Array.prototype.slice.call(args).map(function (arg) {
                if (typeof arg === 'object') {
                    try { return JSON.stringify(arg); } catch (e) { return String(arg); }
                }
                return String(arg);
            }).join(' ');

            if (window.parent && window.parent !== window) {
                window.parent.postMessage({
                    type: 'NAVI_CONSOLE_LOG',
                    level: level,
                    message: msg,
                    time: new Date().toLocaleTimeString(),
                }, '*');
            }
        } catch (e) {}
    }

    var origLog = console.log;
    var origWarn = console.warn;
    var origError = console.error;
    var origInfo = console.info;

    console.log = function () { forwardLog('info', arguments); origLog.apply(console, arguments); };
    console.info = function () { forwardLog('info', arguments); origInfo.apply(console, arguments); };
    console.warn = function () { forwardLog('warn', arguments); origWarn.apply(console, arguments); };
    console.error = function () { forwardLog('error', arguments); origError.apply(console, arguments); };

    // -------------------------------------------------------------------------
    // 5. SSE Live Reload Connection
    // -------------------------------------------------------------------------
    var eventSource = null;
    var reconnectTimer = null;

    function notifyStatus(connected, extra) {
        if (window.parent && window.parent !== window) {
            window.parent.postMessage(Object.assign({
                type: 'NAVI_PREVIEW_STATUS',
                connected: connected,
                time: new Date().toLocaleTimeString(),
            }, extra || {}), '*');
        }
    }

    function connectSSE() {
        if (eventSource) {
            try { eventSource.close(); } catch (e) {}
        }

        try {
            eventSource = new EventSource('/_live/events');

            eventSource.onopen = function () {
                console.log('[Navi Studio] Connected to Live Reload stream.');
                notifyStatus(true);
                updateBadge(true);
            };

            eventSource.onerror = function () {
                notifyStatus(false);
                updateBadge(false);
                if (eventSource) {
                    try { eventSource.close(); } catch (e) {}
                    eventSource = null;
                }
                clearTimeout(reconnectTimer);
                reconnectTimer = setTimeout(connectSSE, 2000);
            };

            eventSource.onmessage = function (e) {
                try {
                    var data = JSON.parse(e.data);
                    handleLiveEvent(data);
                } catch (err) {
                    console.error('[Navi Studio] Error parsing SSE payload:', err);
                }
            };
        } catch (e) {
            clearTimeout(reconnectTimer);
            reconnectTimer = setTimeout(connectSSE, 2000);
        }
    }

    function handleLiveEvent(data) {
        if (!data || !data.type) return;

        if (data.type === 'connected') {
            console.log('[Navi Studio] SSE Session established:', data.client_id);
            return;
        }

        if (data.type === 'reload' || data.type === 'file_change') {
            var fileName = data.file || (data.files && data.files[0]) || '';
            console.log('[Navi Studio] Reload triggered by file change:', fileName);

            // Forward to parent Studio
            if (window.parent && window.parent !== window) {
                window.parent.postMessage({
                    type: 'NAVI_RELOAD_OCCURRED',
                    file: fileName,
                    timestamp: Date.now(),
                    time: new Date().toLocaleTimeString(),
                }, '*');
            }

            // Pulse badge
            pulseBadge();

            // Check if hot script update or full refresh
            if (fileName.indexOf('.html') !== -1) {
                window.location.reload();
            } else {
                // Hot-re-execute script where applicable, or reload cleanly
                hotReloadScript(data);
            }
        }
    }

    function hotReloadScript(data) {
        var fileName = data.file || '';
        console.log('[Navi Studio] Hot-reloading script:', fileName);

        // Reset injection flags so re-execution works
        window.__slsInjected = false;
        window.__slsLuaBtnAdded = false;
        window.__slsAppDetailsInjected = false;
        window.__naviCloudManagerLoaded = false;

        // Fetch fresh script content and execute
        var scriptUrl = '/_preview/scripts/' + encodeURIComponent(fileName) + '?t=' + Date.now();
        fetch(scriptUrl)
            .then(function (r) {
                if (!r.ok) throw new Error('HTTP ' + r.status);
                return r.text();
            })
            .then(function (code) {
                // If depot script, substitute %APPID%
                if (fileName.indexOf('depot') !== -1) {
                    code = code.replace(/%APPID%/g, '1443430');
                }
                var scriptEl = document.createElement('script');
                scriptEl.type = 'text/javascript';
                scriptEl.text = code;
                document.body.appendChild(scriptEl);
                console.log('[Navi Studio] Hot-applied script update:', fileName);
            })
            .catch(function () {
                // Fallback to full page reload on error
                window.location.reload();
            });
    }

    // -------------------------------------------------------------------------
    // 6. Cross-Frame Modal Launcher Dispatcher
    // -------------------------------------------------------------------------
    window.addEventListener('message', function (e) {
        if (!e.data || !e.data.action) return;

        switch (e.data.action) {
            case 'OPEN_CONFIG_MODAL':
                if (typeof window.__naviShowConfigPage === 'function') {
                    window.__naviShowConfigPage();
                } else {
                    var cfgTab = document.getElementById('sls-config-top-tab');
                    if (cfgTab) cfgTab.click();
                    else console.warn('[Navi Studio] Config modal trigger not found in active shell.');
                }
                break;

            case 'OPEN_CLOUD_MODAL':
                if (window.NaviCloudManager && typeof window.NaviCloudManager.openSettingsModal === 'function') {
                    window.NaviCloudManager.openSettingsModal();
                } else {
                    var cloudBtn = document.querySelector('.sls-cloud-toggle-btn') || document.getElementById('sls-cloud-btn');
                    if (cloudBtn) cloudBtn.click();
                    else console.warn('[Navi Studio] Cloud Save modal trigger not found in active shell.');
                }
                break;

            case 'OPEN_DEPOT_MODAL':
                // Check if depot modal script can be invoked or fetch it
                var existingDepot = document.getElementById('sls-depot-overlay-modal');
                if (existingDepot) {
                    existingDepot.style.display = 'flex';
                } else {
                    fetch('/_preview/scripts/store/depot-selection-script.js?t=' + Date.now())
                        .then(function (r) { return r.text(); })
                        .then(function (code) {
                            var scriptEl = document.createElement('script');
                            scriptEl.text = code.replace(/%APPID%/g, '1443430');
                            document.body.appendChild(scriptEl);
                        })
                        .catch(function (err) {
                            console.error('[Navi Studio] Could not load depot selection modal:', err);
                        });
                }
                break;

            case 'OPEN_LUA_IMPORT_MODAL':
                if (typeof window.__naviShowImportLuaPage === 'function') {
                    window.__naviShowImportLuaPage();
                } else {
                    var topTab = document.getElementById('sls-top-tab');
                    if (topTab) topTab.click();
                    else console.warn('[Navi Studio] LUA Import trigger not found in active shell.');
                }
                break;

            case 'FORCE_RELOAD':
                window.location.reload();
                break;

            default:
                break;
        }
    });

    // -------------------------------------------------------------------------
    // 7. Preview Mini Floating Badge
    // -------------------------------------------------------------------------
    var badgeEl = null;

    function createBadge() {
        if (badgeEl || document.getElementById('sls-preview-badge')) return;
        badgeEl = document.createElement('div');
        badgeEl.id = 'sls-preview-badge';
        badgeEl.style.cssText =
            'position: fixed; bottom: 12px; right: 12px; z-index: 999999; ' +
            'background: rgba(14, 20, 27, 0.85); backdrop-filter: blur(6px); ' +
            'color: #dcdedf; border: 1px solid rgba(26, 159, 255, 0.4); ' +
            'padding: 6px 12px; border-radius: 20px; font-family: monospace; ' +
            'font-size: 11px; display: flex; align-items: center; gap: 8px; ' +
            'box-shadow: 0 4px 12px rgba(0,0,0,0.5); cursor: pointer; user-select: none; ' +
            'transition: transform 0.2s ease, opacity 0.2s ease;';

        badgeEl.innerHTML = '<span id="sls-badge-dot" style="width:8px; height:8px; border-radius:50%; background:#10b981;"></span>' +
                            '<span>NAVI LIVE</span>';

        badgeEl.title = 'Click to force reload';
        badgeEl.onclick = function () { window.location.reload(); };
        document.body.appendChild(badgeEl);
    }

    function updateBadge(connected) {
        var dot = document.getElementById('sls-badge-dot');
        if (dot) {
            dot.style.background = connected ? '#10b981' : '#ef4444';
        }
    }

    function pulseBadge() {
        if (!badgeEl) return;
        badgeEl.style.transform = 'scale(1.15)';
        badgeEl.style.borderColor = '#eab308';
        setTimeout(function () {
            if (badgeEl) {
                badgeEl.style.transform = 'scale(1.0)';
                badgeEl.style.borderColor = 'rgba(26, 159, 255, 0.4)';
            }
        }, 350);
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', function () {
            createBadge();
            connectSSE();
        });
    } else {
        createBadge();
        connectSSE();
    }
})();

