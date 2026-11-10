/**
 * @file  cdp-bridge.js
 * @brief CDP communication bridge and network request helpers for store pages
 */
if (window.__slsLuaBtnAdded) return;
window.__slsLuaBtnAdded = true;
console.log('[SLS] Store Page Script Triggered');

function ping(msg) { console.log('[SLS] StorePage: ' + msg); }

ping('Script active on: ' + window.location.href);

// ── Backend bridge ────────────────────────────────────────────────────
// store.steampowered.com's CSP (default-src https:) blocks fetch() to
// http://127.0.0.1:9001, and changing location.hash breaks Steam navigation.
// Requests are queued as "<id> <path>" strings; the Navi backend drains
// window.__naviQueue over CDP, replays them against its local server and
// answers through window.__naviResolve(id, status, body).
window.__naviQueue = window.__naviQueue || [];
var naviPending = {};
var naviSeq = 0;
window.__naviResolve = function(id, status, body) {
    var p = naviPending[id];
    if (!p) return;
    delete naviPending[id];
    p(status, body);
};
window.naviFetch = naviFetch;
function naviFetch(url, opts) {
    return new Promise(function(resolve, reject) {
        var path = String(url).replace(/^http:\/\/127\.0\.0\.1:9001/, '');
        var signal = opts && opts.signal;
        if (signal && signal.aborted) { reject(new DOMException('Aborted', 'AbortError')); return; }
        var id = (++naviSeq) + '-' + Date.now();
        var timer = setTimeout(function() {
            delete naviPending[id];
            reject(new Error('Navi bridge timeout'));
        }, 20000);
        if (signal) {
            signal.addEventListener('abort', function() {
                if (!naviPending[id]) return;
                delete naviPending[id];
                clearTimeout(timer);
                reject(new DOMException('Aborted', 'AbortError'));
            });
        }
        naviPending[id] = function(status, body) {
            clearTimeout(timer);
            if (!status) { reject(new Error('Navi backend unreachable')); return; }
            resolve({
                ok: status >= 200 && status < 300,
                status: status,
                text: function() { return Promise.resolve(body); },
                json: function() {
                    try { return Promise.resolve(JSON.parse(body)); }
                    catch (e) { return Promise.reject(e); }
                }
            });
        };
        window.__naviQueue.push(id + ' ' + path);
    });
}

function naviRequest(path, params) {
    var qs = Object.keys(params || {}).map(function (k) {
        return encodeURIComponent(k) + '=' + encodeURIComponent(params[k]);
    }).join('&');
    return naviFetch('http://127.0.0.1:9001' + path + (qs ? '?' + qs : ''))
        .then(function (r) { return r.ok ? r.json().catch(function () { return {}; }) : Promise.reject(r.status); });
}

