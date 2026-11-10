/**
 * @file  reset-modal.js
 * @brief Confirmation modal controller for resetting cloud save directories
 */
/**
 * Typed Confirmation Modal for Reset Cloud Save
 */
function resetAppSave(appId, onCompleted) {
    if (document.getElementById('sls-reset-modal')) return;

    var overlay = document.createElement('div');
    overlay.id = 'sls-reset-modal';
    overlay.setAttribute('role', 'dialog');
    overlay.setAttribute('aria-modal', 'true');
    overlay.setAttribute('data-testid', 'sls-reset-modal');
    overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.82);' +
        'z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);';

    overlay.innerHTML = RESET_MODAL_HTML.replace(/\{\{APPID\}\}/g, appId);
    document.body.appendChild(overlay);

    var closeBtn = overlay.querySelector('#sls-reset-close');
    var cancelBtn = overlay.querySelector('#sls-modal-cancel');
    var confirmBtn = overlay.querySelector('#sls-modal-confirm');
    var input = overlay.querySelector('#sls-reset-input');

    trapSteamGamepadModal(overlay, [input, confirmBtn, cancelBtn, closeBtn]);

    function closeModal() {
        overlay.remove();
    }

    closeBtn.onclick = closeModal;
    cancelBtn.onclick = closeModal;

    input.addEventListener('input', function() {
        var val = (input.value || '').trim();
        if (val === 'RESET') {
            confirmBtn.disabled = false;
            confirmBtn.style.opacity = '1';
            confirmBtn.style.cursor = 'pointer';
        } else {
            confirmBtn.disabled = true;
            confirmBtn.style.opacity = '0.4';
            confirmBtn.style.cursor = 'default';
        }
    });

    confirmBtn.onclick = function(e) {
        e.preventDefault();
        if (confirmBtn.disabled) return;
        confirmBtn.disabled = true;
        confirmBtn.innerText = 'Resetting...';

        executeResetAppSave(appId)
            .then(function() {
                closeModal();
                alert('Cloud save reset enqueued. Please restart Steam via Navi to safely apply changes.');
                if (typeof onCompleted === 'function') onCompleted(true);
            })
            .catch(function(err) {
                confirmBtn.innerText = 'Confirm Reset';
                confirmBtn.disabled = false;
                alert('Failed to reset cloud save: ' + (err && err.message ? err.message : err));
            });
    };

    setTimeout(function() { input.focus(); }, 100);
}

