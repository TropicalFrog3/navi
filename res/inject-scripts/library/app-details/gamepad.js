/**
 * @file  gamepad.js
 * @brief Steam Deck gamepad controller navigation handler for library modals
 */
function registerSteamFocusNode(element, properties) {
    if (!properties) properties = { focusable: true };
    if (!element.hasAttribute('tabindex')) element.setAttribute('tabindex', '-1');
    element.classList.add('Focusable');
    var parent = element.parentElement;
    var parentNode = null;
    while (parent && parent !== document.body) {
        var fiberKey = Object.keys(parent).find(key => key.startsWith('__reactFiber'));
        if (fiberKey) {
            var n = parent[fiberKey];
            while (n) {
                if (n.memoizedProps && n.memoizedProps.node) {
                    parentNode = n.memoizedProps.node;
                    break;
                }
                n = n.return;
            }
        }
        if (parentNode) break;
        parent = parent.parentElement;
    }
    if (!parentNode) return null;
    try {
        var NavNodeClass = parentNode.constructor;
        var newNode = new NavNodeClass(parentNode.m_Tree, parentNode, null);
        if (newNode.SetProperties) newNode.SetProperties(properties);
        else newNode.m_Properties = Object.assign(newNode.m_Properties || {}, properties);
        if (newNode.OnMount) newNode.OnMount(element);
        return newNode;
    } catch (e) {
        return null;
    }
}

function trapSteamGamepadModal(overlay, buttons) {
    var activeIdx = -1;

    function isFocusable(el) {
        if (!el || el.disabled) return false;
        return !!(el.offsetWidth || el.offsetHeight || el.getClientRects().length);
    }
    function firstFocusable() {
        for (var i = 0; i < buttons.length; i++) {
            if (isFocusable(buttons[i])) return i;
        }
        return -1;
    }
    function nextFocusable(start, step) {
        var idx = start;
        for (var guard = 0; guard < buttons.length; guard++) {
            idx = (idx + step + buttons.length) % buttons.length;
            if (isFocusable(buttons[idx])) return idx;
        }
        return -1;
    }
    function updateFocus() {
        if (activeIdx < 0 || !isFocusable(buttons[activeIdx])) activeIdx = firstFocusable();
        for (var i = 0; i < buttons.length; i++) {
            if (!buttons[i]) continue;
            if (i === activeIdx) {
                buttons[i].style.outline = '3px solid #fff';
                buttons[i].style.outlineOffset = '2px';
                if (buttons[i].scrollIntoViewIfNeeded) buttons[i].scrollIntoViewIfNeeded();
                else buttons[i].scrollIntoView({ block: 'nearest', inline: 'nearest' });
            } else {
                buttons[i].style.outline = 'none';
            }
        }
    }
    overlay.__slsResetFocus = function() {
        activeIdx = -1;
        updateFocus();
    };
    setTimeout(updateFocus, 100);

    function cleanup() {
        window.removeEventListener('vgp_ondirection', gamepadCatcher, true);
        window.removeEventListener('vgp_onok', okCatcher, true);
        window.removeEventListener('vgp_oncancel', cancelCatcher, true);
    }
    function gamepadCatcher(e) {
        if (!document.body.contains(overlay)) {
            cleanup();
            return;
        }
        e.stopPropagation();
        if (e.cancelable) e.preventDefault();
        var dir = e.detail ? e.detail.button : null;
        if (dir === 9 || dir === 11) { // Up or Left
            if (activeIdx < 0) activeIdx = firstFocusable();
            else activeIdx = nextFocusable(activeIdx, -1);
        } else if (dir === 10 || dir === 12) { // Down or Right
            if (activeIdx < 0) activeIdx = firstFocusable();
            else activeIdx = nextFocusable(activeIdx, 1);
        }
        updateFocus();
    }
    function okCatcher(e) {
        if (!document.body.contains(overlay)) { cleanup(); return; }
        e.stopPropagation();
        if (e.cancelable) e.preventDefault();
        if (activeIdx < 0) activeIdx = firstFocusable();
        if (isFocusable(buttons[activeIdx])) buttons[activeIdx].click();
    }
    function cancelCatcher(e) {
        if (!document.body.contains(overlay)) { cleanup(); return; }
        e.stopPropagation();
        if (e.cancelable) e.preventDefault();
        if (typeof overlay.__slsGamepadCancel === 'function') {
            overlay.__slsGamepadCancel();
            return;
        }
        var cancelBtn = overlay.querySelector('#sls-btn-cancel, #sls-prov-cancel, #sls-close-x, #sls-prov-close, #sls-remove-cancel, #sls-navi-cancel, #sls-navi-close');
        if (cancelBtn) cancelBtn.click(); else overlay.remove();
    }
    window.addEventListener('vgp_ondirection', gamepadCatcher, true);
    window.addEventListener('vgp_onok', okCatcher, true);
    window.addEventListener('vgp_oncancel', cancelCatcher, true);
}

