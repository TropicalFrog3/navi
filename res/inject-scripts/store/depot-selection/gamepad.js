/**
 * @file  gamepad.js
 * @brief Steam Gamepad controller modal navigation, focus trapping, and button handling
 */
var activeGamepadCleanup = null;
    function setupGamepadTrap(buttons, startR, startC) {
        if (activeGamepadCleanup) {
            activeGamepadCleanup();
            activeGamepadCleanup = null;
        }
        activeGamepadCleanup = trapSteamGamepadModal(overlay, buttons, startR, startC);
    }

    function isStoreGamepadMode() {
        var isBpmWindow = (window.name || '').indexOf('BPM') !== -1;
        var isBpmTitle = /Steam Big Picture Mode/i.test(document.title || '');
        var isTenfootUa = /Valve Steam Tenfoot/i.test(navigator.userAgent || '');
        var hasGamepadClass = !!(document.body && (document.body.classList.contains('GamepadUI') || document.body.classList.contains('gamepad_mode')));
        var hasDesktopClass = !!((document.body && document.body.classList.contains('DesktopUI')) || document.querySelector('.DesktopUI'));
        if (hasDesktopClass && !isBpmWindow && !isBpmTitle && !isTenfootUa) return false;
        return isBpmWindow || isBpmTitle || isTenfootUa || hasGamepadClass;
    }

    function trapSteamGamepadModal(overlay, buttons, initialActiveR, initialActiveC) {
        var isGrid = Array.isArray(buttons[0]);
        var grid = isGrid ? buttons : buttons.map(function(b) { return [b]; });
        var activeR = (initialActiveR !== undefined) ? initialActiveR : 0;
        var activeC = (initialActiveC !== undefined) ? initialActiveC : 0;
        var showFocusRing = !!(overlay && overlay.__slsShowFocusRing) || isStoreGamepadMode();

        function updateFocus() {
            for (var r = 0; r < grid.length; r++) {
                for (var c = 0; c < grid[r].length; c++) {
                    var btn = grid[r][c];
                    if (!btn) continue;
                    if (r === activeR && c === activeC && showFocusRing) {
                        btn.style.outline = '3px solid #fff';
                        btn.style.outlineOffset = '2px';
                        if (btn.scrollIntoViewIfNeeded) btn.scrollIntoViewIfNeeded();
                        else btn.scrollIntoView({ block: 'nearest', inline: 'nearest' });
                    } else {
                        btn.style.outline = 'none';
                    }
                }
            }
        }
        updateFocus();
        setTimeout(updateFocus, 50);
        
        function gamepadCatcher(e) {
            if (!document.body.contains(overlay)) {
                cleanup();
                return;
            }
            e.stopPropagation(); if(e.cancelable) e.preventDefault();
            var dir = e.detail ? e.detail.button : null;
            
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

            showFocusRing = true;
            if (overlay) overlay.__slsShowFocusRing = true;
            updateFocus();
        }
        function okCatcher(e) {
            if (!document.body.contains(overlay)) return;
            e.stopPropagation(); if(e.cancelable) e.preventDefault();
            var btn = grid[activeR] ? grid[activeR][activeC] : null;
            if (!btn || btn.disabled) return;
            // If this is a checkbox (or its parent label), toggle it
            if (btn.tagName === 'INPUT' && btn.type === 'checkbox') {
                btn.checked = !btn.checked;
                btn.dispatchEvent(new Event('change', { bubbles: true }));
            } else if (btn.tagName === 'LABEL' && btn.querySelector('input[type="checkbox"]')) {
                var cb = btn.querySelector('input[type="checkbox"]');
                cb.checked = !cb.checked;
                cb.dispatchEvent(new Event('change', { bubbles: true }));
            } else {
                btn.click();
            }
        }
        function cancelCatcher(e) {
            if (!document.body.contains(overlay)) return;
            e.stopPropagation(); if(e.cancelable) e.preventDefault();
            var closeBtn = overlay.querySelector('#sls-depot-close-x, #sls-depot-btn-cancel');
            if (closeBtn) closeBtn.click(); else overlay.remove();
        }
        function onOverlayPointerDown() {
            showFocusRing = false;
            if (overlay) overlay.__slsShowFocusRing = false;
            updateFocus();
        }
        window.addEventListener('vgp_ondirection', gamepadCatcher, true);
        window.addEventListener('vgp_onok', okCatcher, true);
        window.addEventListener('vgp_oncancel', cancelCatcher, true);
        overlay.addEventListener('mousedown', onOverlayPointerDown, true);
        overlay.addEventListener('pointerdown', onOverlayPointerDown, true);

        function cleanup() {
            window.removeEventListener('vgp_ondirection', gamepadCatcher, true);
            window.removeEventListener('vgp_onok', okCatcher, true);
            window.removeEventListener('vgp_oncancel', cancelCatcher, true);
            overlay.removeEventListener('mousedown', onOverlayPointerDown, true);
            overlay.removeEventListener('pointerdown', onOverlayPointerDown, true);
            for (var r = 0; r < grid.length; r++) {
                for (var c = 0; c < grid[r].length; c++) {
                    if (grid[r][c]) grid[r][c].style.outline = 'none';
                }
            }
        }
        return cleanup;
    }
