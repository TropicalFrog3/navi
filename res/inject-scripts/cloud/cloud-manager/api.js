/**
 * @file  api.js
 * @brief Client-side REST API wrapper for engine CloudRedirect endpoints
 */
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

function fetchCloudApps(provider) {
    var url = '/cloud/apps';
    if (provider) {
        url += '?provider=' + encodeURIComponent(provider);
    }
    return secureFetch(url, { method: 'GET' })
        .then(function(res) { return res.json(); });
}

function fetchAppCloudFiles(appId, provider) {
    var url = '/cloud/apps/' + encodeURIComponent(appId) + '/files';
    if (provider) {
        url += '?provider=' + encodeURIComponent(provider);
    }
    return secureFetch(url, { method: 'GET' })
        .then(function(res) { return res.json(); });
}

function syncAppNow(appId) {
    return secureFetch('/cloud/apps/' + encodeURIComponent(appId) + '/sync', {
        method: 'POST',
        body: JSON.stringify({})
    }).then(function(res) { return res.json(); });
}

function downloadAppCloudFile(appId, fileName, provider) {
    return secureFetch('/cloud/apps/' + encodeURIComponent(appId) + '/files/download', {
        method: 'POST',
        body: JSON.stringify({
            fileName: String(fileName || ''),
            provider: String(provider || state.provider || '')
        })
    }).then(function(res) {
        return res.json();
    });
}

function fetchProviderCredentials(provider) {
    var p = provider || state.provider || 'r2';
    return secureFetch('/cloud/provider/credentials?provider=' + encodeURIComponent(p), {
        method: 'GET'
    }).then(function(res) {
        return res.json();
    });
}

function saveProviderCredentials(payload) {
    return secureFetch('/cloud/provider/credentials', {
        method: 'POST',
        body: JSON.stringify(payload || {})
    }).then(function(res) {
        return res.json();
    });
}

function scanMigrationSource(provider) {
    var p = provider || state.provider || 'local-only';
    return secureFetch('/cloud/migrate/scan?provider=' + encodeURIComponent(p), {
        method: 'GET'
    }).then(function(res) {
        return res.json();
    });
}

function migrateProviderData(sourceProvider, targetProvider, switchActiveProvider) {
    return secureFetch('/cloud/migrate', {
        method: 'POST',
        body: JSON.stringify({
            sourceProvider: String(sourceProvider || ''),
            targetProvider: String(targetProvider || ''),
            switchActiveProvider: switchActiveProvider !== false
        })
    }).then(function(res) {
        return res.json();
    });
}

function fetchOrphanedSaves(provider) {
    var p = provider || state.provider || 'local-only';
    return secureFetch('/cloud/orphans?provider=' + encodeURIComponent(p), {
        method: 'GET'
    }).then(function(res) {
        return res.json();
    });
}

function cleanOrphanedApp(appId, provider, deleteFiles) {
    return secureFetch('/cloud/orphans/clean', {
        method: 'POST',
        body: JSON.stringify({
            appId: Number(appId) || 0,
            provider: String(provider || state.provider || 'local-only'),
            deleteFiles: !!deleteFiles
        })
    }).then(function(res) {
        return res.json();
    });
}

function fetchAllBackups() {
    return secureFetch('/cloud/backups', {
        method: 'GET'
    }).then(function(res) {
        return res.json();
    });
}

function deleteBackupArchive(appId, fileName) {
    return secureFetch('/cloud/backups/delete', {
        method: 'POST',
        body: JSON.stringify({
            appId: Number(appId) || 0,
            fileName: String(fileName || '')
        })
    }).then(function(res) {
        return res.json();
    });
}

