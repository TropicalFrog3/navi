/**
 * @file  navi-modal.js
 * @brief Base modal dialog container and backdrop management for CEF
 */
function openNaviModal(appid, isPending, onlineFixInstalled, autoCrackInstalled, provider, onRestart) {
    if (document.getElementById('sls-navi-modal')) return;
    ensureStyles();
    var overlay = document.createElement('div');
    overlay.id = 'sls-navi-modal';
    overlay.setAttribute('role', 'dialog');
    overlay.setAttribute('aria-modal', 'true');
    overlay.setAttribute('data-testid', 'sls-navi-modal');
    overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.8);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);transition:all 0.3s ease;opacity:0;';

    var provText = provider || 'None';
    var removeLabel = isPending ? 'Restart Steam...' : 'Remove Lua';
    var removeBtnStyle = isPending
        ? 'background:rgba(234,179,8,0.2);color:#fde047;'
        : 'color:#f87171;';

    var cardHtml = NAVI_MODAL_HTML
        .replace(/\{\{APPID\}\}/g, appid)
        .replace('{{PROVIDER}}', provText)
        .replace('{{REMOVE_BTN_STYLE}}', removeBtnStyle)
        .replace('{{REMOVE_LABEL}}', removeLabel);

    overlay.innerHTML = cardHtml;
    document.body.appendChild(overlay);

    requestAnimationFrame(function() {
        overlay.style.opacity = '1';
        var card = document.getElementById('sls-navi-card');
        if (card) { card.style.transform = 'scale(1)'; card.style.opacity = '1'; }
    });

    function closeModal(cb) {
        overlay.style.opacity = '0';
        var card = document.getElementById('sls-navi-card');
        if (card) { card.style.transform = 'scale(0.95)'; card.style.opacity = '0'; }
        setTimeout(function() {
            overlay.remove();
            if (typeof cb === 'function') cb();
        }, 300);
    }

    var closeX = document.getElementById('sls-navi-close');
    var cancelBtn = document.getElementById('sls-navi-cancel');
    var provBtn = document.getElementById('sls-navi-btn-provider');
    var confBtn = document.getElementById('sls-navi-btn-config');
    var cloudToggleBtn = document.getElementById('sls-navi-btn-cloud-toggle');
    var cloudFilesBtn = document.getElementById('sls-navi-btn-cloud-files');
    var cloudBackupBtn = document.getElementById('sls-navi-btn-cloud-backup');
    var cloudRestoreBtn = document.getElementById('sls-navi-btn-cloud-restore');
    var cloudResetBtn = document.getElementById('sls-navi-btn-cloud-reset');
    var cloudBadge = document.getElementById('sls-navi-cloud-badge');
    var remBtn = document.getElementById('sls-navi-btn-remove');

    closeX.onclick = function() { closeModal(); };
    cancelBtn.onclick = function() { closeModal(); };

    provBtn.onclick = function(e) {
        e.preventDefault(); e.stopPropagation();
        closeModal(function() {
            openChangeProviderModal(appid, provider || '');
        });
    };

    confBtn.onclick = function(e) {
        e.preventDefault(); e.stopPropagation();
        closeModal(function() {
            openSlsConfig(appid);
        });
    };

    var isAppCloudActive = false;
    function updateModalCloudUI(active) {
        isAppCloudActive = !!active;
        if (cloudBadge) {
            if (isAppCloudActive) {
                cloudBadge.innerText = 'Active';
                cloudBadge.style.color = '#34d399';
            } else {
                cloudBadge.innerText = 'Inactive';
                cloudBadge.style.color = '#9ca3af';
            }
        }
    }

    fetchAppCloudStatus(appid).then(function(cdata) {
        updateModalCloudUI(cdata.redirected);
    }).catch(function() {
        updateModalCloudUI(false);
    });

    cloudToggleBtn.onclick = function(e) {
        e.preventDefault(); e.stopPropagation();
        var target = !isAppCloudActive;
        if (cloudBadge) cloudBadge.innerText = 'Updating...';
        toggleAppCloudRedirect(appid, target).then(function() {
            updateModalCloudUI(target);
        }).catch(function() {
            updateModalCloudUI(isAppCloudActive);
        });
    };

    cloudFilesBtn.onclick = function(e) {
        e.preventDefault(); e.stopPropagation();
        closeModal(function() {
            if (window.NaviCloudManager && typeof window.NaviCloudManager.openCloudFilesModal === 'function') {
                window.NaviCloudManager.openCloudFilesModal(appid);
            } else if (window.opener && window.opener.NaviCloudManager && typeof window.opener.NaviCloudManager.openCloudFilesModal === 'function') {
                window.opener.NaviCloudManager.openCloudFilesModal(appid);
            }
        });
    };

    cloudBackupBtn.onclick = function(e) {
        e.preventDefault(); e.stopPropagation();
        var span = cloudBackupBtn.querySelector('span');
        if (span) span.innerText = 'Backing up...';
        backupAppCloudSave(appid).then(function() {
            if (span) span.innerText = 'Backup Created!';
            setTimeout(function() { if (span) span.innerText = 'Backup Cloud Save'; }, 2000);
        }).catch(function() {
            if (span) span.innerText = 'Backup Cloud Save';
        });
    };

    cloudRestoreBtn.onclick = function(e) {
        e.preventDefault(); e.stopPropagation();
        closeModal(function() {
            openRestoreModal(appid);
        });
    };

    cloudResetBtn.onclick = function(e) {
        e.preventDefault(); e.stopPropagation();
        closeModal(function() {
            openResetModal(appid);
        });
    };

    remBtn.onclick = function(e) {
        e.preventDefault(); e.stopPropagation();
        if (isPending) {
            log('Restart requested from Navi menu');
            fetch('http://127.0.0.1:9001/restart', { mode: 'no-cors' }).catch(function(){});
            closeModal();
        } else {
            closeModal(function() {
                openRemoveConfirmModal(appid, function() {
                    if (typeof onRestart === 'function') onRestart();
                });
            });
        }
    };

    trapSteamGamepadModal(overlay, [provBtn, confBtn, cloudToggleBtn, cloudFilesBtn, cloudBackupBtn, cloudRestoreBtn, cloudResetBtn, remBtn, cancelBtn, closeX]);
}

