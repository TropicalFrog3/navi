/**
 * @file  gamepad.js
 * @brief Steam Gamepad controller modal navigation and focus trapping handler
 */
/**
 * Steam Gamepad modal focus trapper
 */
function trapSteamGamepadModal(overlay, buttons) {
    var activeIdx = 0;
    function updateFocus() {
        for (var i = 0; i < buttons.length; i++) {
            if (!buttons[i]) continue;
            if (i === activeIdx) {
                buttons[i].style.outline = '3px solid #fff';
                buttons[i].style.outlineOffset = '2px';
                if (buttons[i].focus) buttons[i].focus();
                if (buttons[i].scrollIntoViewIfNeeded) buttons[i].scrollIntoViewIfNeeded();
                else if (buttons[i].scrollIntoView) buttons[i].scrollIntoView({ block: 'nearest', inline: 'nearest' });
            } else {
                buttons[i].style.outline = 'none';
            }
        }
    }
    setTimeout(updateFocus, 100);

    function cleanup() {
        window.removeEventListener('vgp_ondirection', gamepadCatcher, true);
        window.removeEventListener('vgp_onok', okCatcher, true);
        window.removeEventListener('vgp_oncancel', cancelCatcher, true);
    }
    function gamepadCatcher(e) {
        if (!document.body.contains(overlay)) { cleanup(); return; }
        e.stopPropagation(); if (e.cancelable) e.preventDefault();
        var dir = e.detail ? e.detail.button : null;
        var startIdx = activeIdx;
        if (dir === 9 || dir === 11) { // Up or Left
            do { activeIdx = (activeIdx > 0) ? activeIdx - 1 : buttons.length - 1; }
            while (buttons[activeIdx] && buttons[activeIdx].disabled && activeIdx !== startIdx);
        } else if (dir === 10 || dir === 12) { // Down or Right
            do { activeIdx = (activeIdx < buttons.length - 1) ? activeIdx + 1 : 0; }
            while (buttons[activeIdx] && buttons[activeIdx].disabled && activeIdx !== startIdx);
        }
        updateFocus();
    }
    function okCatcher(e) {
        if (!document.body.contains(overlay)) { cleanup(); return; }
        e.stopPropagation(); if (e.cancelable) e.preventDefault();
        if (buttons[activeIdx] && !buttons[activeIdx].disabled) buttons[activeIdx].click();
    }
    function cancelCatcher(e) {
        if (!document.body.contains(overlay)) { cleanup(); return; }
        e.stopPropagation(); if (e.cancelable) e.preventDefault();
        var cancelBtn = overlay.querySelector('#sls-modal-cancel, #sls-modal-close, #navi-close-x, #navi-modal-cancel');
        if (cancelBtn) cancelBtn.click(); else overlay.remove();
    }
    window.addEventListener('vgp_ondirection', gamepadCatcher, true);
    window.addEventListener('vgp_onok', okCatcher, true);
    window.addEventListener('vgp_oncancel', cancelCatcher, true);
}

