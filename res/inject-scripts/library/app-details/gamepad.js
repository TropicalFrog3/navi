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
    var activeIdx = 0;
    function updateFocus() {
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
        var startIdx = activeIdx;
        if (dir === 9 || dir === 11) { // Up or Left
            do {
                activeIdx = (activeIdx > 0) ? activeIdx - 1 : buttons.length - 1;
            } while (buttons[activeIdx] && buttons[activeIdx].disabled && activeIdx !== startIdx);
        } else if (dir === 10 || dir === 12) { // Down or Right
            do {
                activeIdx = (activeIdx < buttons.length - 1) ? activeIdx + 1 : 0;
            } while (buttons[activeIdx] && buttons[activeIdx].disabled && activeIdx !== startIdx);
        }
        updateFocus();
    }
    function okCatcher(e) {
        if (!document.body.contains(overlay)) { cleanup(); return; }
        e.stopPropagation();
        if (e.cancelable) e.preventDefault();
        if (buttons[activeIdx] && !buttons[activeIdx].disabled) buttons[activeIdx].click();
    }
    function cancelCatcher(e) {
        if (!document.body.contains(overlay)) { cleanup(); return; }
        e.stopPropagation();
        if (e.cancelable) e.preventDefault();
        var cancelBtn = overlay.querySelector('#sls-btn-cancel, #sls-prov-cancel, #sls-close-x, #sls-prov-close, #sls-remove-cancel, #sls-navi-cancel, #sls-navi-close');
        if (cancelBtn) cancelBtn.click(); else overlay.remove();
    }
    window.addEventListener('vgp_ondirection', gamepadCatcher, true);
    window.addEventListener('vgp_onok', okCatcher, true);
    window.addEventListener('vgp_oncancel', cancelCatcher, true);
}

