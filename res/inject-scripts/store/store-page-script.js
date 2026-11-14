/**
 * @file  store-page-script.js
 * @brief Injects unlock and depot download buttons into Steam store pages
 */
(function() {
    // Inlined Styles & Templates
    var SLS_STORE_STYLES = "@keyframes slsShimmer {\n  0% { background-position: -200% 0; }\n  100% { background-position: 200% 0; }\n}\n\n.sls-bar-host {\n  position: relative;\n  overflow: hidden;\n}\n\n.sls-bar-fill {\n  position: absolute;\n  top: 0;\n  left: 0;\n  height: 100%;\n  width: 0%;\n  border-radius: inherit;\n  background: linear-gradient(90deg, rgba(16, 185, 129, 0.15) 0%, rgba(52, 211, 153, 0.35) 50%, rgba(16, 185, 129, 0.15) 100%),\n              linear-gradient(90deg, rgba(255, 255, 255, 0) 0%, rgba(255, 255, 255, 0.2) 50%, rgba(255, 255, 255, 0) 100%);\n  background-size: 100% 100%, 200% 100%;\n  animation: slsShimmer 2.5s infinite linear;\n  box-shadow: inset 0 0 12px rgba(52, 211, 153, 0.3), 0 0 8px rgba(16, 185, 129, 0.4);\n  border-right: 2px solid rgba(52, 211, 153, 0.8);\n  transition: width 0.55s cubic-bezier(0.4, 0, 0.2, 1), background 0.4s ease, box-shadow 0.4s ease, border-color 0.4s ease;\n  pointer-events: none;\n  z-index: 0;\n}\n\n.sls-bar-fill.sls-bar-fail {\n  background: linear-gradient(90deg, rgba(225, 29, 72, 0.2) 0%, rgba(244, 63, 94, 0.4) 50%, rgba(225, 29, 72, 0.2) 100%),\n              linear-gradient(90deg, rgba(255, 255, 255, 0) 0%, rgba(255, 255, 255, 0.15) 50%, rgba(255, 255, 255, 0) 100%);\n  background-size: 100% 100%, 200% 100%;\n  box-shadow: inset 0 0 12px rgba(244, 63, 94, 0.35), 0 0 10px rgba(225, 29, 72, 0.5);\n  border-right: 2px solid rgba(244, 63, 94, 0.9);\n}\n\n.sls-bar-fill.sls-bar-done {\n  background: linear-gradient(90deg, rgba(16, 185, 129, 0.35) 0%, rgba(52, 211, 153, 0.55) 100%);\n  box-shadow: inset 0 0 16px rgba(52, 211, 153, 0.5), 0 0 12px rgba(16, 185, 129, 0.6);\n  border-right: 2px solid rgba(52, 211, 153, 1);\n}\n\n.sls-bar-host > a {\n  position: relative;\n  z-index: 1;\n}\n\n#sls-provider-rows::-webkit-scrollbar {\n  width: 6px;\n}\n\n#sls-provider-rows::-webkit-scrollbar-track {\n  background: rgba(0, 0, 0, 0.2);\n  border-radius: 3px;\n}\n\n#sls-provider-rows::-webkit-scrollbar-thumb {\n  background: rgba(172, 178, 201, 0.3);\n  border-radius: 3px;\n}\n\n#sls-provider-rows::-webkit-scrollbar-thumb:hover {\n  background: rgba(172, 178, 201, 0.5);\n}\n\n.sls-btn-action {\n  display: flex;\n  justify-content: center;\n  align-items: center;\n  background: rgba(172, 178, 201, 0.14);\n  color: rgb(220, 222, 223);\n  border: none;\n  border-radius: 2px;\n  padding: 6px 14px;\n  font-size: 15px;\n  font-weight: 400;\n  font-family: \"Motiva Sans\", sans-serif;\n  height: 42px;\n  box-sizing: border-box;\n  cursor: pointer;\n  text-decoration: none;\n  transition: background 0.1s ease;\n}\n\n.sls-btn-action:not(:disabled):hover {\n  background: rgba(172, 178, 201, 0.25);\n}\n\n.sls-btn-action:not(:disabled):active {\n  background: rgba(172, 178, 201, 0.30);\n}\n\nbutton[data-sls-provider-download]:hover {\n  background: #2d73ff !important;\n}\n\n.sls-btn-close {\n  background: transparent;\n  border: none;\n  color: #969696;\n  font-size: 24px;\n  cursor: pointer;\n  width: 32px;\n  height: 32px;\n  border-radius: 2px;\n  display: flex;\n  align-items: center;\n  justify-content: center;\n  transition: background 0.1s ease;\n  flex-shrink: 0;\n}\n\n.sls-btn-close:hover {\n  background: rgba(172, 178, 201, 0.14);\n  color: #fff;\n}\n\nbutton:disabled {\n  opacity: 0.5;\n  cursor: default;\n}\n\n#sls-rm-confirm:hover {\n  background: rgba(220, 38, 38, 1) !important;\n}\n\n#sls-btn-save:hover {\n  background: #2d73ff !important;\n}\n\n#sls-hubcap:focus,\n#sls-ryuu:focus,\n#sls-dpbx:focus {\n  border-color: #6366f1 !important;\n}\n\n";
    var PROVIDER_MODAL_HTML = "<div style=\"background: #1e2024; border: 1px solid #3d4450; border-radius: 6px; padding: 18px 22px; width: 680px; max-width:calc(100% - 32px); max-height: 85vh; max-height: calc(100vh - 40px); display: flex; flex-direction: column; overflow: hidden; box-shadow: 0 8px 24px rgba(0,0,0,0.6); font-family: 'Motiva Sans', sans-serif; color: #dcdedf; box-sizing: border-box;\" id=\"sls-modal-card\">\n    <div style=\"display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:14px;flex-shrink:0;\">\n        <div>\n            <h2 style=\"margin:0; font-size:20px; font-weight:300; color: #fff; text-transform: uppercase; letter-spacing: 1px;\">Lua download provider</h2>\n            <p style=\"margin:4px 0 0;font-size:13px;color:#969696;\">AppID: {{APPID}}</p>\n        </div>\n        <button id=\"sls-provider-close\" class=\"sls-btn-close Focusable\" tabindex=\"-1\" role=\"button\" aria-label=\"Close\">&times;</button>\n    </div>\n    <div id=\"sls-provider-rows\" style=\"flex: 1 1 auto; min-height: 0; overflow-y: auto; overflow-x: hidden; padding-right: 6px; padding-left: 2px; padding-top: 2px; padding-bottom: 2px; display: flex; flex-direction: column; gap: 6px; overscroll-behavior: contain;\">\n        {{PROVIDER_ROWS}}\n    </div>\n    <div style=\"display:flex;justify-content:flex-end;gap:10px;margin-top:14px;flex-shrink:0;\">\n        <button id=\"sls-clear-app-cache\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"flex:1;height:40px;font-size:14px;\">Clear App Cache</button>\n        <button id=\"sls-clear-all-cache\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"flex:1;height:40px;font-size:14px;\">Clear All Cache</button>\n        <button id=\"sls-auto-search\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"flex:1;height:40px;font-size:14px;background:#06bfff;color:#fff;\">Auto Download</button>\n    </div>\n</div>\n\n";
    var REMOVE_MODAL_HTML = "<div style=\"background: #1e2024; border: 1px solid #3d4450; border-radius: 4px; padding: 24px; width: 440px; max-width: calc(100% - 32px); max-height: 85vh; max-height: calc(100vh - 40px); overflow-y: auto; box-sizing: border-box; box-shadow: 0 4px 16px rgba(0,0,0,0.5); font-family: 'Motiva Sans', sans-serif; color: #dcdedf;\">\n    <h2 style=\"margin:0 0 10px;font-size:22px;font-weight:300;color:#fff;text-transform:uppercase;letter-spacing:1px;\">Remove Lua</h2>\n    <p style=\"margin:0 0 24px;font-size:16px;color:#969696;\">Remove Lua and Game files for AppID <b>{{APPID}}</b>?</p>\n    <div style=\"display:flex;flex-direction:column;gap:10px;\">\n        <div style=\"display:flex;gap:10px;\">\n            <button id=\"sls-rm-clear-app-cache\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"flex:1;padding:0 12px;height:36px;font-size:13px;\">Clear App Cache</button>\n            <button id=\"sls-rm-clear-all-cache\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"flex:1;padding:0 12px;height:36px;font-size:13px;\">Clear All Cache</button>\n        </div>\n        <div style=\"display:flex;justify-content:flex-end;gap:10px;\">\n            <button id=\"sls-rm-cancel\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"padding: 0 24px; height: 36px;\">Cancel</button>\n            <button id=\"sls-rm-confirm\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"padding: 0 24px; height: 36px; background: rgba(220, 38, 38, 0.8); color: #fff;\">Remove</button>\n        </div>\n    </div>\n</div>\n\n";
    var CONFIG_MODAL_HTML = "<div style=\"background: #1e2024; border: 1px solid #3d4450; border-radius: 4px; padding: 24px; width: 440px; max-width: calc(100% - 32px); max-height: 85vh; max-height: calc(100vh - 40px); overflow-y: auto; box-sizing: border-box; box-shadow: 0 4px 16px rgba(0,0,0,0.5); font-family: 'Motiva Sans', sans-serif; color: #dcdedf; transition: all 0.3s cubic-bezier(0.16, 1, 0.3, 1); transform: scale(0.95); opacity: 0;\" id=\"sls-modal-card\">\n    <!-- Title bar -->\n    <div style=\"display:flex; justify-content:space-between; align-items:flex-start; margin-bottom:20px;\">\n        <div>\n            <h2 style=\"margin:0; font-size:22px; font-weight:300; color: #fff; text-transform: uppercase; letter-spacing: 1px;\">API Credentials</h2>\n            <p style=\"margin:4px 0 0; font-size:14px; color:#969696;\">AppID: {{APPID}}</p>\n        </div>\n        <button id=\"sls-close-x\" class=\"sls-btn-close Focusable\" tabindex=\"-1\" role=\"button\" aria-label=\"Close\">&times;</button>\n    </div>\n    <!-- API Settings Section -->\n    <div>\n        <div style=\"margin-bottom:16px;\">\n            <div style=\"display:flex; justify-content:space-between; margin-bottom:6px; font-size:12px; font-weight:600; color:#969696;\">\n                <span>HubcapDB API Key</span>\n                <a href=\"https://hubcapmanifest.com/\" class=\"Focusable\" tabindex=\"-1\" role=\"button\" target=\"_blank\" style=\"color:#06bfff; text-decoration:none; transition: color 0.2s;\">Get Key</a>\n            </div>\n            <input id=\"sls-hubcap\" type=\"text\" value=\"{{HUBCAP_KEY}}\" style=\"width:100%; box-sizing:border-box; background:rgba(0,0,0,0.4); border:1px solid #3d4450; color:#dcdedf; padding:10px 14px; border-radius:2px; font-family:'Motiva Sans', sans-serif; font-size:14px; outline:none; transition: all 0.2s;\" placeholder=\"Optional...\"/>\n        </div>\n        <div style=\"margin-bottom:16px;\">\n            <div style=\"display:flex; justify-content:space-between; margin-bottom:6px; font-size:12px; font-weight:600; color:#969696;\">\n                <span>Ryuu API Key</span>\n                <a href=\"https://generator.ryuu.lol/\" class=\"Focusable\" tabindex=\"-1\" role=\"button\" target=\"_blank\" style=\"color:#06bfff; text-decoration:none; transition: color 0.2s;\">Get Key</a>\n            </div>\n            <input id=\"sls-ryuu\" type=\"text\" value=\"{{RYUU_KEY}}\" style=\"width:100%; box-sizing:border-box; background:rgba(0,0,0,0.4); border:1px solid #3d4450; color:#dcdedf; padding:10px 14px; border-radius:2px; font-family:'Motiva Sans', sans-serif; font-size:14px; outline:none; transition: all 0.2s;\" placeholder=\"Optional...\"/>\n        </div>\n        <div style=\"margin-bottom:24px;\">\n            <div style=\"display:flex; justify-content:space-between; margin-bottom:6px; font-size:12px; font-weight:600; color:#969696;\">\n                <span>DepotBox API Key</span>\n                <a href=\"https://depotbox.org/pricing\" class=\"Focusable\" tabindex=\"-1\" role=\"button\" target=\"_blank\" style=\"color:#06bfff; text-decoration:none; transition: color 0.2s;\">Get Key</a>\n            </div>\n            <input id=\"sls-dpbx\" type=\"text\" value=\"{{DPBX_KEY}}\" style=\"width:100%; box-sizing:border-box; background:rgba(0,0,0,0.4); border:1px solid #3d4450; color:#dcdedf; padding:10px 14px; border-radius:2px; font-family:'Motiva Sans', sans-serif; font-size:14px; outline:none; transition: all 0.2s;\" placeholder=\"Optional...\"/>\n        </div>\n    </div>\n    <!-- Footer Actions -->\n    <div style=\"display:flex; justify-content:flex-end; gap:10px;\">\n        <button id=\"sls-btn-cancel\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"padding: 0 24px; height: 36px;\">Cancel</button>\n        <button id=\"sls-btn-save\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"padding: 0 24px; height: 36px; background: #06bfff; color: #fff;\">Save Keys</button>\n    </div>\n</div>\n\n";

    // --- styles.js ---
    /**
     * @file  styles.js
     * @brief Dynamic stylesheet injection for store page elements and modals
     */
    function ensureStoreStyles() {
        if (document.getElementById('sls-bar-style')) return;
        var s = document.createElement('style');
        s.id = 'sls-bar-style';
        s.textContent = SLS_STORE_STYLES;
        document.head.appendChild(s);
    }

    // Alias for backward-compatibility with internal call sites
    var injectLoadingBarStyles = ensureStoreStyles;

    // --- cdp-bridge.js ---
    /**
     * @file  cdp-bridge.js
     * @brief CDP communication bridge and network request helpers for store pages
     */
    if (window.__slsLuaBtnAdded) return;
    window.__slsLuaBtnAdded = true;
    console.log('[SLS] Store Page Script Triggered');

    function ping(msg) { console.log('[SLS] StorePage: ' + msg); }

    ping('Script active on: ' + window.location.href);

    // ── Backend bridge ────────────────────────────────────────────────────
    // store.steampowered.com's CSP (default-src https:) blocks fetch() to
    // http://127.0.0.1:9001, and changing location.hash breaks Steam navigation.
    // Requests are queued as "<id> <path>" strings; the Navi backend drains
    // window.__naviQueue over CDP, replays them against its local server and
    // answers through window.__naviResolve(id, status, body).
    window.__naviQueue = window.__naviQueue || [];
    var naviPending = {};
    var naviSeq = 0;
    window.__naviResolve = function(id, status, body) {
        var p = naviPending[id];
        if (!p) return;
        delete naviPending[id];
        p(status, body);
    };
    window.naviFetch = naviFetch;
    function naviFetch(url, opts) {
        return new Promise(function(resolve, reject) {
            var path = String(url).replace(/^http:\/\/127\.0\.0\.1:9001/, '');
            var signal = opts && opts.signal;
            if (signal && signal.aborted) { reject(new DOMException('Aborted', 'AbortError')); return; }
            var id = (++naviSeq) + '-' + Date.now();
            var timer = setTimeout(function() {
                delete naviPending[id];
                reject(new Error('Navi bridge timeout'));
            }, 20000);
            if (signal) {
                signal.addEventListener('abort', function() {
                    if (!naviPending[id]) return;
                    delete naviPending[id];
                    clearTimeout(timer);
                    reject(new DOMException('Aborted', 'AbortError'));
                });
            }
            naviPending[id] = function(status, body) {
                clearTimeout(timer);
                if (!status) { reject(new Error('Navi backend unreachable')); return; }
                resolve({
                    ok: status >= 200 && status < 300,
                    status: status,
                    text: function() { return Promise.resolve(body); },
                    json: function() {
                        try { return Promise.resolve(JSON.parse(body)); }
                        catch (e) { return Promise.reject(e); }
                    }
                });
            };
            window.__naviQueue.push(id + ' ' + path);
        });
    }

    function naviRequest(path, params) {
        var qs = Object.keys(params || {}).map(function (k) {
            return encodeURIComponent(k) + '=' + encodeURIComponent(params[k]);
        }).join('&');
        return naviFetch('http://127.0.0.1:9001' + path + (qs ? '?' + qs : ''))
            .then(function (r) { return r.ok ? r.json().catch(function () { return {}; }) : Promise.reject(r.status); });
    }

    // --- gamepad.js ---
    /**
     * @file  gamepad.js
     * @brief Steam Deck and controller navigation focus trapping for store page modals and buttons
     */
    function isStoreGamepadMode() {
        var isBpmWindow = (window.name || '').indexOf('BPM') !== -1;
        var isBpmTitle = /Steam Big Picture Mode/i.test(document.title || '');
        var isTenfootUa = /Valve Steam Tenfoot/i.test(navigator.userAgent || '');
        var hasGamepadClass = !!(document.body && (document.body.classList.contains('GamepadUI') || document.body.classList.contains('gamepad_mode')));
        var hasDesktopClass = !!((document.body && document.body.classList.contains('DesktopUI')) || document.querySelector('.DesktopUI'));
        if (hasDesktopClass && !isBpmWindow && !isBpmTitle && !isTenfootUa) return false;
        return isBpmWindow || isBpmTitle || isTenfootUa || hasGamepadClass;
    }

    function trapSteamGamepadModal(overlay, buttons) {
        var isGrid = Array.isArray(buttons[0]);
        var grid = isGrid ? buttons : buttons.map(function(b) { return [b]; });
        var activeR = 0;
        var activeC = 0;
        var showFocusRing = isStoreGamepadMode();

        function updateFocus() {
            for (var r = 0; r < grid.length; r++) {
                for (var c = 0; c < grid[r].length; c++) {
                    var btn = grid[r][c];
                    if (!btn) continue;
                    if (r === activeR && c === activeC && showFocusRing) {
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

        function onOverlayPointerDown() {
            showFocusRing = false;
            updateFocus();
        }
        overlay.addEventListener('mousedown', onOverlayPointerDown, true);
        overlay.addEventListener('pointerdown', onOverlayPointerDown, true);

        function cleanup() {
            window.removeEventListener('vgp_ondirection', gamepadCatcher, true);
            window.removeEventListener('vgp_onok', okCatcher, true);
            window.removeEventListener('vgp_oncancel', cancelCatcher, true);
            overlay.removeEventListener('focusin', onOverlayFocusIn);
            overlay.removeEventListener('mousedown', onOverlayPointerDown, true);
            overlay.removeEventListener('pointerdown', onOverlayPointerDown, true);
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

            showFocusRing = true;
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

    // --- provider-config.js ---
    /**
     * @file  provider-config.js
     * @brief Lua provider settings, API key synchronization, and download request dispatcher
     */
    // Cache of app unlock status: { appid: { exists, pending, onlineFixInstalled, autoCrackInstalled } }
    var appUnlockStatus = {};

    var luaProviders = [
        { name: 'Ryuu', key: 'sls-ryuu-key', url: 'https://generator.ryuu.lol' },
        { name: 'DepotBox', key: 'sls-dpbx-key', url: 'https://depotbox.org/' },
        { name: 'HubcapDB', key: 'sls-hubcap-key', url: 'https://hubcapmanifest.com/api-keys/stats' },
        { name: 'Spinoza', key: 'sls-spinoza-key', url: '#' }
    ];

    function getProviderOrder() {
        if (localStorage.getItem('sls-lua-provider-order-v') !== '4') {
            var oldSaved = (localStorage.getItem('sls-lua-provider-order') || '').split(',');
            var migrated = [];
            var indexMap = { 0: 0, 1: 1, 2: 2, 3: 3, 5: 3 }; // Ryuu, DepotBox, HubcapDB, Spinoza
            oldSaved.forEach(function(value) {
                var idx = parseInt(value, 10);
                var target = indexMap[idx];
                if (target !== undefined && target < luaProviders.length && migrated.indexOf(target) === -1) {
                    migrated.push(target);
                }
            });
            for (var j = 0; j < luaProviders.length; j++) {
                if (migrated.indexOf(j) === -1) migrated.push(j);
            }
            localStorage.setItem('sls-lua-provider-order', migrated.join(','));
            localStorage.removeItem('sls-ost-key');
            localStorage.removeItem('sls-wudrm-key');
            localStorage.removeItem('sls-steamrun-key');
            localStorage.removeItem('sls-morr-key');
            localStorage.removeItem('sls-sushi-key');
            localStorage.removeItem('sls-twentytwo-key');
            localStorage.setItem('sls-lua-provider-order-v', '4');
            return migrated;
        }

        var saved = (localStorage.getItem('sls-lua-provider-order') || '').split(',');
        var order = [];
        saved.forEach(function(value) {
            var index = parseInt(value, 10);
            if (index >= 0 && index < luaProviders.length && order.indexOf(index) === -1) order.push(index);
        });
        for (var i = 0; i < luaProviders.length; i++) {
            if (order.indexOf(i) === -1) order.push(i);
        }
        return order;
    }

    function saveProviderOrder(order) {
        localStorage.setItem('sls-lua-provider-order', order.join(','));
    }

    function escapeHtml(value) {
        return String(value).replace(/[&<>"']/g, function(character) {
            return {'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[character];
        });
    }

    function sanitizeSeedKey(val) {
        if (!val || val.indexOf('%') === 0) return '';
        return val;
    }

    var configApiKeys = {
        'sls-ryuu-key': sanitizeSeedKey('%RYUU_KEY%'),
        'sls-dpbx-key': sanitizeSeedKey('%DPBX_KEY%'),
        'sls-hubcap-key': sanitizeSeedKey('%HUBCAP_KEY%')
    };

    try {
        localStorage.removeItem('sls-ryuu-key');
        localStorage.removeItem('sls-dpbx-key');
        localStorage.removeItem('sls-hubcap-key');
    } catch (_) {}

    function fetchConfigApiKeys(onLoaded) {
        naviFetch('http://127.0.0.1:9001/api-keys')
            .then(function(res) {
                if (!res.ok) throw new Error('HTTP ' + res.status);
                return res.json();
            })
            .then(function(data) {
                if (data && typeof data === 'object') {
                    if (typeof data.ryuu === 'string') configApiKeys['sls-ryuu-key'] = data.ryuu;
                    if (typeof data.dpbx === 'string') configApiKeys['sls-dpbx-key'] = data.dpbx;
                    if (typeof data.hubcap === 'string') configApiKeys['sls-hubcap-key'] = data.hubcap;
                }
                if (typeof onLoaded === 'function') onLoaded(configApiKeys);
            })
            .catch(function() {
                if (typeof onLoaded === 'function') onLoaded(configApiKeys);
            });
    }

    function sendApiKeys() {
        var ryuu = configApiKeys['sls-ryuu-key'] || '';
        var dpbx = configApiKeys['sls-dpbx-key'] || '';
        var hubcap = configApiKeys['sls-hubcap-key'] || '';

        naviFetch('http://127.0.0.1:9001/set-api-keys?ryuu=' + encodeURIComponent(ryuu) + '&dpbx=' + encodeURIComponent(dpbx) + '&hubcap=' + encodeURIComponent(hubcap)).catch(function() {});
    }

    function sendLuaRequest(appid, providerIndex, customOrder) {
        var params = { id: appid };
        if (providerIndex === null) params.order = (customOrder || getProviderOrder()).join(',');
        else params.provider = providerIndex;
        return naviRequest('/download', params);
    }

    // --- download-tracker.js ---
    /**
     * @file  download-tracker.js
     * @brief Progress tracking, row loading bar animations, and download session state management
     */
    // ── Row loading bar ───────────────────────────────────────────────────
    // Phases reported via pushStatus (button span text) → progress milestones.
    // Between milestones the bar crawls automatically; it snaps forward when the
    // next phase text is detected. On success it completes to 100% then fades
    // out; on failure it turns red and fades out after a short pause.

    var SLS_PHASE_MAP = [
        { match: /checking/i,    pct: 5  },
        { match: /trying/i,      pct: 35 },
        { match: /browser/i,     pct: 45 },
        { match: /extracting/i,  pct: 65 },
        { match: /installing/i,  pct: 82 },
        { match: /installed/i,   pct: 100 },
    ];
    var SLS_FAIL_TEXTS = /not available|failed|not found|steam not|no api|no decryption key|missing decryption key/i;

    /**
     * Attach a loading bar to `luaLink`'s parent wrapper and poll the span
     * text for phase updates until the download completes or fails.
     * Works identically for single-provider and auto-download.
     */
    function startRowLoadingBar(luaBtn, luaLink) {
        injectLoadingBarStyles();

        // Find the actual clickable <a> element's parent to host the bar
        var host = luaLink.parentElement || luaBtn;
        host.classList.add('sls-bar-host');

        // Remove any stale bar
        var oldBar = host.querySelector('.sls-bar-fill');
        if (oldBar) oldBar.remove();

        var bar = document.createElement('div');
        bar.className = 'sls-bar-fill';
        host.insertBefore(bar, host.firstChild);

        var currentPct  = 0;
        var targetPct   = 0;
        var pollId      = null;
        var lastText    = '';
        var finished    = false;

        function setTarget(pct) {
            if (pct <= targetPct) return;
            targetPct = pct;
            bar.style.width = targetPct + '%';
        }

        function finish(success) {
            if (finished) return;
            finished = true;
            clearInterval(pollId);
            if (success) {
                bar.classList.add('sls-bar-done');
                bar.style.width = '100%';
                setTimeout(function() {
                    bar.style.transition = 'opacity 0.6s ease';
                    bar.style.opacity = '0';
                    setTimeout(function() {
                        bar.remove();
                        host.classList.remove('sls-bar-host');
                    }, 650);
                }, 900);
            } else {
                bar.classList.add('sls-bar-fail');
                bar.style.width = '100%';
                setTimeout(function() {
                    bar.style.transition = 'opacity 0.8s ease';
                    bar.style.opacity = '0';
                    setTimeout(function() {
                        bar.remove();
                        host.classList.remove('sls-bar-host');
                    }, 850);
                }, 2200);
            }
        }

        // Crawl the bar a tiny bit every tick so it always looks alive
        function crawl() {
            if (finished) return;
            currentPct = parseFloat(bar.style.width) || 0;
            // Slow organic crawl up to targetPct — max 0.18% per tick
            if (currentPct < targetPct) {
                var step = Math.max(0.05, (targetPct - currentPct) * 0.04);
                currentPct = Math.min(targetPct, currentPct + step);
                bar.style.width = currentPct + '%';
            }
        }
        var crawlId = setInterval(crawl, 80);

        // Start at 2% immediately so the bar is visible right away
        setTarget(2);

        pollId = setInterval(function() {
            var span = luaLink.querySelector('span');
            var text = span ? span.innerText : '';
            if (text === lastText) return;
            lastText = text;

            if (SLS_FAIL_TEXTS.test(text)) {
                clearInterval(crawlId);
                finish(false);
                return;
            }
            if (/installed!?/i.test(text)) {
                clearInterval(crawlId);
                finish(true);
                return;
            }
            for (var i = 0; i < SLS_PHASE_MAP.length; i++) {
                if (SLS_PHASE_MAP[i].match.test(text)) {
                    setTarget(SLS_PHASE_MAP[i].pct);
                    break;
                }
            }
        }, 150);
    }

    var globalDownloadSessions = {};

    function saveSessionToStorage(appid) {
        var session = globalDownloadSessions[appid];
        if (!session) return;
        try {
            var dataToSave = {
                requestedProviders: session.requestedProviders,
                providerStates: session.providerStates,
                allowedIndices: session.allowedIndices,
                currentActiveIdx: session.currentActiveIdx,
                targetPct: session.targetPct,
                finished: session.finished
            };
            localStorage.setItem('sls-session-' + appid, JSON.stringify(dataToSave));
        } catch(e) {}
    }

    function loadSessionFromStorage(appid) {
        try {
            var raw = localStorage.getItem('sls-session-' + appid);
            if (!raw) return null;
            var data = JSON.parse(raw);
            return data;
        } catch(e) {
            return null;
        }
    }

    function getOrCreateGlobalSession(appid) {
        if (!globalDownloadSessions[appid]) {
            var saved = loadSessionFromStorage(appid);
            if (saved) {
                globalDownloadSessions[appid] = {
                    requestedProviders: saved.requestedProviders || {},
                    providerStates: saved.providerStates || {},
                    allowedIndices: saved.allowedIndices || [],
                    currentActiveIdx: typeof saved.currentActiveIdx === 'number' ? saved.currentActiveIdx : -1,
                    targetPct: saved.targetPct || 0,
                    finished: !!saved.finished,
                    lastText: '',
                    pollId: null,
                    crawlId: null
                };
            } else {
                globalDownloadSessions[appid] = {
                    requestedProviders: {},
                    providerStates: {},
                    allowedIndices: [],
                    currentActiveIdx: -1,
                    targetPct: 0,
                    finished: false,
                    lastText: '',
                    pollId: null,
                    crawlId: null
                };
            }
        }
        return globalDownloadSessions[appid];
    }

    function syncModalUI(appid) {
        var overlay = document.getElementById('sls-overlay-modal');
        if (!overlay || overlay.dataset.slsAppid !== String(appid)) return;
        var session = globalDownloadSessions[appid];
        if (!session) return;

        Object.keys(session.requestedProviders).forEach(function(pIdxStr) {
            var pIdx = parseInt(pIdxStr, 10);
            var btn = overlay.querySelector('[data-sls-provider-download="' + pIdx + '"]');
            if (btn) {
                // btn.disabled = true;
                // btn.style.opacity = '0.4';
                // btn.style.cursor = 'not-allowed';
            }
        });

        Object.keys(session.providerStates).forEach(function(pIdxStr) {
            var pIdx = parseInt(pIdxStr, 10);
            var state = session.providerStates[pIdxStr];
            var rowEl = overlay.querySelector('[data-sls-provider="' + pIdx + '"]');
            if (!rowEl) return;
            var bar = rowEl.querySelector('.sls-bar-fill');
            if (!bar) {
                injectLoadingBarStyles();
                bar = document.createElement('div');
                bar.className = 'sls-bar-fill';
                rowEl.insertBefore(bar, rowEl.firstChild);
            }
            bar.style.width = state.pct + '%';
            if (state.status === 'done') {
                bar.classList.remove('sls-bar-fail');
                bar.classList.add('sls-bar-done');
            } else if (state.status === 'fail') {
                bar.classList.remove('sls-bar-done');
                bar.classList.add('sls-bar-fail');
            } else {
                bar.classList.remove('sls-bar-done', 'sls-bar-fail');
            }
        });
    }

    function findProviderIndexFromText(text) {
        if (!text) return -1;
        var lower = text.toLowerCase();
        for (var i = 0; i < luaProviders.length; i++) {
            var keyName = luaProviders[i].name.split(' ')[0].toLowerCase();
            if (lower.indexOf(keyName) !== -1) {
                return i;
            }
        }
        return -1;
    }

    function startGlobalDownloadTracking(appid, newAllowedIndices) {
        injectLoadingBarStyles();
        var session = getOrCreateGlobalSession(appid);
        session.allowedIndices = newAllowedIndices || session.allowedIndices;

        if (newAllowedIndices) {
            newAllowedIndices.forEach(function(pIdx) {
                session.requestedProviders[pIdx] = true;
            });
        }

        if (session.currentActiveIdx === -1 || session.finished) {
            if (session.allowedIndices && session.allowedIndices.length > 0) {
                session.currentActiveIdx = session.allowedIndices[0];
            }
            session.targetPct = 5;
            session.finished = false;
            if (session.currentActiveIdx !== -1) {
                session.providerStates[session.currentActiveIdx] = { status: 'active', pct: 5 };
            }
        }

        saveSessionToStorage(appid);
        syncModalUI(appid);

        if (session.crawlId) clearInterval(session.crawlId);
        session.crawlId = setInterval(function() {
            if (session.finished || session.currentActiveIdx === -1) return;
            var st = session.providerStates[session.currentActiveIdx];
            if (!st) return;
            if (st.pct < session.targetPct) {
                var step = Math.max(0.05, (session.targetPct - st.pct) * 0.04);
                st.pct = Math.min(session.targetPct, st.pct + step);
                saveSessionToStorage(appid);
                syncModalUI(appid);
            }
        }, 80);

        if (session.pollId) clearInterval(session.pollId);
        session.pollId = setInterval(function() {
            var pageBtn = document.querySelector('.sls-lua-btn[data-sls-appid="' + appid + '"]');
            var text = pageBtn ? (pageBtn.dataset.slsStatus || (pageBtn.querySelector('a span') ? pageBtn.querySelector('a span').innerText : '')) : '';
            if (text === session.lastText) return;
            session.lastText = text;

            if (SLS_FAIL_TEXTS.test(text)) {
                finishGlobalTracking(appid, false);
                return;
            }
            if (/installed!?/i.test(text)) {
                finishGlobalTracking(appid, true);
                return;
            }

            var allowed = session.allowedIndices || [];
            var foundIdx = findProviderIndexFromText(text);
            if (foundIdx !== -1 && allowed.indexOf(foundIdx) !== -1) {
                if (session.currentActiveIdx !== -1 && session.currentActiveIdx !== foundIdx) {
                    session.providerStates[session.currentActiveIdx] = { status: 'fail', pct: 100 };
                    session.targetPct = 5;
                }
                session.currentActiveIdx = foundIdx;
                session.targetPct = Math.max(session.targetPct, 35);
                if (!session.providerStates[foundIdx]) {
                    session.providerStates[foundIdx] = { status: 'active', pct: 5 };
                } else if (session.providerStates[foundIdx].pct < 5) {
                    session.providerStates[foundIdx].pct = 5;
                }
                session.providerStates[foundIdx].status = 'active';
            }

            for (var i = 0; i < SLS_PHASE_MAP.length; i++) {
                if (SLS_PHASE_MAP[i].match.test(text)) {
                    if (SLS_PHASE_MAP[i].pct > session.targetPct) {
                        session.targetPct = SLS_PHASE_MAP[i].pct;
                    }
                    break;
                }
            }
            saveSessionToStorage(appid);
            syncModalUI(appid);
        }, 150);
    }

    function finishGlobalTracking(appid, success) {
        var session = globalDownloadSessions[appid];
        if (!session || session.finished) return;
        session.finished = true;
        if (session.crawlId) clearInterval(session.crawlId);
        if (session.pollId) clearInterval(session.pollId);

        var allowed = session.allowedIndices || [];
        if (success) {
            if (session.currentActiveIdx !== -1) {
                session.providerStates[session.currentActiveIdx] = {
                    status: 'done',
                    pct: 100
                };
            }
        } else {
            allowed.forEach(function(pIdx) {
                session.providerStates[pIdx] = {
                    status: 'fail',
                    pct: 100
                };
            });
            if (session.currentActiveIdx !== -1) {
                session.providerStates[session.currentActiveIdx] = {
                    status: 'fail',
                    pct: 100
                };
            }
        }
        saveSessionToStorage(appid);
        syncModalUI(appid);
    }

    function clearAppCache(appid, btnEl) {
        try {
            localStorage.removeItem('sls-session-' + appid);
        } catch (e) {}

        if (globalDownloadSessions[appid]) {
            var session = globalDownloadSessions[appid];
            if (session.crawlId) clearInterval(session.crawlId);
            if (session.pollId) clearInterval(session.pollId);
            session.requestedProviders = {};
            session.providerStates = {};
            session.allowedIndices = [];
            session.currentActiveIdx = -1;
            session.targetPct = 0;
            session.finished = false;
        }

        var modal = document.getElementById('sls-overlay-modal');
        if (modal) {
            modal.querySelectorAll('.sls-bar-fill').forEach(function(el) {
                el.remove();
            });
        }

        delete appUnlockStatus[appid];

        naviFetch('http://127.0.0.1:9001/clear-cache?id=' + appid).catch(function() {});

        if (btnEl) {
            var origText = btnEl.innerText;
            btnEl.innerText = 'App Cache Cleared!';
            setTimeout(function() {
                btnEl.innerText = origText;
            }, 1500);
        }
    }

    function clearAllCache(appid, btnEl) {
        try {
            var keysToRemove = [];
            for (var i = 0; i < localStorage.length; i++) {
                var k = localStorage.key(i);
                if (k && k.indexOf('sls-session-') === 0) {
                    keysToRemove.push(k);
                }
            }
            keysToRemove.forEach(function(k) {
                localStorage.removeItem(k);
            });
        } catch (e) {}

        Object.keys(globalDownloadSessions).forEach(function(id) {
            var session = globalDownloadSessions[id];
            if (session) {
                if (session.crawlId) clearInterval(session.crawlId);
                if (session.pollId) clearInterval(session.pollId);
            }
        });
        globalDownloadSessions = {};

        var modal = document.getElementById('sls-overlay-modal');
        if (modal) {
            modal.querySelectorAll('.sls-bar-fill').forEach(function(el) {
                el.remove();
            });
        }

        appUnlockStatus = {};

        naviFetch('http://127.0.0.1:9001/clear-all-cache').catch(function() {});

        if (btnEl) {
            var origText = btnEl.innerText;
            btnEl.innerText = 'All Cache Cleared!';
            setTimeout(function() {
                btnEl.innerText = origText;
            }, 1500);
        }
    }

    /**
     * Called by the C++ side via CDP injection when a download completes or
     * fails while the store page is open (i.e. pushStatus calls come through
     * as innerText changes on the button span).  The loading bar already polls
     * those text changes via startRowLoadingBar — no extra wiring needed here.
     * This hook exists so any future direct-JS callers can still trigger bars.
     */
    function slsTriggerLoadingBar(productID) {
        var btn  = document.querySelector('.sls-lua-btn[data-sls-appid="' + productID + '"]');
        var link = btn ? btn.querySelector('a') : null;
        if (btn && link) startRowLoadingBar(btn, link);
    }
    window.slsTriggerLoadingBar = slsTriggerLoadingBar;

    // --- modals.js ---
    /**
     * @file  modals.js
     * @brief Modal controllers for Lua provider configuration, removal confirmation, and API credentials
     */
    function openLuaProviderConfig(appid) {
        if (document.getElementById('sls-overlay-modal')) return;
        ensureStoreStyles();
        var overlay = document.createElement('div');
        overlay.id = 'sls-overlay-modal';
        overlay.setAttribute('role', 'dialog');
        overlay.setAttribute('aria-modal', 'true');
        overlay.setAttribute('data-testid', 'sls-overlay-modal');
        overlay.dataset.slsAppid = String(appid);
        overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(10,12,18,0.85);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);';
        var order = getProviderOrder();
        var session = getOrCreateGlobalSession(appid);
        var requestedProviders = session.requestedProviders;

        var rows = '';
        order.forEach(function(providerIndex, position) {
            var provider = luaProviders[providerIndex];
            var apiKey = configApiKeys[provider.key] || '';
            var getKeyBtn = (provider.url && provider.url !== '#') ?
                '<button data-sls-getkey="' + providerIndex + '" class="Focusable" tabindex="-1" role="button" title="Get API Key" style="color:#dcdedf;font-size:12px;margin-left:4px;padding:2px 8px;border:1px solid #3d4450;border-radius:2px;background:rgba(172,178,201,0.14);cursor:pointer;white-space:nowrap;flex-shrink:0;">Get Key</button>' : '';

            rows += '<div data-sls-provider="' + providerIndex + '" style="position:relative;overflow:hidden;display:flex;align-items:center;gap:8px;padding:10px 12px;background:rgba(0,0,0,0.2);border:1px solid rgba(172,178,201,0.1);border-radius:4px;flex-shrink:0;box-sizing:border-box;">' +
                '<strong style="padding-left:4px;position:relative;z-index:1;flex:1;min-width:70px;color:#dcdedf;font-size:15px;font-family:\'Motiva Sans\', sans-serif;font-weight:400;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">' + provider.name + '</strong>' +
                '<div style="position:relative;z-index:1;display:flex;align-items:center;gap:6px;min-width:180px;max-width:240px;flex:1.2;border:1px solid #3d4450;border-radius:2px;padding:3px 8px;background:rgba(0,0,0,0.4);box-sizing:border-box;">' +
                '<span title="API key" aria-hidden="true" style="color:#969696;font-size:14px;flex-shrink:0;">&#128273;</span>' +
                '<input data-sls-api-key="' + providerIndex + '" aria-label="API key for ' + escapeHtml(provider.name) + '" value="' + escapeHtml(apiKey) + '" placeholder="API key" type="text" style="min-width:0;flex:1;background:transparent;border:0;outline:0;color:#dcdedf;padding:4px 2px;font-size:13px;font-family:\'Motiva Sans\', sans-serif;" />' +
                getKeyBtn +
                '</div>' +
                '<div style="display:flex;gap:4px;position:relative;z-index:1;flex-shrink:0;">' +
                '<button data-sls-up="' + providerIndex + '" class="sls-btn-action Focusable" tabindex="-1" role="button" title="Move provider up" style="height:32px;padding:0 8px;font-size:12px;"' + (position === 0 ? ' disabled' : '') + '>Up</button>' +
                '<button data-sls-down="' + providerIndex + '" class="sls-btn-action Focusable" tabindex="-1" role="button" title="Move provider down" style="height:32px;padding:0 8px;font-size:12px;"' + (position === order.length - 1 ? ' disabled' : '') + '>Down</button>' +
                '</div>' +
                '<button data-sls-provider-download="' + providerIndex + '" class="sls-btn-action Focusable" tabindex="-1" role="button" style="position:relative;z-index:1;height:32px;padding:0 14px;font-size:13px;background:#06bfff;color:#fff;flex-shrink:0;">Download</button>' +
                '</div>';
        });
        overlay.innerHTML = PROVIDER_MODAL_HTML
            .replace('{{APPID}}', appid)
            .replace('{{PROVIDER_ROWS}}', rows);
        document.body.appendChild(overlay);
        syncModalUI(appid);

        fetchConfigApiKeys(function(keys) {
            overlay.querySelectorAll('[data-sls-api-key]').forEach(function(input) {
                if (document.activeElement === input) return;
                var pIdx = parseInt(input.getAttribute('data-sls-api-key'), 10);
                var prov = luaProviders[pIdx];
                if (prov && keys[prov.key] !== undefined) {
                    input.value = keys[prov.key] || '';
                }
            });
        });

        var clearAppBtn = document.getElementById('sls-clear-app-cache');
        var clearAllBtn = document.getElementById('sls-clear-all-cache');
        var autoSearchBtn = document.getElementById('sls-auto-search');

        // Controller support: collect interactive elements into a 2D grid
        function buildModalButtons() {
            var closeBtnEl = document.getElementById('sls-provider-close');
            var modalButtons = [
                closeBtnEl ? [closeBtnEl] : []
            ];
            overlay.querySelectorAll('[data-sls-provider]').forEach(function(row) {
                var rowItems = [];
                row.querySelectorAll('[data-sls-api-key], [data-sls-getkey], [data-sls-up], [data-sls-down], [data-sls-provider-download]').forEach(function(b) { rowItems.push(b); });
                if (rowItems.length > 0) modalButtons.push(rowItems);
            });
            var actionRow = [];
            if (clearAppBtn) actionRow.push(clearAppBtn);
            if (clearAllBtn) actionRow.push(clearAllBtn);
            if (autoSearchBtn) actionRow.push(autoSearchBtn);
            if (actionRow.length > 0) modalButtons.push(actionRow);
            return modalButtons;
        }
        trapSteamGamepadModal(overlay, buildModalButtons());

        if (clearAppBtn) {
            clearAppBtn.onclick = function() {
                clearAppCache(appid, clearAppBtn);
            };
        }
        if (clearAllBtn) {
            clearAllBtn.onclick = function() {
                clearAllCache(appid, clearAllBtn);
            };
        }

        function close() {
            if (overlay.__slsCleanup) overlay.__slsCleanup();
            overlay.remove();
        }
        var closeBtn = document.getElementById('sls-provider-close');
        if (closeBtn) {
            closeBtn.onclick = close;
            closeBtn.addEventListener('click', close);
        }
        var cancelButton = document.getElementById('sls-provider-cancel');
        if (cancelButton) {
            cancelButton.onclick = close;
            cancelButton.addEventListener('click', close);
        }

        function disableProviderBtn(pIdx) {
            var btn = overlay.querySelector('[data-sls-provider-download="' + pIdx + '"]');
            if (btn) {
                // btn.disabled = true;
                // btn.style.opacity = '0.4';
                // btn.style.cursor = 'not-allowed';
            }
        }

        // Auto Download — try only unrequested providers sequentially
        document.getElementById('sls-auto-search').onclick = function() {
            var fullOrder = getProviderOrder();
            var remainingOrder = fullOrder.filter(function(pIdx) {
                return !requestedProviders[pIdx];
            });

            if (remainingOrder.length === 0) return;

            startGlobalDownloadTracking(appid, remainingOrder);
            sendLuaRequest(appid, null, remainingOrder);
        };

        overlay.querySelectorAll('[data-sls-api-key]').forEach(function(input) {
            function saveApiKey() {
                var provider = luaProviders[parseInt(input.getAttribute('data-sls-api-key'), 10)];
                if (provider) {
                    configApiKeys[provider.key] = input.value;
                    sendApiKeys();
                }
            }
            input.addEventListener('blur', saveApiKey);
            input.addEventListener('keydown', function(event) {
                if (event.key === 'Enter') {
                    event.preventDefault();
                    saveApiKey();
                    input.blur();
                }
            });
        });
        overlay.querySelectorAll('[data-sls-up], [data-sls-down]').forEach(function(button) {
            button.onclick = function() {
                var isUp = button.hasAttribute('data-sls-up');
                var rowEl = button.closest ? button.closest('[data-sls-provider]') : button.parentElement;
                if (!rowEl) return;
                var container = rowEl.parentElement;
                if (!container) return;

                if (isUp) {
                    var prev = rowEl.previousElementSibling;
                    if (prev) container.insertBefore(rowEl, prev);
                } else {
                    var next = rowEl.nextElementSibling;
                    if (next) container.insertBefore(next, rowEl);
                }

                var allRows = Array.from(container.children);
                var newOrder = allRows.map(function(child) {
                    return parseInt(child.getAttribute('data-sls-provider'), 10);
                });
                saveProviderOrder(newOrder);

                allRows.forEach(function(child, idx) {
                    var upBtn = child.querySelector('[data-sls-up]');
                    var downBtn = child.querySelector('[data-sls-down]');
                    if (upBtn) upBtn.disabled = (idx === 0);
                    if (downBtn) downBtn.disabled = (idx === allRows.length - 1);
                });

                if (overlay.__slsUpdateGamepadGrid) {
                    overlay.__slsUpdateGamepadGrid(buildModalButtons(), button);
                }
            };
        });
        // Single provider download — block only the requested provider, modal stays open
        overlay.querySelectorAll('[data-sls-provider-download]').forEach(function(button) {
            button.onclick = function() {
                var pIdx = parseInt(button.getAttribute('data-sls-provider-download'), 10);
                // if (requestedProviders[pIdx]) return;

                startGlobalDownloadTracking(appid, [pIdx]);
                sendLuaRequest(appid, pIdx);
            };
        });
        // Open provider page in a new Steam browser tab to get an API key
        overlay.querySelectorAll('[data-sls-getkey]').forEach(function(button) {
            button.onclick = function(e) {
                e.preventDefault();
                e.stopPropagation();
                var pIdx = parseInt(button.getAttribute('data-sls-getkey'), 10);
                var provider = luaProviders[pIdx];
                if (provider && provider.url && provider.url !== '#') {
                    window.location.href = provider.url;
                }
            };
        });
    }

    function setupRemoveButton(luaLink, luaBtn, productID, cartBtn) {
        var span = luaLink.querySelector('span');
        if (span) span.innerText = 'Already installed';
        luaLink.style.filter = 'hue-rotate(320deg) brightness(1.1)';
        luaLink.style.pointerEvents = '';
        luaLink.style.opacity = '';
        luaLink.onclick = function(e) {
            e.preventDefault();
            e.stopPropagation();

            // Show confirmation modal
            if (document.getElementById('sls-remove-overlay')) return;
            ensureStoreStyles();
            var overlay = document.createElement('div');
            overlay.id = 'sls-remove-overlay';
            overlay.setAttribute('role', 'dialog');
            overlay.setAttribute('aria-modal', 'true');
            overlay.setAttribute('data-testid', 'sls-remove-overlay');
            overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.8);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);';
            overlay.innerHTML = REMOVE_MODAL_HTML.replace(/\{\{APPID\}\}/g, productID);
            document.body.appendChild(overlay);

            var rmClearAppBtn = document.getElementById('sls-rm-clear-app-cache');
            var rmClearAllBtn = document.getElementById('sls-rm-clear-all-cache');
            var rmCancelBtn = document.getElementById('sls-rm-cancel');
            var rmConfirmBtn = document.getElementById('sls-rm-confirm');
            trapSteamGamepadModal(overlay, [
                [rmClearAppBtn, rmClearAllBtn],
                [rmCancelBtn, rmConfirmBtn]
            ]);

            if (rmClearAppBtn) {
                rmClearAppBtn.onclick = function() {
                    clearAppCache(productID, rmClearAppBtn);
                };
            }
            if (rmClearAllBtn) {
                rmClearAllBtn.onclick = function() {
                    clearAllCache(productID, rmClearAllBtn);
                };
            }

            rmCancelBtn.onclick = function() { overlay.remove(); };
            rmConfirmBtn.onclick = function() {
                rmConfirmBtn.innerText = 'Processing...';
                rmConfirmBtn.style.opacity = '0.5';
                // confirmBtn.style.pointerEvents = 'none';

                ping('Remove Lua: ' + productID);
                naviRequest('/remove', { id: productID, game: 'true' }).catch(function() {});

                // Update cached status
                appUnlockStatus[productID] = { exists: false, pending: false, onlineFixInstalled: false, autoCrackInstalled: false };

                overlay.remove();

                // Switch button back to Download Lua
                setupDownloadButton(luaLink, luaBtn, productID, cartBtn);

            };
        };
    }

    function openSlsConfig(appid) {
        if (document.getElementById('sls-overlay-modal')) return;
        ensureStoreStyles();
        var overlay = document.createElement('div');
        overlay.id = 'sls-overlay-modal';
        overlay.setAttribute('role', 'dialog');
        overlay.setAttribute('aria-modal', 'true');
        overlay.setAttribute('data-testid', 'sls-overlay-modal');
        overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.8);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);transition:all 0.3s ease;opacity:0;';
        var currentRyuu = configApiKeys['sls-ryuu-key'] || '';
        var currentDpbx = configApiKeys['sls-dpbx-key'] || '';
        var currentHubcap = configApiKeys['sls-hubcap-key'] || '';

        overlay.innerHTML = CONFIG_MODAL_HTML
            .replace('{{APPID}}', appid)
            .replace('{{HUBCAP_KEY}}', currentHubcap)
            .replace('{{RYUU_KEY}}', currentRyuu)
            .replace('{{DPBX_KEY}}', currentDpbx);

        document.body.appendChild(overlay);

        fetchConfigApiKeys(function(keys) {
            var elH = document.getElementById('sls-hubcap');
            var elR = document.getElementById('sls-ryuu');
            var elD = document.getElementById('sls-dpbx');
            if (elH && document.activeElement !== elH) elH.value = keys['sls-hubcap-key'] || '';
            if (elR && document.activeElement !== elR) elR.value = keys['sls-ryuu-key'] || '';
            if (elD && document.activeElement !== elD) elD.value = keys['sls-dpbx-key'] || '';
        });

        requestAnimationFrame(function() {
            overlay.style.opacity = '1';
            var card = document.getElementById('sls-modal-card');
            if (card) {
                card.style.transform = 'scale(1)';
                card.style.opacity = '1';
            }
        });

        var closeX = document.getElementById('sls-close-x');
        var cancelBtn = document.getElementById('sls-btn-cancel');
        var saveBtn = document.getElementById('sls-btn-save');

        function close() {
            overlay.style.opacity = '0';
            var card = document.getElementById('sls-modal-card');
            if (card) {
                card.style.transform = 'scale(0.95)';
                card.style.opacity = '0';
            }
            setTimeout(function() { overlay.remove(); }, 300);
        }
        closeX.onclick = close;
        cancelBtn.onclick = close;

        trapSteamGamepadModal(overlay, [closeX, saveBtn, cancelBtn]);

        saveBtn.onclick = function() {
            var hubcap = document.getElementById('sls-hubcap').value;
            var ryuu = document.getElementById('sls-ryuu').value;
            var dpbx = document.getElementById('sls-dpbx').value;
            configApiKeys['sls-hubcap-key'] = hubcap;
            configApiKeys['sls-ryuu-key'] = ryuu;
            configApiKeys['sls-dpbx-key'] = dpbx;
            sendApiKeys();
            close();

            var toast = document.createElement('div');
            toast.innerText = 'API Settings Saved!';
            toast.style.cssText = 'position:fixed;bottom:30px;right:30px;background:#28a745;color:#fff;padding:12px 20px;border-radius:8px;font-family:Inter,sans-serif;font-weight:500;z-index:999999;box-shadow:0 5px 15px rgba(0,0,0,0.3);transition:opacity 0.5s;';
            document.body.appendChild(toast);
            setTimeout(function(){ toast.style.opacity = '0'; }, 2000);
            setTimeout(function(){ toast.remove(); }, 2500);
        };
    }

    // --- buttons.js ---
    /**
     * @file  buttons.js
     * @brief Store page action button injection and controller setup for desktop and Big Picture modes
     */
    function setupDownloadButton(luaLink, luaBtn, productID, cartBtn) {
        var span = luaLink.querySelector('span');
        if (span) span.innerText = 'Download Lua';
        luaLink.style.filter = 'hue-rotate(110deg) brightness(1.2)';
        luaLink.style.pointerEvents = '';
        luaLink.style.opacity = '';
        luaLink.onclick = function(e) {
            e.preventDefault();
            e.stopPropagation();
            luaBtn.dataset.slsAppid = productID;
            ping('Lua Click: ' + productID);
            openLuaProviderConfig(productID);
        };
    }

    function addButtons() {
        if (observer) observer.disconnect();

        // Big Picture Mode detection
        var bpSummary = document.getElementById('summaryBarTop');
        var match = window.location.href.match(/\/(app|sub)\/([0-9]+)/);

        if (bpSummary && match) {
            var productID = match[2];
            if (!document.getElementById('sls-bp-lua-btn-container-' + productID)) {
                var bpContainer = document.createElement('div');
                bpContainer.id = 'sls-bp-lua-btn-container-' + productID;
                bpContainer.style.cssText = 'display: flex; justify-content: center; z-index: 100; position: relative; margin: 12px 0px 12px 0px; width: 100%; box-sizing: border-box;';

                var bpBtn = document.createElement('button');
                bpBtn.id = 'sls-bp-lua-btn-' + productID;
                bpBtn.className = 'Focusable sls-lua-btn';
                bpBtn.dataset.slsProcessed = '1';
                bpBtn.dataset.slsAppid = productID;
                bpBtn.style.cssText = 'background: linear-gradient(135deg, #1a9fff 0%, #0073d4 100%); border: none; color: white; padding: 12px 30px; font-size: 18px; font-weight: bold; border-radius: 6px; cursor: pointer; box-shadow: 0 4px 15px rgba(0,0,0,0.4); text-transform: uppercase; transition: all 0.2s; width: 100%; display: flex; justify-content: center; box-sizing: border-box;';

                var bpLink = document.createElement('a');
                bpLink.style.display = 'block';
                bpLink.style.color = 'white';
                bpLink.style.textDecoration = 'none';

                var bpSpan = document.createElement('span');
                bpSpan.innerText = 'Checking Lua...';

                bpLink.appendChild(bpSpan);
                bpBtn.appendChild(bpLink);
                bpContainer.appendChild(bpBtn);

                bpSummary.parentNode.insertBefore(bpContainer, bpSummary);

                bpLink.onclick = function(e) {
                    e.preventDefault();
                    e.stopPropagation();
                    ping('Lua Click (early): ' + productID);
                    openLuaProviderConfig(productID);
                };

                // Check unlock status via callback server
                if (appUnlockStatus[productID] !== undefined) {
                    var cached = appUnlockStatus[productID];
                    if (cached && cached.exists) {
                        setupRemoveButton(bpLink, bpBtn, productID, null);
                    } else {
                        setupDownloadButton(bpLink, bpBtn, productID, null);
                    }
                } else {
                    (function(ll, lb, pid) {
                        var controller = new AbortController();
                        var timeoutId = setTimeout(function() { controller.abort(); }, 5000);
                        naviFetch('http://127.0.0.1:9001/check?id=' + pid, { signal: controller.signal })
                            .then(function(r) { clearTimeout(timeoutId); return r.json(); })
                            .then(function(data) {
                                appUnlockStatus[pid] = data;
                                var isUnlocked = data.exists;
                                if (isUnlocked) {
                                    setupRemoveButton(ll, lb, pid, null);
                                } else {
                                    setupDownloadButton(ll, lb, pid, null);
                                }
                            })
                            .catch(function() {
                                clearTimeout(timeoutId);
                                ping('Check failed for ' + pid + ', defaulting to Download');
                                setupDownloadButton(ll, lb, pid, null);
                            });
                    })(bpLink, bpBtn, productID);
                }

                // Register for gamepad navigation
                setTimeout(function() {
                    registerSteamFocusNode(bpBtn);
                    bpBtn.addEventListener('vgp_onfocus', function() {
                        bpBtn.style.outline = '4px solid #fff';
                        bpBtn.style.outlineOffset = '2px';
                        bpBtn.style.transform = 'scale(1.05)';
                    });
                    bpBtn.addEventListener('vgp_onblur', function() {
                        bpBtn.style.outline = 'none';
                        bpBtn.style.transform = 'scale(1)';
                    });
                    bpBtn.addEventListener('vgp_onok', function(e) {
                        e.preventDefault(); e.stopPropagation();
                        bpLink.click();
                    });
                }, 100);
            }
        }

        var desktopMatch = window.location.href.match(/\/(app|sub)\/([0-9]+)/);
        var curProductId = desktopMatch ? desktopMatch[2] : null;
        var bpBtnContainer = curProductId ? document.getElementById('sls-bp-lua-btn-container-' + curProductId) : null;
        if (bpBtnContainer && bpBtnContainer.isConnected) {
            var rogueDesktopBtn = document.querySelector('.btn_addtocart.sls-lua-btn');
            if (rogueDesktopBtn) rogueDesktopBtn.remove();
        }

        if (!bpBtnContainer && desktopMatch && !document.querySelector('.sls-lua-btn[data-sls-appid="' + curProductId + '"]')) {
            var productID = curProductId;
            var cartBtn = null;
            var cartBtns = document.querySelectorAll('.btn_addtocart, .btn_add_to_cart');
            for (var i = 0; i < cartBtns.length; i++) {
                var c = cartBtns[i];
                var link = c.querySelector('a');
                if (!link) continue;
                var hrefLower = link.href.toLowerCase();
                if (hrefLower.indexOf('bundle') !== -1 || hrefLower.indexOf('dlc') !== -1) continue;
                cartBtn = c;
                break;
            }

            var luaBtn;
            if (cartBtn) {
                cartBtn.dataset.slsProcessed = '1';
            }
            luaBtn = document.createElement('div');
            luaBtn.className = 'btn_addtocart';
            luaBtn.innerHTML = '<a class="btn_green_steamui btn_medium" href="javascript:void(0)"><span>Checking Lua...</span></a>';

            luaBtn.classList.add('sls-lua-btn');
            luaBtn.dataset.slsProcessed = '1';
            luaBtn.dataset.slsAppid = productID;
            luaBtn.style.display = 'inline-block';
            luaBtn.style.marginRight = '4px';
            luaBtn.style.float = 'right';

            var luaLink = luaBtn.querySelector('a');
            if (luaLink) {
                luaLink.href = 'javascript:void(0)';
                luaLink.removeAttribute('id');

                // Avoid showing the download action until the installed state is known.
                var initialSpan = luaLink.querySelector('span');
                if (initialSpan) initialSpan.innerText = 'Checking Lua...';
                luaLink.style.filter = 'hue-rotate(200deg) brightness(1.0)';

                // Allow clicking even while checking
                luaLink.onclick = function(e) {
                    e.preventDefault();
                    e.stopPropagation();
                    luaBtn.dataset.slsAppid = productID;
                    ping('Lua Click (early): ' + productID);
                    openLuaProviderConfig(productID);
                };

                // Check unlock status via callback server
                if (appUnlockStatus[productID] !== undefined) {
                    var cached = appUnlockStatus[productID];
                    if (cached && cached.exists) {
                        setupRemoveButton(luaLink, luaBtn, productID, cartBtn);
                    } else {
                        setupDownloadButton(luaLink, luaBtn, productID, cartBtn);
                    }
                } else {
                    // Query the server with a short timeout
                    (function(ll, lb, pid, cb) {
                        var controller = new AbortController();
                        var timeoutId = setTimeout(function() { controller.abort(); }, 5000);
                        naviFetch('http://127.0.0.1:9001/check?id=' + pid, { signal: controller.signal })
                            .then(function(r) { clearTimeout(timeoutId); return r.json(); })
                            .then(function(data) {
                                appUnlockStatus[pid] = data;
                                var isUnlocked = data.exists;
                                if (isUnlocked) {
                                    setupRemoveButton(ll, lb, pid, cb);
                                } else {
                                    setupDownloadButton(ll, lb, pid, cb);
                                }
                            })
                            .catch(function() {
                                clearTimeout(timeoutId);
                                ping('Check failed for ' + pid + ', defaulting to Download');
                                setupDownloadButton(ll, lb, pid, cb);
                            });
                    })(luaLink, luaBtn, productID, cartBtn);
                }
            }

            var header = document.querySelector('div.apphub_HeaderStandardTop') || document.querySelector('.apphub_HeaderStandardTop');
            if (header && header.offsetParent !== null) {
                var appName = null;
                for (var childIndex = 0; childIndex < header.children.length; childIndex++) {
                    if (header.children[childIndex].classList.contains('apphub_AppName')) {
                        appName = header.children[childIndex];
                        break;
                    }
                }
                var insertionPoint = appName || header.firstChild;
                header.insertBefore(luaBtn, insertionPoint);
            } else if (cartBtn && cartBtn.parentNode) {
                cartBtn.parentNode.insertBefore(luaBtn, cartBtn);
            } else {
                var purchaseArea = document.getElementById('game_area_purchase') || document.querySelector('.game_area_purchase') || document.querySelector('.game_area_comingsoon') || document.querySelector('.game_bg') || document.querySelector('.rightcol');
                if (purchaseArea) {
                    luaBtn.style.float = 'none';
                    luaBtn.style.marginBottom = '15px';
                    purchaseArea.prepend(luaBtn);
                }
            }

            // Register for gamepad navigation
            setTimeout(function() {
                registerSteamFocusNode(luaBtn);
                luaBtn.addEventListener('vgp_onfocus', function() {
                    luaBtn.style.outline = '3px solid #fff';
                    luaBtn.style.outlineOffset = '2px';
                });
                luaBtn.addEventListener('vgp_onblur', function() {
                    luaBtn.style.outline = 'none';
                });
                luaBtn.addEventListener('vgp_onok', function(e) {
                    e.preventDefault(); e.stopPropagation();
                    var link = luaBtn.querySelector('a');
                    if (link) link.click();
                });
            }, 100);
        }

        // Auto-resume download tracking if there is an active session for this product
        var pageMatch = window.location.href.match(/\/(app|sub)\/([0-9]+)/);
        if (pageMatch && pageMatch[2]) {
            var currentAppid = pageMatch[2];
            var saved = loadSessionFromStorage(currentAppid);
            if (saved && !saved.finished) {
                startGlobalDownloadTracking(currentAppid, null);
            }
        }

        if (observer && document.documentElement) observer.observe(document.documentElement, { childList: true, subtree: true });
    }

    // --- ownership-decoration.js ---
    /**
     * @file  ownership-decoration.js
     * @brief Lua library ownership synchronization and GDynamicStore 'IN LIBRARY' decoration in store pages
     */
    // Test mode flag: when true, every item/capsule on the page receives the "IN LIBRARY" label.
    // Can also be toggled at runtime via window.__slsIsTesting.
    var isTesting = false;

    function getIsTesting() {
        if (typeof window !== 'undefined' && typeof window.__slsIsTesting === 'boolean') {
            return window.__slsIsTesting;
        }
        return (typeof isTesting !== 'undefined') ? isTesting : false;
    }

    var slsLuaAppIds = (typeof slsLuaAppIds !== 'undefined') ? slsLuaAppIds : new Set();
    var slsLuaAppIdsLoaded = (typeof slsLuaAppIdsLoaded !== 'undefined') ? slsLuaAppIdsLoaded : false;

    function cleanupLegacyHiding() {
        try {
            sessionStorage.removeItem('sls_steam_hide_owned');
        } catch(e) {}

        var legacyStyle = document.getElementById('sls-hide-lua-library');
        if (legacyStyle && legacyStyle.parentNode) {
            legacyStyle.parentNode.removeChild(legacyStyle);
        }

        var legacyFlags = document.querySelectorAll('.sls-in-library-flag');
        for (var fi = 0; fi < legacyFlags.length; fi++) {
            var flagEl = legacyFlags[fi];
            var flagParent = flagEl.parentElement;
            if (flagParent && flagParent.classList) {
                flagParent.classList.remove('ds_flagged', 'ds_owned');
            }
            if (flagEl.parentNode) {
                flagEl.parentNode.removeChild(flagEl);
            }
        }

        var legacyOwnedBanner = document.getElementById('game_area_already_owned');
        if (legacyOwnedBanner && legacyOwnedBanner.parentNode) {
            legacyOwnedBanner.parentNode.removeChild(legacyOwnedBanner);
        }

        var legacyHiddenEls = document.querySelectorAll('.sls-lua-library-hidden');
        for (var i = 0; i < legacyHiddenEls.length; i++) {
            var el = legacyHiddenEls[i];
            if (el.classList && typeof el.classList.remove === 'function') {
                el.classList.remove('sls-lua-library-hidden');
            }
            if (el.style) {
                el.style.removeProperty('display');
                el.style.removeProperty('width');
                el.style.removeProperty('height');
                el.style.removeProperty('min-width');
                el.style.removeProperty('min-height');
                el.style.removeProperty('max-width');
                el.style.removeProperty('max-height');
                el.style.removeProperty('margin');
                el.style.removeProperty('padding');
                el.style.removeProperty('border');
                el.style.removeProperty('outline');
                el.style.removeProperty('box-shadow');
                el.style.removeProperty('position');
                el.style.removeProperty('pointer-events');
                el.style.removeProperty('visibility');
                el.style.removeProperty('opacity');
                el.style.removeProperty('overflow');
            }
        }
    }

    function collectTargetAppIds(testMode) {
        var targetAppIds = new Set(slsLuaAppIds);
        if (testMode) {
            var candidateElements = document.querySelectorAll(
                '[data-ds-appid], [data-ds-itemkey], a[href*="/app/"], .search_result_row'
            );
            for (var ci = 0; ci < candidateElements.length; ci++) {
                var cel = candidateElements[ci];
                var cAid = cel.getAttribute('data-ds-appid');
                if (!cAid) {
                    var chref = cel.getAttribute('href') || cel.href || '';
                    var cm = chref.match(/\/app\/([0-9]+)/);
                    if (cm) cAid = cm[1];
                }
                if (cAid) {
                    var parts = String(cAid).split(',');
                    for (var pi = 0; pi < parts.length; pi++) {
                        var parsedAid = parseInt(parts[pi], 10);
                        if (parsedAid) targetAppIds.add(parsedAid);
                    }
                }
            }
        }
        return targetAppIds;
    }

    function mergeOwnedAppIdsIntoUserdata(payload, appIdSet) {
        if (!payload || typeof payload !== 'object' || !Array.isArray(payload.rgOwnedApps)) {
            return false;
        }
        if (!appIdSet || appIdSet.size === 0) return false;
        var existing = new Set();
        for (var i = 0; i < payload.rgOwnedApps.length; i++) {
            existing.add(Number(payload.rgOwnedApps[i]));
        }
        var modified = false;
        appIdSet.forEach(function(aid) {
            var numAid = Number(aid);
            if (numAid && !existing.has(numAid)) {
                existing.add(numAid);
                payload.rgOwnedApps.push(numAid);
                modified = true;
            }
        });
        return modified;
    }

    function installDynamicStoreUserdataHook() {
        if (typeof window === 'undefined') return;

        if (typeof window.fetch === 'function' && !window.__slsFetchOwnedHooked) {
            window.__slsFetchOwnedHooked = true;
            var origFetch = window.fetch;
            window.fetch = function(input) {
                var url = (typeof input === 'string') ? input : (input && input.url ? input.url : '');
                var fetchPromise = origFetch.apply(this, arguments);
                if (!url || url.indexOf('/dynamicstore/userdata/') === -1) {
                    return fetchPromise;
                }
                return fetchPromise.then(function(response) {
                    if (!response || !response.ok || typeof response.clone !== 'function') {
                        return response;
                    }
                    return response.clone().json().then(function(data) {
                        var targetIds = collectTargetAppIds(getIsTesting());
                        if (targetIds.size > 0 && mergeOwnedAppIdsIntoUserdata(data, targetIds)) {
                            var headers = new Headers(response.headers);
                            headers.delete('content-length');
                            return new Response(JSON.stringify(data), {
                                status: response.status,
                                statusText: response.statusText,
                                headers: headers
                            });
                        }
                        return response;
                    }).catch(function() {
                        return response;
                    });
                });
            };
        }

        var jq = window.$J || window.jQuery;
        if (jq && typeof jq.ajaxPrefilter === 'function' && !window.__slsAjaxOwnedHooked) {
            window.__slsAjaxOwnedHooked = true;
            jq.ajaxPrefilter(function(options) {
                if (options && typeof options.url === 'string' && options.url.indexOf('/dynamicstore/userdata/') !== -1) {
                    var origDataFilter = options.dataFilter;
                    options.dataFilter = function(data, type) {
                        var filtered = (typeof origDataFilter === 'function')
                            ? origDataFilter.call(this, data, type)
                            : data;
                        var targetIds = collectTargetAppIds(getIsTesting());
                        if (targetIds.size > 0 && typeof filtered === 'string') {
                            try {
                                var parsed = JSON.parse(filtered);
                                if (mergeOwnedAppIdsIntoUserdata(parsed, targetIds)) {
                                    return JSON.stringify(parsed);
                                }
                            } catch(e) {}
                        }
                        return filtered;
                    };
                }
            });
        }
    }

    function syncStoreItemCacheOwnership(itemCache, targetAppIds) {
        if (!itemCache || !itemCache.m_mapApps) return;
        targetAppIds.forEach(function(aid) {
            var app = itemCache.m_mapApps.get(Number(aid));
            if (app) {
                if (!app.m_userFilterFailure) app.m_userFilterFailure = {};
                app.m_userFilterFailure.already_owned = true;
                // Preserve normal owned visibility and ribbons without manual hiding
            }
        });
    }

    function syncQueryClientOwnership(qc, targetAppIds) {
        if (!qc || typeof qc.getQueryCache !== 'function') return;
        var queries = qc.getQueryCache().getAll();
        for (var qi = 0; qi < queries.length; qi++) {
            var q = queries[qi];
            if (!q || !Array.isArray(q.queryKey)) continue;
            var k0 = q.queryKey[0];
            var qData = q.state ? q.state.data : undefined;
            if (k0 === 'AccountOwnedApps' && qData instanceof Set) {
                var changedOwned = false;
                var nextOwned = new Set(qData);
                targetAppIds.forEach(function(aid) {
                    var numAid = Number(aid);
                    if (!nextOwned.has(numAid)) {
                        nextOwned.add(numAid);
                        changedOwned = true;
                    }
                });
                if (changedOwned) {
                    targetAppIds.forEach(function(aid) { qData.add(Number(aid)); });
                    if (typeof qc.setQueryData === 'function') {
                        qc.setQueryData(q.queryKey, nextOwned);
                    }
                }
            } else if (k0 === 'dynamicuserdata' && qData && qData.setOwnedApps instanceof Set) {
                var changedDyn = false;
                var nextDynSet = new Set(qData.setOwnedApps);
                targetAppIds.forEach(function(aid) {
                    var numAid = Number(aid);
                    if (!nextDynSet.has(numAid)) {
                        nextDynSet.add(numAid);
                        changedDyn = true;
                    }
                });
                if (changedDyn) {
                    targetAppIds.forEach(function(aid) { qData.setOwnedApps.add(Number(aid)); });
                    if (typeof qc.setQueryData === 'function') {
                        qc.setQueryData(q.queryKey, Object.assign({}, qData, { setOwnedApps: nextDynSet }));
                    }
                }
            } else if (k0 === 'StoreItem' && q.queryKey[2] === 'apply_user_filters' && typeof q.queryKey[1] === 'string') {
                var m = q.queryKey[1].match(/^app_([0-9]+)$/);
                if (m && targetAppIds.has(Number(m[1]))) {
                    if (!qData || !qData.already_owned) {
                        if (typeof qc.setQueryData === 'function') {
                            qc.setQueryData(q.queryKey, Object.assign({}, qData || {}, { already_owned: true }));
                        }
                    }
                }
            }
        }
    }

    function applyViteStoreModule(mod, targetAppIds) {
        if (!mod) return;
        var keys = Object.keys(mod);
        var foundQC = null;
        for (var i = 0; i < keys.length; i++) {
            var v = mod[keys[i]];
            if (!v) continue;
            try {
                if (typeof v.Get === 'function') {
                    var srcStr = Function.prototype.toString.call(v);
                    if (srcStr.indexOf('BIsGameOwned') !== -1 || srcStr.indexOf('m_boxCacheVersion') !== -1) {
                        var dynStore = v.Get();
                        if (dynStore) {
                            if (dynStore.m_queryClient) foundQC = dynStore.m_queryClient;
                            if (typeof dynStore.ReadData === 'function') {
                                var rData = dynStore.ReadData();
                                if (rData && rData.setOwnedApps instanceof Set) {
                                    targetAppIds.forEach(function(aid) {
                                        rData.setOwnedApps.add(Number(aid));
                                    });
                                }
                            }
                        }
                    } else if (srcStr.indexOf('m_mapApps') !== -1) {
                        var itemCache = v.Get();
                        syncStoreItemCacheOwnership(itemCache, targetAppIds);
                    }
                } else if (typeof v.getQueryCache === 'function') {
                    foundQC = v;
                }
            } catch(e) {}
        }
        if (foundQC) {
            syncQueryClientOwnership(foundQC, targetAppIds);
        }
    }

    function syncViteAndWebpackStore(targetAppIds) {
        // Webpack React Query & UserPreferences stores (legacy storefront bundles)
        try {
            if (window.webpackChunkstore && Array.isArray(window.webpackChunkstore)) {
                var req = null;
                window.webpackChunkstore.push([['__sls_sync_hook_' + Date.now()], {}, function(r) { req = r; }]);
                if (req) {
                    try {
                        var mod6469 = req(6469);
                        if (mod6469 && mod6469.Fm && typeof mod6469.Fm.Get === 'function') {
                            var store = mod6469.Fm.Get();
                            var data = store.ReadData();
                            if (data && data.setOwnedApps) {
                                targetAppIds.forEach(function(aid) {
                                    data.setOwnedApps.add(Number(aid));
                                });
                            }
                            if (store.m_queryClient) {
                                syncQueryClientOwnership(store.m_queryClient, targetAppIds);
                            }
                        }
                    } catch(e) {}
                }
            }
        } catch(e) {}

        // Vite ES module discovery (current store.steampowered.com applications/store/main.js)
        try {
            if (window.__slsViteStoreMod) {
                applyViteStoreModule(window.__slsViteStoreMod, targetAppIds);
                return;
            }
            var mainScript = document.querySelector('script[src*="applications/store/main.js"]');
            if (mainScript && mainScript.src) {
                if (!window.__slsViteStoreModPromise) {
                    window.__slsViteStoreModPromise = import(mainScript.src)
                        .then(function(mod) {
                            window.__slsViteStoreMod = mod;
                            applyViteStoreModule(mod, collectTargetAppIds(getIsTesting()));
                            return mod;
                        })
                        .catch(function() { return null; });
                }
            }
        } catch(e) {}
    }

    function decorateAppPageAlreadyOwned(pageAid) {
        if (!pageAid) return;

        var wishlistArea = document.getElementById('add_to_wishlist_area');
        if (wishlistArea && wishlistArea.style.display !== 'none') {
            wishlistArea.style.display = 'none';
        }

        var queueCtns = document.querySelectorAll('#queueCtn, .queue_ctn');
        for (var qi = 0; qi < queueCtns.length; qi++) {
            if (queueCtns[qi].classList && !queueCtns[qi].classList.contains('already_owned')) {
                queueCtns[qi].classList.add('already_owned');
            }
        }

        var queueAndPlaytime = document.querySelector('.queue_and_playtime');
        if (!queueAndPlaytime) return;

        var insertAnchor = queueAndPlaytime.querySelector('.queue_overflow_ctn');
        var purchaseOptionsContent = queueAndPlaytime.querySelector('#purchaseOptionsContent');

        function insertNativeNode(node) {
            if (insertAnchor && insertAnchor.parentNode === queueAndPlaytime) {
                queueAndPlaytime.insertBefore(node, insertAnchor.nextSibling);
                insertAnchor = node;
            } else if (purchaseOptionsContent && purchaseOptionsContent.parentNode === queueAndPlaytime) {
                queueAndPlaytime.insertBefore(node, purchaseOptionsContent);
            } else {
                queueAndPlaytime.appendChild(node);
            }
        }

        if (!queueAndPlaytime.querySelector('.game_area_already_owned.page_content')) {
            var appNameEl = document.getElementById('appHubAppName') || document.querySelector('.apphub_AppName');
            var appTitle = (appNameEl && appNameEl.textContent) ? appNameEl.textContent.trim() : '';
            if (!appTitle) appTitle = 'This title';

            var ownedDiv = document.createElement('div');
            ownedDiv.className = 'game_area_already_owned page_content';

            var ctnDiv = document.createElement('div');
            ctnDiv.className = 'game_area_already_owned_ctn';

            var flagDiv = document.createElement('div');
            flagDiv.className = 'ds_owned_flag ds_flag';
            flagDiv.innerHTML = 'IN LIBRARY&nbsp;&nbsp;';

            var textDiv = document.createElement('div');
            textDiv.className = 'already_in_library';
            textDiv.textContent = appTitle + ' is already in your Steam library';

            ctnDiv.appendChild(flagDiv);
            ctnDiv.appendChild(textDiv);
            ownedDiv.appendChild(ctnDiv);

            insertNativeNode(ownedDiv);
        }

        if (!queueAndPlaytime.querySelector('.game_area_play_stats')) {
            var playStatsDiv = document.createElement('div');
            playStatsDiv.className = 'game_area_play_stats';

            var panelDiv = document.createElement('div');
            panelDiv.setAttribute('data-panel', '{"flow-children":"row"}');

            var actionsDiv = document.createElement('div');
            actionsDiv.className = 'already_owned_actions';

            var btnDiv = document.createElement('div');
            btnDiv.className = 'game_area_already_owned_btn';

            var launchLink = document.createElement('a');
            launchLink.className = 'btnv6_lightblue_blue btnv6_border_2px btn_medium';
            launchLink.href = 'steam://launch/' + String(pageAid) + '/Dialog';

            var spanEl = document.createElement('span');
            spanEl.textContent = 'Play Now';

            launchLink.appendChild(spanEl);
            btnDiv.appendChild(launchLink);
            actionsDiv.appendChild(btnDiv);
            panelDiv.appendChild(actionsDiv);

            var clearDiv = document.createElement('div');
            clearDiv.style.clear = 'inline-start';

            playStatsDiv.appendChild(panelDiv);
            playStatsDiv.appendChild(clearDiv);

            insertNativeNode(playStatsDiv);
        }
    }

    function tagMissingDynamicStoreAttributes(targetAppIds, testMode) {
        var taggedAny = false;
        var linkCapsules = document.querySelectorAll('a.search_result_row, a.game_area_dlc_row, a.home_smallcap, a.match');
        var jq = window.$J || window.jQuery;
        for (var li = 0; li < linkCapsules.length; li++) {
            var lEl = linkCapsules[li];
            if (lEl.hasAttribute('data-ds-appid') || lEl.hasAttribute('data-ds-packageid') || lEl.hasAttribute('data-ds-bundleid')) {
                continue;
            }
            var lHref = lEl.getAttribute('href') || lEl.href || '';
            var lm = lHref.match(/\/app\/([0-9]+)/);
            if (lm && lm[1]) {
                var lAid = parseInt(lm[1], 10);
                if (lAid && (testMode || targetAppIds.has(lAid))) {
                    lEl.setAttribute('data-ds-appid', String(lAid));
                    if (typeof jq === 'function') {
                        try {
                            jq(lEl).data('dsAppid', String(lAid));
                        } catch(e) {}
                    }
                    taggedAny = true;
                }
            }
        }
        return taggedAny;
    }

    function hasUndecoratedOwnedElements(targetAppIds) {
        var dsEls = document.querySelectorAll('[data-ds-appid]');
        for (var i = 0; i < dsEls.length; i++) {
            var el = dsEls[i];
            if (!el.classList || el.classList.contains('ds_no_flags') || el.classList.contains('ds_owned')) {
                continue;
            }
            var rawAttr = el.getAttribute('data-ds-appid');
            if (!rawAttr) continue;
            var parts = String(rawAttr).split(',');
            var hasValid = false;
            var allOwned = true;
            for (var pi = 0; pi < parts.length; pi++) {
                var aid = parseInt(parts[pi], 10);
                if (!aid) continue;
                hasValid = true;
                if (!targetAppIds.has(aid) && !(window.GDynamicStore && GDynamicStore.s_rgOwnedApps && GDynamicStore.s_rgOwnedApps[aid])) {
                    allOwned = false;
                    break;
                }
            }
            if (hasValid && allOwned) return true;
        }
        return false;
    }

    function applyLuaLibraryOwnership() {
        var testMode = (typeof getIsTesting === 'function') ? getIsTesting() : (typeof isTesting !== 'undefined' ? isTesting : false);
        if (!testMode && (!slsLuaAppIdsLoaded || slsLuaAppIds.size === 0)) return;

        cleanupLegacyHiding();
        installDynamicStoreUserdataHook();

        var targetAppIds = collectTargetAppIds(testMode);

        // 1. Synchronize into GDynamicStore.s_rgOwnedApps & trigger native Steam decoration
        if (window.GDynamicStore && GDynamicStore.s_rgOwnedApps) {
            if (typeof GDynamicStore.OnReady === 'function' && !window.__slsDynamicStoreOnReadyHooked) {
                window.__slsDynamicStoreOnReadyHooked = true;
                GDynamicStore.OnReady(function() {
                    applyLuaLibraryOwnership();
                });
            }

            var needsDecorate = false;
            targetAppIds.forEach(function(aid) {
                if (!GDynamicStore.s_rgOwnedApps[aid]) {
                    GDynamicStore.s_rgOwnedApps[aid] = true;
                    needsDecorate = true;
                }
            });

            if (tagMissingDynamicStoreAttributes(targetAppIds, testMode)) {
                needsDecorate = true;
            }

            if (!needsDecorate && hasUndecoratedOwnedElements(targetAppIds)) {
                needsDecorate = true;
            }

            if (needsDecorate && typeof GDynamicStore.DecorateDynamicItems === 'function') {
                if (typeof GDynamicStore.s_rgPersonalizedBundleData === 'object') {
                    GDynamicStore.s_rgPersonalizedBundleData = {};
                }
                var jq = window.$J || window.jQuery;
                if (typeof jq === 'function') {
                    try {
                        jq('[data-ds-appid], [data-ds-packageid], [data-ds-bundleid]').each(function() {
                            var $el = jq(this);
                            if (!$el.hasClass('ds_owned')) {
                                $el.children('.ds_options').remove();
                            }
                        });
                    } catch(e) {}
                }
                GDynamicStore.DecorateDynamicItems(null, true);
            }
        }

        // 2. Synchronize into StoreItemCache if present (used by modern React content hubs)
        if (window.StoreItemCache) {
            syncStoreItemCacheOwnership(window.StoreItemCache, targetAppIds);
        }

        // 3. Synchronize into modern Vite / Webpack React Query & UserPreferences stores
        syncViteAndWebpackStore(targetAppIds);

        // 4. Native App Product Page (.queue_and_playtime -> .game_area_already_owned.page_content)
        var pageMatch = (typeof window !== 'undefined' && window.location && window.location.href)
            ? window.location.href.match(/\/app\/([0-9]+)/)
            : null;
        if (pageMatch && pageMatch[1]) {
            var pageAid = parseInt(pageMatch[1], 10);
            if (testMode || targetAppIds.has(pageAid)) {
                decorateAppPageAlreadyOwned(pageAid);
            }
        }
    }

    function initLuaLibraryOwnership() {
        cleanupLegacyHiding();
        installDynamicStoreUserdataHook();

        // If test mode is enabled, immediately apply ownership decoration to all items
        if (getIsTesting()) {
            applyLuaLibraryOwnership();
        }

        // Fetch Lua AppIDs from local Navi server
        naviFetch('http://127.0.0.1:9001/lua_apps')
            .then(function(r) { return r.json(); })
            .then(function(data) {
                if (data && Array.isArray(data.appids)) {
                    data.appids.forEach(function(id) {
                        slsLuaAppIds.add(Number(id));
                    });
                    slsLuaAppIdsLoaded = true;
                    applyLuaLibraryOwnership();
                }
            })
            .catch(function(err) {
                ping('Failed to fetch /lua_apps: ' + err);
            });

        // If GDynamicStore is loading asynchronously, hook OnReady
        if (window.GDynamicStore) {
            if (typeof GDynamicStore.OnReady === 'function' && !window.__slsDynamicStoreOnReadyHooked) {
                window.__slsDynamicStoreOnReadyHooked = true;
                GDynamicStore.OnReady(function() {
                    applyLuaLibraryOwnership();
                });
            }
        }
    }

    // --- main.js ---
    /**
     * @file  main.js
     * @brief Initialization lifecycle, mutation observer setup, and debounced store page runner
     */
    if (window.__slsStoreObserver && typeof window.__slsStoreObserver.disconnect === 'function') {
        window.__slsStoreObserver.disconnect();
    }

    var observer = null;
    var debounceTimer = null;

    function debouncedAddButtons() {
        if (debounceTimer) return;
        debounceTimer = requestAnimationFrame(function() {
            debounceTimer = null;
            addButtons();
            applyLuaLibraryOwnership();
        });
    }

    initLuaLibraryOwnership();
    addButtons();
    observer = new MutationObserver(debouncedAddButtons);
    window.__slsStoreObserver = observer;
    if (document.documentElement) observer.observe(document.documentElement, { childList: true, subtree: true });
})();
