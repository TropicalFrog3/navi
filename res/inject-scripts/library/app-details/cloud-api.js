/**
 * @file  cloud-api.js
 * @brief Client-side REST API wrapper for engine CloudRedirect endpoints
 */
function fetchAppCloudStatus(appId) {
    if (window.NaviCloudManager && window.NaviCloudManager.fetchAppStatus) {
        return window.NaviCloudManager.fetchAppStatus(appId);
    }
    return fetch('http://127.0.0.1:9001/cloud/apps/' + appId + '/status', {
        headers: { 'X-Navi-Auth': window.__naviAuthToken || '' }
    }).then(function(r) { return r.json(); });
}

function toggleAppCloudRedirect(appId, redirect) {
    if (window.NaviCloudManager && window.NaviCloudManager.toggleAppRedirect) {
        return window.NaviCloudManager.toggleAppRedirect(appId, redirect);
    }
    return fetch('http://127.0.0.1:9001/cloud/apps/' + appId + '/toggle', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
            'X-Navi-Auth': window.__naviAuthToken || ''
        },
        body: JSON.stringify({ redirect: !!redirect })
    }).then(function(r) { return r.json(); });
}

function backupAppCloudSave(appId) {
    if (window.NaviCloudManager && window.NaviCloudManager.backupAppSave) {
        return window.NaviCloudManager.backupAppSave(appId);
    }
    return fetch('http://127.0.0.1:9001/cloud/apps/' + appId + '/backup', {
        method: 'POST',
        headers: { 'X-Navi-Auth': window.__naviAuthToken || '' }
    }).then(function(r) { return r.json(); });
}

