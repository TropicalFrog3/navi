/**
 * @file  cloud-fallback.js
 * @brief Standalone fallback implementation of NaviCloudManager when cloud_manager.js is not loaded
 */
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

function initFallbackCloudManager() {
    startPolling(POLL_INTERVAL_MS);
}

function setupCloudManager() {
    if (window.NaviCloudManager && typeof window.NaviCloudManager.init === 'function') {
        window.NaviCloudManager.init();
        return;
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
        init: initFallbackCloudManager
    };

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', initFallbackCloudManager);
    } else {
        initFallbackCloudManager();
    }
}

