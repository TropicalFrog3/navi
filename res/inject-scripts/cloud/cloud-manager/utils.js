/**
 * @file  utils.js
 * @brief Data formatting helpers and Steam game title resolution cache
 */
/**
 * Format byte count matching Valve's Steam Cloud page ("4.46 KB", "1.42 MB", "581 B")
 */
function formatBytes(bytes) {
    var n = Number(bytes) || 0;
    if (n < 1024) return n + ' B';
    if (n < 1024 * 1024) return (n / 1024).toFixed(2).replace(/\.00$/, '') + ' KB';
    if (n < 1024 * 1024 * 1024) return (n / (1024 * 1024)).toFixed(2).replace(/\.00$/, '') + ' MB';
    return (n / (1024 * 1024 * 1024)).toFixed(2) + ' GB';
}

/**
 * Format Unix seconds matching Valve's Steam Cloud page ("3 Mar @ 1:48pm")
 */
function formatSteamCloudDate(unixSec) {
    var ts = Number(unixSec) || 0;
    if (!ts) return '—';
    var d = new Date(ts * 1000);
    if (isNaN(d.getTime())) return '—';
    var months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
    var day = d.getDate();
    var mon = months[d.getMonth()];
    var hrs = d.getHours();
    var mins = d.getMinutes();
    var ampm = hrs >= 12 ? 'pm' : 'am';
    hrs = hrs % 12;
    if (hrs === 0) hrs = 12;
    var minStr = mins < 10 ? ('0' + mins) : String(mins);
    return day + ' ' + mon + ' @ ' + hrs + ':' + minStr + ampm;
}

var titleCache = {};
try {
    titleCache = JSON.parse(localStorage.getItem('navi_app_titles_cache') || '{}');
} catch (e) {}

function isPlaceholderTitle(title, appId) {
    if (!title || typeof title !== 'string') return true;
    var t = title.trim();
    if (t.indexOf('App ') === 0 || t.indexOf('app ') === 0 ||
        t.indexOf('App_') === 0 || t.indexOf('app_') === 0) {
        return true;
    }
    if (appId && t === String(appId)) return true;
    return false;
}

/**
 * Resolve human-readable Game Title from Steam client appStore, backend title, or AppID fallback
 */
function resolveGameTitle(appId, backendTitle) {
    var idNum = Number(appId);
    var sId = String(appId);
    if (titleCache[sId]) {
        if (!isPlaceholderTitle(titleCache[sId], sId)) {
            return titleCache[sId];
        }
        delete titleCache[sId];
        try { localStorage.setItem('navi_app_titles_cache', JSON.stringify(titleCache)); } catch (e) {}
    }
    try {
        if (window.appStore && typeof window.appStore.GetAppOverviewByAppID === 'function') {
            var ov = window.appStore.GetAppOverviewByAppID(idNum);
            if (ov && ov.display_name && !isPlaceholderTitle(ov.display_name, sId)) return String(ov.display_name);
        }
        if (window.opener && window.opener.appStore && typeof window.opener.appStore.GetAppOverviewByAppID === 'function') {
            var ov2 = window.opener.appStore.GetAppOverviewByAppID(idNum);
            if (ov2 && ov2.display_name && !isPlaceholderTitle(ov2.display_name, sId)) return String(ov2.display_name);
        }
    } catch (e) {}
    if (backendTitle && !isPlaceholderTitle(backendTitle, sId)) {
        return backendTitle;
    }
    return backendTitle || ('App ' + appId);
}

