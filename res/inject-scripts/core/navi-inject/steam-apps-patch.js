/**
 * @file  steam-apps-patch.js
 * @brief Intercepts SteamClient.Apps.RegisterForAppDetails to provide accurate disk requirements
 */
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

function patchSteamClientApps() {
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
}

