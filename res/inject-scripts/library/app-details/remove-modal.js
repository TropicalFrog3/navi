/**
 * @file  remove-modal.js
 * @brief Confirmation modal for removing Lua plugins and resetting saves
 */
function openRemoveConfirmModal(appid, onConfirmed) {
    log('Remove clicked for ' + appid);
    ensureStyles();
    var modalOverlay = document.createElement('div');
    modalOverlay.id = 'sls-remove-modal';
    modalOverlay.setAttribute('role', 'dialog');
    modalOverlay.setAttribute('aria-modal', 'true');
    modalOverlay.setAttribute('data-testid', 'sls-remove-modal');
    modalOverlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.8);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);';
    modalOverlay.innerHTML = REMOVE_MODAL_HTML.replace(/\{\{APPID\}\}/g, appid);

    document.body.appendChild(modalOverlay);

    var cancelBtn = document.getElementById('sls-remove-cancel');
    var confirmBtn = document.getElementById('sls-remove-confirm');
    trapSteamGamepadModal(modalOverlay, [cancelBtn, confirmBtn]);

    cancelBtn.onclick = function() { modalOverlay.remove(); };
    confirmBtn.onclick = function() {
        confirmBtn.innerText = 'Processing...';
        confirmBtn.style.opacity = '0.5';
        confirmBtn.style.pointerEvents = 'none';

        try {
            if (window.SteamClient && window.SteamClient.Apps && window.SteamClient.Apps.SetAppHidden) {
                window.SteamClient.Apps.SetAppHidden(appid, true);
            }
        } catch(e) {}

        fetch('http://127.0.0.1:9001/remove?id=' + appid + '&game=true', { mode: 'no-cors' })
            .then(function() {
                if (typeof onConfirmed === 'function') onConfirmed();
                modalOverlay.remove();
            })
            .catch(function() {
                if (typeof onConfirmed === 'function') onConfirmed();
                modalOverlay.remove();
            });
    };
}

