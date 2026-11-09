function openResetModal(appId, onCompleted) {
    if (document.getElementById('sls-reset-modal')) return;
    ensureStyles();
    var overlay = document.createElement('div');
    overlay.id = 'sls-reset-modal';
    overlay.setAttribute('role', 'dialog');
    overlay.setAttribute('aria-modal', 'true');
    overlay.setAttribute('data-testid', 'sls-reset-modal');
    overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.82);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);';
    overlay.innerHTML = RESET_MODAL_HTML.replace(/\{\{APPID\}\}/g, appId);
    document.body.appendChild(overlay);

    var closeBtn = overlay.querySelector('#sls-reset-close');
    var cancelBtn = overlay.querySelector('#sls-modal-cancel');
    var confirmBtn = overlay.querySelector('#sls-modal-confirm');
    var input = overlay.querySelector('#sls-reset-input');

    function closeModal() { overlay.remove(); }
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

    confirmBtn.onclick = function() {
        if (confirmBtn.disabled) return;
        confirmBtn.disabled = true;
        confirmBtn.innerText = 'Resetting...';
        fetch('http://127.0.0.1:9001/cloud/apps/' + appId + '/reset', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'X-Navi-Auth': window.__naviAuthToken || '' },
            body: JSON.stringify({ confirm: 'RESET' })
        }).then(function() {
            closeModal();
            alert('Cloud save reset enqueued. Please restart Steam via Navi to safely apply changes.');
            if (onCompleted) onCompleted(true);
        }).catch(function(e) {
            confirmBtn.disabled = false;
            confirmBtn.innerText = 'Confirm Reset';
            alert('Reset error: ' + e);
        });
    };

    trapSteamGamepadModal(overlay, [input, confirmBtn, cancelBtn, closeBtn]);
    setTimeout(function() { input.focus(); }, 100);
}

