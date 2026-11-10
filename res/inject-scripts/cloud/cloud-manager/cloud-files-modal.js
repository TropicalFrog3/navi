/**
 * @file  cloud-files-modal.js
 * @brief Steam-styled per-game cloud files explorer modal controller
 */
/**
 * Inject shared Steam Cloud table styles following modern-web-guidance responsive-table pattern
 */
function ensureSteamCloudTableStyles(targetDoc) {
    var doc = targetDoc || document;
    if (!doc || !doc.head) return;
    var existing = doc.getElementById('navi-steam-cloud-table-styles');
    if (existing) {
        existing.remove();
    }
    var style = doc.createElement('style');
    style.id = 'navi-steam-cloud-table-styles';
    style.textContent = NAVI_CLOUD_STYLES;
    doc.head.appendChild(style);
}

/**
 * Open Steam-styled "View Steam Cloud" per-game modal (used from Library Navi Menu or Settings)
 */
function openCloudFilesModal(appId, gameTitleHint) {
    ensureSteamCloudTableStyles(document);
    var existing = document.getElementById('navi-cloud-files-modal-overlay');
    if (existing) existing.remove();

    var overlay = document.createElement('div');
    overlay.id = 'navi-cloud-files-modal-overlay';
    overlay.style.cssText = 'position:fixed;top:0;left:0;width:100vw;height:100vh;background:rgba(0,0,0,0.82);z-index:999999;display:flex;align-items:center;justify-content:center;font-family:"Motiva Sans",Arial,sans-serif;backdrop-filter:blur(4px);';

    overlay.innerHTML = CLOUD_FILES_MODAL_HTML;
    document.body.appendChild(overlay);

    var closeX = overlay.querySelector('#navi-close-x');
    closeX.onclick = function() { overlay.remove(); };

    var gameNameEl = overlay.querySelector('#navi-cloud-game-title');
    var currentTitle = resolveGameTitle(appId, gameTitleHint);
    gameNameEl.innerText = currentTitle;
    if (isPlaceholderTitle(currentTitle, appId)) {
        fetch('https://store.steampowered.com/api/appdetails?appids=' + encodeURIComponent(appId))
            .then(function(r) { return r.json(); })
            .then(function(data) {
                var sId = String(appId);
                if (data && data[sId] && data[sId].success && data[sId].data && data[sId].data.name) {
                    var realName = data[sId].data.name;
                    if (!isPlaceholderTitle(realName, sId)) {
                        titleCache[sId] = realName;
                        try { localStorage.setItem('navi_app_titles_cache', JSON.stringify(titleCache)); } catch (e) {}
                        if (gameNameEl) gameNameEl.innerText = realName;
                    }
                }
            }).catch(function() {});
    }

    var actionToolbar = overlay.querySelector('#navi-cloud-actions');

    function makeActionBtn(label, bg, color) {
        var b = document.createElement('button');
        b.className = 'Focusable';
        b.setAttribute('tabindex', '-1');
        b.style.cssText = 'background:' + bg + ';color:' + color + ';border:1px solid rgba(255,255,255,0.12);border-radius:2px;padding:6px 12px;font-size:12px;font-weight:600;cursor:pointer;';
        b.innerText = label;
        return b;
    }

    var syncBtn = makeActionBtn('Sync Now', '#2a475e', '#66c0f4');
    var backupBtn = makeActionBtn('Backup', '#273b4b', '#c6d4df');
    var restoreBtn = makeActionBtn('Restore', '#273b4b', '#c6d4df');
    var resetBtn = makeActionBtn('Reset', '#4c1d1d', '#fca5a5');

    actionToolbar.appendChild(syncBtn);
    actionToolbar.appendChild(backupBtn);
    actionToolbar.appendChild(restoreBtn);
    actionToolbar.appendChild(resetBtn);

    var statusToast = overlay.querySelector('#navi-cloud-status-toast');

    function showStatusMessage(msg, isError) {
        statusToast.style.display = 'block';
        statusToast.style.borderColor = isError ? '#ef4444' : '#66c0f4';
        statusToast.style.color = isError ? '#fca5a5' : '#66c0f4';
        statusToast.innerText = msg;
    }

    var tableRegion = overlay.querySelector('#navi-cloud-table-region');
    var summaryText = overlay.querySelector('#navi-cloud-summary-text');
    var closeBtn = overlay.querySelector('#navi-modal-cancel');
    closeBtn.onclick = function() { overlay.remove(); };

    var focusableButtons = [syncBtn, backupBtn, restoreBtn, resetBtn, closeBtn, closeX];

    function loadFiles() {
        tableRegion.innerHTML = '<div style="padding:32px;text-align:center;color:#8f98a0;font-size:13px;">Scanning cloud storage for AppID ' + appId + '...</div>';
        fetchAppCloudFiles(appId).then(function(data) {
            var resolvedName = resolveGameTitle(appId, data && data.gameTitle ? data.gameTitle : gameTitleHint);
            gameNameEl.innerText = resolvedName + ' (' + appId + ')';
            var files = (data && Array.isArray(data.files)) ? data.files : [];
            summaryText.innerText = files.length + (files.length === 1 ? ' file' : ' files') + ' • Total size: ' + formatBytes(data && data.totalSizeBytes ? data.totalSizeBytes : 0);

            if (files.length === 0) {
                tableRegion.innerHTML = '<div style="padding:36px 20px;text-align:center;color:#8f98a0;font-size:13px;">No cloud save files found for this game yet. Play the game or click <b>Sync Now</b> to upload local saves.</div>';
                return;
            }

            var table = document.createElement('table');
            table.className = 'navi-steam-cloud-table';

            var thead = document.createElement('thead');
            thead.innerHTML = '<tr>' +
                '<th scope="col">FOLDER</th>' +
                '<th scope="col">FILE NAME</th>' +
                '<th scope="col">FILE SIZE</th>' +
                '<th scope="col">DATE WRITTEN</th>' +
                '<th scope="col"></th>' +
                '</tr>';
            table.appendChild(thead);

            var tbody = document.createElement('tbody');
            files.forEach(function(f) {
                var tr = document.createElement('tr');

                var tdFolder = document.createElement('td');
                tdFolder.setAttribute('data-label', 'FOLDER');
                tdFolder.innerText = f.folder || 'SteamCloud';

                var tdName = document.createElement('td');
                tdName.setAttribute('data-label', 'FILE NAME');
                tdName.style.wordBreak = 'break-all';
                tdName.innerText = f.fileName || '';

                var tdSize = document.createElement('td');
                tdSize.className = 'navi-col-num';
                tdSize.setAttribute('data-label', 'FILE SIZE');
                tdSize.innerText = formatBytes(f.sizeBytes);

                var tdDate = document.createElement('td');
                tdDate.className = 'navi-col-num';
                tdDate.setAttribute('data-label', 'DATE WRITTEN');
                tdDate.innerText = formatSteamCloudDate(f.timestamp);

                var tdDl = document.createElement('td');
                tdDl.setAttribute('data-label', 'ACTION');
                tdDl.style.textAlign = 'right';

                var dlBtn = document.createElement('button');
                dlBtn.className = 'navi-steam-cloud-link Focusable';
                dlBtn.setAttribute('tabindex', '-1');
                dlBtn.innerText = 'Download';
                dlBtn.onclick = function() {
                    dlBtn.innerText = 'Downloading...';
                    dlBtn.disabled = true;
                    downloadAppCloudFile(appId, f.fileName, data.provider).then(function(res) {
                        dlBtn.innerText = 'Download';
                        dlBtn.disabled = false;
                        if (res && res.success) {
                            showStatusMessage('Downloaded to ' + (res.savedPath || '~/Downloads'), false);
                        } else {
                            showStatusMessage((res && res.error) ? res.error : 'Download failed', true);
                        }
                    }).catch(function(err) {
                        dlBtn.innerText = 'Download';
                        dlBtn.disabled = false;
                        showStatusMessage('Download error: ' + err.message, true);
                    });
                };

                tdDl.appendChild(dlBtn);
                tr.appendChild(tdFolder);
                tr.appendChild(tdName);
                tr.appendChild(tdSize);
                tr.appendChild(tdDate);
                tr.appendChild(tdDl);
                tbody.appendChild(tr);
                focusableButtons.push(dlBtn);
            });

            table.appendChild(tbody);
            tableRegion.innerHTML = '';
            tableRegion.appendChild(table);
        }).catch(function(err) {
            tableRegion.innerHTML = '<div style="padding:28px;text-align:center;color:#fca5a5;">Failed to load cloud files: ' + (err && err.message ? err.message : 'Unknown error') + '</div>';
        });
    }

    syncBtn.onclick = function() {
        syncBtn.innerText = 'Syncing...';
        syncBtn.disabled = true;
        syncAppNow(appId).then(function(res) {
            syncBtn.innerText = 'Sync Now';
            syncBtn.disabled = false;
            showStatusMessage((res && res.message) ? res.message : 'Cloud sync complete.', false);
            loadFiles();
        }).catch(function(err) {
            syncBtn.innerText = 'Sync Now';
            syncBtn.disabled = false;
            showStatusMessage('Sync failed: ' + err.message, true);
        });
    };

    backupBtn.onclick = function() {
        backupBtn.innerText = 'Backing up...';
        backupBtn.disabled = true;
        backupAppSave(appId).then(function(res) {
            backupBtn.innerText = 'Backup';
            backupBtn.disabled = false;
            showStatusMessage('Backup saved: ' + ((res && res.backupFile) ? res.backupFile : 'created'), false);
        }).catch(function(err) {
            backupBtn.innerText = 'Backup';
            backupBtn.disabled = false;
            showStatusMessage('Backup failed: ' + err.message, true);
        });
    };

    restoreBtn.onclick = function() {
        openRestoreModal(appId, resolveGameTitle(appId, gameTitleHint));
    };

    resetBtn.onclick = function() {
        resetAppSave(appId, resolveGameTitle(appId, gameTitleHint));
    };

    loadFiles();
    trapSteamGamepadModal(overlay, focusableButtons);
}

