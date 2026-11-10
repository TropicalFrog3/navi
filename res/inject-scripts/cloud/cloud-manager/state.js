/**
 * @file  state.js
 * @brief Core runtime state and polling intervals for Cloud Saves manager
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

