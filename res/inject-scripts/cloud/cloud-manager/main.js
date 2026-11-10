/**
 * @file  main.js
 * @brief Lifecycle initialization, polling coordinator, and public NaviCloudManager API export
 */
/**
 * Polling tick
 */
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

function init() {
    startPolling(POLL_INTERVAL_MS);
}

// Public API
window.NaviCloudManager = {
    fetchStatus: fetchStatus,
    fetchPendingOps: fetchPendingOps,
    toggleMaster: toggleMaster,
    fetchProviderStatus: fetchProviderStatus,
    fetchProviderCredentials: fetchProviderCredentials,
    saveProviderCredentials: saveProviderCredentials,
    scanMigrationSource: scanMigrationSource,
    migrateProviderData: migrateProviderData,
    fetchOrphanedSaves: fetchOrphanedSaves,
    cleanOrphanedApp: cleanOrphanedApp,
    fetchAllBackups: fetchAllBackups,
    deleteBackupArchive: deleteBackupArchive,
    signInProvider: signInProvider,
    signOutProvider: signOutProvider,
    restartSteam: restartSteam,
    fetchAppStatus: fetchAppStatus,
    fetchCloudApps: fetchCloudApps,
    fetchAppCloudFiles: fetchAppCloudFiles,
    syncAppNow: syncAppNow,
    downloadAppCloudFile: downloadAppCloudFile,
    formatBytes: formatBytes,
    formatSteamCloudDate: formatSteamCloudDate,
    resolveGameTitle: resolveGameTitle,
    ensureSteamCloudTableStyles: ensureSteamCloudTableStyles,
    openCloudFilesModal: openCloudFilesModal,
    toggleAppRedirect: toggleAppRedirect,
    backupAppSave: backupAppSave,
    restoreAppSave: restoreAppSave,
    resetAppSave: resetAppSave,
    executeResetAppSave: executeResetAppSave,
    openResetModal: resetAppSave,
    openRestoreModal: openRestoreModal,
    openSettingsModal: openSettingsModal,
    updatePendingBanner: updatePendingBanner,
    secureFetch: secureFetch,
    startPolling: startPolling,
    stopPolling: stopPolling,
    getState: function() { return Object.assign({}, state); },
    init: init
};

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
} else {
    init();
}

