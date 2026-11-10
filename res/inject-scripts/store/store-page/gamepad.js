/**
 * @file  gamepad.js
 * @brief Steam Deck and controller navigation focus trapping for store page modals and buttons
 */
function trapSteamGamepadModal(overlay, buttons) {
    var isGrid = Array.isArray(buttons[0]);
    var grid = isGrid ? buttons : buttons.map(function(b) { return [b]; });
    var activeR = 0;
    var activeC = 0;

    function updateFocus() {
        for (var r = 0; r < grid.length; r++) {
            for (var c = 0; c < grid[r].length; c++) {
                var btn = grid[r][c];
                if (!btn) continue;
                if (r === activeR && c === activeC) {
                    btn.style.outline = '3px solid #fff';
                    btn.style.outlineOffset = '2px';

                    // If navigating away from an active text input, blur it cleanly so changes commit and OSK closes
                    if (document.activeElement && document.activeElement !== btn && document.activeElement.tagName && document.activeElement.tagName.toLowerCase() === 'input') {
                        document.activeElement.blur();
                    }

                    // If at the header row (row 0), scroll list container to top
                    if (activeR === 0) {
                        var topContainer = overlay.querySelector('#sls-provider-rows');
                        if (topContainer && topContainer.scrollTop > 0) {
                            topContainer.scrollTop = 0;
                        }
                    }

                    // Ensure parent row block inside scrollable list is scrolled into view
                    var rowParent = btn.closest ? (btn.closest('[data-sls-provider]') || btn.closest('.sls-modal-row')) : null;
                    if (rowParent) {
                        if (rowParent.scrollIntoViewIfNeeded) {
                            rowParent.scrollIntoViewIfNeeded(false);
                        } else if (rowParent.scrollIntoView) {
                            rowParent.scrollIntoView({ block: 'nearest', inline: 'nearest' });
                        }
                    }

                    // Also ensure the exact button/input element itself is scrolled into view
                    if (btn.scrollIntoViewIfNeeded) {
                        btn.scrollIntoViewIfNeeded(false);
                    } else if (btn.scrollIntoView) {
                        btn.scrollIntoView({ block: 'nearest', inline: 'nearest' });
                    }
                } else {
                    btn.style.outline = 'none';
                }
            }
        }
    }
    setTimeout(updateFocus, 100);

    function onOverlayFocusIn(e) {
        var target = e.target;
        for (var r = 0; r < grid.length; r++) {
            var c = grid[r].indexOf(target);
            if (c !== -1) {
                activeR = r;
                activeC = c;
                updateFocus();
                break;
            }
        }
    }
    overlay.addEventListener('focusin', onOverlayFocusIn);

    function cleanup() {
        window.removeEventListener('vgp_ondirection', gamepadCatcher, true);
        window.removeEventListener('vgp_onok', okCatcher, true);
        window.removeEventListener('vgp_oncancel', cancelCatcher, true);
        overlay.removeEventListener('focusin', onOverlayFocusIn);
    }

    overlay.__slsUpdateGamepadGrid = function(newButtons, targetElement) {
        grid = Array.isArray(newButtons[0]) ? newButtons : newButtons.map(function(b) { return [b]; });
        if (targetElement) {
            for (var r = 0; r < grid.length; r++) {
                var c = grid[r].indexOf(targetElement);
                if (c !== -1) {
                    activeR = r;
                    activeC = c;
                    break;
                }
            }
        } else {
            if (activeR >= grid.length) activeR = Math.max(0, grid.length - 1);
            if (grid[activeR] && activeC >= grid[activeR].length) activeC = Math.max(0, grid[activeR].length - 1);
        }
        updateFocus();
    };

    overlay.__slsCleanup = cleanup;
    
    function gamepadCatcher(e) {
        if (!document.body.contains(overlay)) {
            cleanup();
            return;
        }
        e.stopPropagation(); if(e.cancelable) e.preventDefault();
        var dir = e.detail ? e.detail.button : null;
        var startR = activeR, startC = activeC;
        
        function move(dr, dc) {
            var newR = activeR, newC = activeC;
            var attempts = 0;
            var maxAttempts = grid.length * 10;
            
            // Calculate alignment from the RIGHT side to handle ragged rows
            var targetOffsetRight = grid[activeR].length - 1 - activeC;
            
            while (attempts < maxAttempts) {
                attempts++;
                if (dr !== 0) {
                    newR += dr;
                    if (newR < 0) newR = grid.length - 1;
                    if (newR >= grid.length) newR = 0;
                    
                    // Apply right-alignment to the new row
                    newC = grid[newR].length - 1 - targetOffsetRight;
                    if (newC < 0) newC = 0; // Clamp to left if new row is too short
                    if (newC >= grid[newR].length) newC = grid[newR].length - 1;
                }
                if (dc !== 0) {
                    newC += dc;
                    if (newC < 0) newC = grid[newR].length - 1;
                    if (newC >= grid[newR].length) newC = 0;
                }
                var targetBtn = grid[newR][newC];
                if (targetBtn) {
                    activeR = newR;
                    activeC = newC;
                    return;
                }
            }
        }

        if (dir === 9 || dir === 14) move(-1, 0); // Up
        else if (dir === 10 || dir === 15) move(1, 0); // Down
        else if (dir === 11 || dir === 16) move(0, -1); // Left
        else if (dir === 12 || dir === 17) move(0, 1); // Right
        
        updateFocus();
    }
    function okCatcher(e) {
        if (!document.body.contains(overlay)) return;
        e.stopPropagation(); if(e.cancelable) e.preventDefault();
        var btn = grid[activeR] ? grid[activeR][activeC] : null;
        if (btn && !btn.disabled) {
            if (btn.tagName.toLowerCase() === 'input') {
                btn.focus();
                btn.select();
            }
            btn.click();
        }
    }
    function cancelCatcher(e) {
        if (!document.body.contains(overlay)) return;
        e.stopPropagation(); if(e.cancelable) e.preventDefault();
        var closeBtn = overlay.querySelector('#sls-provider-close, #sls-close-x, #sls-btn-cancel, #sls-prov-cancel, #sls-rm-cancel, #sls-depot-close-x, #sls-depot-btn-cancel');
        if (closeBtn) closeBtn.click(); else overlay.remove();
    }
    window.addEventListener('vgp_ondirection', gamepadCatcher, true);
    window.addEventListener('vgp_onok', okCatcher, true);
    window.addEventListener('vgp_oncancel', cancelCatcher, true);
}

function registerSteamFocusNode(element, properties) {
    if (!properties) properties = { focusable: true };
    if (!element.hasAttribute('tabindex')) element.setAttribute('tabindex', '-1');
    element.classList.add('Focusable');
    var parent = element.parentElement;
    var parentNode = null;
    while (parent && parent !== document.body) {
        var fiberKey = Object.keys(parent).find(function(key) { return key.indexOf('__reactFiber') === 0; });
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

