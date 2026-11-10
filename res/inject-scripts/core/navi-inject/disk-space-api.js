/**
 * @file  disk-space-api.js
 * @brief App install size resolver, depot manifest cache, and unlocked app detection
 */
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

