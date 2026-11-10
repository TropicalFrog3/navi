/**
 * @file  security.js
 * @brief CEF origin verification, loopback URL validation, and secureFetch transport wrapper
 */
/**
 * Check that the current window execution context is a trusted Steam CEF origin.
 */
function isOriginCompliant() {
    try {
        var origin = window.location.origin;
        if (!origin || origin === 'null' || origin.indexOf('file:') === 0) {
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

/**
 * Ensure the target URL is strictly directed to the local backend port 9001.
 */
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

/**
 * Secure fetch wrapper:
 * - Enforces origin compliance
 * - Injects X-Navi-Auth header from window.__naviAuthToken
 * - Enforces loopback destination
 * - Supports naviFetch bridge fallback if CSP restricts direct fetch
 */
function secureFetch(pathOrUrl, options) {
    if (!isOriginCompliant()) {
        return Promise.reject(new Error('Security check failed: untrusted origin ' + window.location.origin));
    }
    var targetUrl = validateTargetUrl(pathOrUrl);
    if (!targetUrl) {
        return Promise.reject(new Error('Security check failed: target URL is not a valid Navi backend loopback address'));
    }

    var opts = Object.assign({}, options || {});
    var method = String(opts.method || 'GET').toUpperCase();
    var headers = Object.assign({}, opts.headers || {});
    headers['X-Navi-Auth'] = window.__naviAuthToken || '';
    headers['Connection'] = 'close';
    if (opts.body && typeof opts.body === 'string' && !headers['Content-Type']) {
        headers['Content-Type'] = 'application/json';
    }
    opts.headers = headers;

    if (typeof window.naviFetch === 'function' && window.location.hostname.indexOf('steampowered.com') !== -1) {
        return window.naviFetch(targetUrl, opts);
    }

    return fetch(targetUrl, opts).catch(function(err) {
        if (method === 'GET') {
            return fetch(targetUrl);
        }
        if (method === 'POST' && opts.mode !== 'no-cors') {
            var fallbackBody = 'X-Navi-Auth: ' + (window.__naviAuthToken || '') + '\r\n' + (typeof opts.body === 'string' ? opts.body : '{}');
            return fetch(targetUrl, {
                method: 'POST',
                headers: { 'Content-Type': 'text/plain' },
                body: fallbackBody
            });
        }
        throw err;
    });
}

