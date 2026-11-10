/**
 * @file  restore-modal.js
 * @brief Modal controller for browsing and restoring cloud save backups
 */
/**
 * Modal listing available backups with Restore button
 */
function openRestoreModal(appId, onCompleted) {
    if (document.getElementById('sls-restore-modal')) return;

    var overlay = document.createElement('div');
    overlay.id = 'sls-restore-modal';
    overlay.setAttribute('role', 'dialog');
    overlay.setAttribute('aria-modal', 'true');
    overlay.setAttribute('data-testid', 'sls-restore-modal');
    overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.82);' +
        'z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);';

    overlay.innerHTML = RESTORE_MODAL_HTML.replace(/\{\{APPID\}\}/g, appId);
    document.body.appendChild(overlay);

    var closeBtn = overlay.querySelector('#sls-restore-close');
    var cancelBtn = overlay.querySelector('#sls-modal-cancel');
    var bodyEl = overlay.querySelector('#sls-restore-body');

    function closeModal() {
        overlay.remove();
    }

    closeBtn.onclick = closeModal;
    cancelBtn.onclick = closeModal;

    fetchAppStatus(appId).then(function(data) {
        var backups = data.backups || [];
        if (backups.length === 0) {
            bodyEl.innerHTML = '<div style="text-align:center;padding:24px;color:#9ca3af;font-size:14px;">No backups available for this game.</div>';
            trapSteamGamepadModal(overlay, [cancelBtn, closeBtn]);
            return;
        }

        bodyEl.innerHTML = '';
        var itemBtns = [];
        for (var i = 0; i < backups.length; i++) {
            var file = backups[i];
            var baseName = file.split('/').pop() || file;

            var row = document.createElement('div');
            row.style.cssText = 'display:flex;align-items:center;justify-content:space-between;padding:10px 12px;margin-bottom:6px;background:rgba(0,0,0,0.25);border:1px solid rgba(172,178,201,0.1);border-radius:3px;';

            var nameDiv = document.createElement('div');
            nameDiv.style.cssText = 'font-size:13px;color:#fff;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;margin-right:12px;';
            nameDiv.textContent = baseName;
            nameDiv.title = file;

            var btn = document.createElement('button');
            btn.className = 'sls-btn-restore-item Focusable';
            btn.setAttribute('tabindex', '-1');
            btn.setAttribute('role', 'button');
            btn.setAttribute('data-backup-path', file);
            btn.style.cssText = 'background:#2563eb;color:#fff;border:none;border-radius:2px;padding:6px 14px;font-size:13px;font-weight:500;cursor:pointer;white-space:nowrap;';
            btn.textContent = 'Restore';

            (function(restoreBtn, targetPath) {
                restoreBtn.onclick = function(e) {
                    e.preventDefault();
                    restoreBtn.disabled = true;
                    restoreBtn.textContent = 'Restoring...';
                    restoreAppSave(appId, targetPath)
                        .then(function() {
                            closeModal();
                            alert('Restore enqueued for Steam restart. Please restart Steam to complete restore.');
                            if (typeof onCompleted === 'function') onCompleted(true);
                        })
                        .catch(function(err) {
                            restoreBtn.disabled = false;
                            restoreBtn.textContent = 'Restore';
                            alert('Failed to restore backup: ' + (err && err.message ? err.message : String(err)));
                        });
                };
            })(btn, file);

            row.appendChild(nameDiv);
            row.appendChild(btn);
            bodyEl.appendChild(row);
            itemBtns.push(btn);
        }

        trapSteamGamepadModal(overlay, itemBtns.concat([cancelBtn, closeBtn]));
    }).catch(function(err) {
        bodyEl.innerHTML = '';
        var errEl = document.createElement('div');
        errEl.style.cssText = 'text-align:center;padding:24px;color:#f87171;font-size:14px;';
        errEl.textContent = 'Failed to load backups: ' + (err && err.message ? err.message : String(err));
        bodyEl.appendChild(errEl);
        trapSteamGamepadModal(overlay, [cancelBtn, closeBtn]);
    });
}

