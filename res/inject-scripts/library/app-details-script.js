/**
 * @file  app-details-script.js
 * @brief Bundled standalone script for Steam library game details page
 */
(function() {
    'use strict';
    if (window.__slsAppDetailsInjected) return;
    window.__slsAppDetailsInjected = true;

    function log(m) {
        console.log('[SLS] ' + m);
        fetch('http://127.0.0.1:9001/log?msg=' + encodeURIComponent('[RemoveLua] ' + m)).catch(function(){});
    }

    // Inlined Styles & Templates
    var SLS_STYLES = "/* SLS App Details & Modals Styles */\n\n.sls-btn-action {\n    display: flex;\n    justify-content: center;\n    align-items: center;\n    background: rgba(172, 178, 201, 0.14);\n    color: rgb(220, 222, 223);\n    border: none;\n    border-radius: 2px;\n    padding: 8px 16px;\n    font-size: 16px;\n    font-weight: 400;\n    font-family: \"Motiva Sans\", sans-serif;\n    height: 48px;\n    box-sizing: border-box;\n    cursor: pointer;\n    text-decoration: none;\n    transition: background 0.1s ease;\n}\n\n.sls-btn-action:hover,\n.sls-btn-action:not(:disabled):hover {\n    background: rgba(172, 178, 201, 0.25);\n}\n\n.sls-btn-action:active,\n.sls-btn-action:not(:disabled):active {\n    background: rgba(172, 178, 201, 0.30);\n}\n\n.sls-btn-close {\n    background: transparent;\n    border: none;\n    color: #969696;\n    font-size: 24px;\n    cursor: pointer;\n    width: 32px;\n    height: 32px;\n    border-radius: 2px;\n    display: flex;\n    align-items: center;\n    justify-content: center;\n    transition: background 0.1s ease;\n}\n\n.sls-btn-close:hover {\n    background: rgba(172, 178, 201, 0.14);\n    color: #fff;\n}\n\n#sls-remove-confirm:hover {\n    background: rgba(220, 38, 38, 1) !important;\n}\n\n.sls-btn-restore-item {\n    background: #2563eb;\n    color: #fff;\n    border: none;\n    border-radius: 2px;\n    padding: 6px 14px;\n    font-size: 13px;\n    cursor: pointer;\n    transition: background 0.1s ease;\n}\n\n.sls-btn-restore-item:hover {\n    background: #1d4ed8;\n}\n\n/* Modal overlays */\n#sls-overlay-modal,\n#sls-provider-modal,\n#sls-remove-modal,\n#sls-restore-modal,\n#sls-reset-modal,\n#sls-navi-modal {\n    position: fixed;\n    top: 0;\n    left: 0;\n    width: 100%;\n    height: 100%;\n    background: rgba(0, 0, 0, 0.8);\n    z-index: 999999;\n    display: flex;\n    justify-content: center;\n    align-items: center;\n    backdrop-filter: blur(8px);\n}\n\n#sls-restore-modal,\n#sls-reset-modal {\n    background: rgba(0, 0, 0, 0.82);\n}\n\n";
    var GAME_MANAGER_MODAL_HTML = "<div style=\"background: #1e2024; border: 1px solid #3d4450; border-radius: 4px; padding: 24px; width: 440px; box-shadow: 0 4px 16px rgba(0,0,0,0.5); font-family: 'Motiva Sans', sans-serif; color: #dcdedf; transition: all 0.3s cubic-bezier(0.16, 1, 0.3, 1); transform: scale(0.95); opacity: 0;\" id=\"sls-modal-card\">\n    <!-- Title bar -->\n    <div style=\"display:flex; justify-content:space-between; align-items:flex-start; margin-bottom:20px;\">\n        <div>\n            <h2 style=\"margin:0; font-size:22px; font-weight:300; color: #fff; text-transform: uppercase; letter-spacing: 1px;\">SLS Game Manager</h2>\n            <p style=\"margin:4px 0 0; font-size:14px; color:#969696;\">AppID: {{APPID}}</p>\n        </div>\n        <button id=\"sls-close-x\" class=\"sls-btn-close Focusable\" tabindex=\"-1\" role=\"button\" aria-label=\"Close\">&times;</button>\n    </div>\n    <!-- Administrative Tools Section -->\n    <div id=\"sls-content-admin\" style=\"display:block; margin-bottom:24px;\">\n        <div style=\"display:flex; flex-direction:column; gap:8px;\">\n            <!-- Online Fix -->\n            <button id=\"sls-btn-fix\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"width:100%;\">Checking Fix status...</button>\n            <!-- Auto Crack -->\n            <button id=\"sls-btn-crack\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"width:100%;\">Checking Crack status...</button>\n        </div>\n    </div>\n    <!-- Footer Actions -->\n    <div style=\"display:flex; justify-content:flex-end; gap:10px;\">\n        <button id=\"sls-btn-cancel\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"padding: 0 24px; height: 36px;\">Close</button>\n    </div>\n</div>\n\n";
    var PROVIDER_MODAL_HTML = "<div style=\"background: #1e2024; border: 1px solid #3d4450; border-radius: 4px; padding: 24px; width: 440px; box-shadow: 0 4px 16px rgba(0,0,0,0.5); font-family: 'Motiva Sans', sans-serif; color: #dcdedf; transition: all 0.3s cubic-bezier(0.16, 1, 0.3, 1); transform: scale(0.95); opacity: 0;\" id=\"sls-provider-card\">\n    <div style=\"display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:20px;\">\n        <div>\n            <h2 style=\"margin:0; font-size:22px; font-weight:300; color: #fff; text-transform: uppercase; letter-spacing: 1px;\">Change Provider</h2>\n            <p style=\"margin:4px 0 0; font-size:14px; color:#969696;\">AppID: {{APPID}}</p>\n        </div>\n        <button id=\"sls-prov-close\" class=\"sls-btn-close Focusable\" tabindex=\"-1\" role=\"button\" aria-label=\"Close\">&times;</button>\n    </div>\n    <div id=\"sls-provider-list\" style=\"max-height:320px;overflow-y:auto;margin-bottom:16px;\">{{ROWS}}</div>\n    <div id=\"sls-prov-status\" role=\"status\" aria-live=\"polite\" style=\"display:none;text-align:center;padding:12px;font-size:14px;color:#dcdedf;background:rgba(172,178,201,0.1);border-radius:2px;margin-bottom:16px;\"></div>\n    <div style=\"display:flex;justify-content:flex-end;gap:10px;\">\n        <button id=\"sls-prov-cancel\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"padding: 0 24px; height: 36px;\">Close</button>\n    </div>\n</div>\n\n";
    var REMOVE_MODAL_HTML = "<div style=\"background: #1e2024; border: 1px solid #3d4450; border-radius: 4px; padding: 24px; width: 440px; box-shadow: 0 4px 16px rgba(0,0,0,0.5); font-family: 'Motiva Sans', sans-serif; color: #dcdedf;\">\n    <h2 style=\"margin:0 0 10px;font-size:22px;font-weight:300;color:#fff;text-transform:uppercase;letter-spacing:1px;\">Remove Lua</h2>\n    <p style=\"margin:0 0 24px;font-size:16px;color:#969696;\">Remove Lua and Game files for AppID <b>{{APPID}}</b>?</p>\n    <div style=\"display:flex;justify-content:flex-end;gap:10px;\">\n        <button id=\"sls-remove-cancel\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"padding: 0 24px; height: 36px;\">Cancel</button>\n        <button id=\"sls-remove-confirm\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"padding: 0 24px; height: 36px; background: rgba(220, 38, 38, 0.8); color: #fff;\">Remove</button>\n    </div>\n</div>\n\n";
    var RESTORE_MODAL_HTML = "<div style=\"background: #1e2024; border: 1px solid #3d4450; border-radius: 4px; padding: 24px; width: 480px; box-shadow: 0 8px 24px rgba(0,0,0,0.7); font-family: 'Motiva Sans', sans-serif; color: #dcdedf;\">\n    <div style=\"display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:16px;\">\n        <div>\n            <h2 style=\"margin:0;font-size:20px;font-weight:600;color:#fff;text-transform:uppercase;letter-spacing:1px;\">Restore Cloud Save</h2>\n            <p style=\"margin:4px 0 0;font-size:13px;color:#969696;\">AppID: {{APPID}}</p>\n        </div>\n        <button id=\"sls-restore-close\" class=\"sls-btn-close Focusable\" tabindex=\"-1\" role=\"button\" aria-label=\"Close\" style=\"background:transparent;border:none;color:#969696;font-size:24px;cursor:pointer;\">&times;</button>\n    </div>\n    <div id=\"sls-restore-body\" style=\"max-height:300px;overflow-y:auto;margin-bottom:20px;\">\n        <div style=\"text-align:center;padding:24px;color:#969696;\">Loading backups...</div>\n    </div>\n    <div style=\"display:flex;justify-content:flex-end;\">\n        <button id=\"sls-modal-cancel\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"height:36px;padding:0 20px;background:rgba(172,178,201,0.14);border:none;border-radius:2px;color:#dcdedf;cursor:pointer;\">Close</button>\n    </div>\n</div>\n\n";
    var RESET_MODAL_HTML = "<div style=\"background: #1e2024; border: 1px solid #7f1d1d; border-radius: 4px; padding: 24px; width: 460px; box-shadow: 0 8px 24px rgba(0,0,0,0.7); font-family: 'Motiva Sans', sans-serif; color: #dcdedf;\">\n    <div style=\"display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:16px;\">\n        <div>\n            <h2 style=\"margin:0;font-size:20px;font-weight:600;color:#f87171;text-transform:uppercase;letter-spacing:1px;\">Reset Cloud Save</h2>\n            <p style=\"margin:4px 0 0;font-size:13px;color:#969696;\">AppID: {{APPID}}</p>\n        </div>\n        <button id=\"sls-reset-close\" class=\"sls-btn-close Focusable\" tabindex=\"-1\" role=\"button\" aria-label=\"Close\" style=\"background:transparent;border:none;color:#969696;font-size:24px;cursor:pointer;\">&times;</button>\n    </div>\n    <div style=\"background:rgba(185,28,28,0.15);border:1px solid rgba(185,28,28,0.3);border-radius:3px;padding:12px;margin-bottom:16px;\">\n        <p style=\"margin:0;font-size:13px;line-height:1.5;color:#fca5a5;\">\n            \u26a0\ufe0f <b>Warning:</b> This will permanently erase cloud-redirected save data for this app. This action cannot be reversed.\n        </p>\n    </div>\n    <div style=\"margin-bottom:20px;\">\n        <label style=\"display:block;font-size:13px;color:#dcdedf;margin-bottom:8px;\">\n            To confirm, type <b style=\"color:#fff;letter-spacing:1px;\">RESET</b> into the box below:\n        </label>\n        <input type=\"text\" id=\"sls-reset-input\" autocomplete=\"off\" spellcheck=\"false\" placeholder=\"Type RESET to confirm\"\n            style=\"width:100%;height:38px;padding:0 12px;background:#121417;border:1px solid #4b5563;border-radius:2px;color:#fff;font-size:14px;box-sizing:border-box;outline:none;\" />\n    </div>\n    <div style=\"display:flex;justify-content:flex-end;gap:10px;\">\n        <button id=\"sls-modal-cancel\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"height:36px;padding:0 20px;background:rgba(172,178,201,0.14);border:none;border-radius:2px;color:#dcdedf;cursor:pointer;\">Cancel</button>\n        <button id=\"sls-modal-confirm\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" disabled style=\"height:36px;padding:0 20px;background:#dc2626;color:#fff;cursor:default;opacity:0.4;font-weight:600;\">Confirm Reset</button>\n    </div>\n</div>\n\n";
    var NAVI_MODAL_HTML = "<div style=\"background: #1e2024; border: 1px solid #3d4450; border-radius: 4px; padding: 24px; width: 440px; box-shadow: 0 4px 16px rgba(0,0,0,0.5); font-family: 'Motiva Sans', sans-serif; color: #dcdedf; transition: all 0.3s cubic-bezier(0.16, 1, 0.3, 1); transform: scale(0.95); opacity: 0;\" id=\"sls-navi-card\">\n    <div style=\"display:flex; justify-content:space-between; align-items:flex-start; margin-bottom:18px;\">\n        <div>\n            <h2 style=\"margin:0; font-size:22px; font-weight:300; color: #fff; text-transform: uppercase; letter-spacing: 1px;\">Navi Menu</h2>\n            <p style=\"margin:4px 0 0; font-size:14px; color:#969696;\">AppID: {{APPID}}</p>\n        </div>\n        <button id=\"sls-navi-close\" class=\"sls-btn-close Focusable\" tabindex=\"-1\" role=\"button\" aria-label=\"Close\">&times;</button>\n    </div>\n    <div style=\"display:flex; align-items:center; justify-content:space-between; padding:12px 14px; margin-bottom:16px; background:rgba(0,0,0,0.25); border:1px solid rgba(172,178,201,0.12); border-radius:2px;\">\n        <span style=\"font-size:14px; color:rgb(139, 147, 164);\">Current Provider</span>\n        <span id=\"sls-navi-modal-provider\" style=\"font-size:15px; font-weight:500; color:#fff;\">{{PROVIDER}}</span>\n    </div>\n    <div style=\"display:flex; flex-direction:column; gap:10px; margin-bottom:20px;\">\n        <button id=\"sls-navi-btn-provider\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"width:100%; justify-content:flex-start; padding: 0 16px;\">\n            <span>Change Provider</span>\n        </button>\n        <button id=\"sls-navi-btn-config\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"width:100%; justify-content:flex-start; padding: 0 16px;\">\n            <span>Config (Fix / Auto Crack)</span>\n        </button>\n        <button id=\"sls-navi-btn-cloud-toggle\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"width:100%; justify-content:space-between; padding: 0 16px;\">\n            <span>Cloud Save</span><span id=\"sls-navi-cloud-badge\" style=\"font-size:13px; font-weight:600; color:#9ca3af;\">Checking...</span>\n        </button>\n        <button id=\"sls-navi-btn-cloud-files\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"width:100%; justify-content:space-between; padding: 0 16px;\">\n            <span>View Cloud Files</span><span id=\"sls-navi-cloud-files-badge\" style=\"font-size:12px; color:#66c0f4;\">Steam Cloud &rarr;</span>\n        </button>\n        <button id=\"sls-navi-btn-cloud-backup\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"width:100%; justify-content:flex-start; padding: 0 16px;\">\n            <span>Backup Cloud Save</span>\n        </button>\n        <button id=\"sls-navi-btn-cloud-restore\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"width:100%; justify-content:flex-start; padding: 0 16px;\">\n            <span>Restore Cloud Save</span>\n        </button>\n        <button id=\"sls-navi-btn-cloud-reset\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"width:100%; justify-content:flex-start; padding: 0 16px; color:#f87171;\">\n            <span>Reset Cloud Save</span>\n        </button>\n        <button id=\"sls-navi-btn-remove\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"width:100%; justify-content:flex-start; padding: 0 16px; {{REMOVE_BTN_STYLE}}\">\n            <span>{{REMOVE_LABEL}}</span>\n        </button>\n    </div>\n    <div style=\"display:flex; justify-content:flex-end;\">\n        <button id=\"sls-navi-cancel\" class=\"sls-btn-action Focusable\" tabindex=\"-1\" role=\"button\" style=\"padding: 0 24px; height: 36px;\">Close</button>\n    </div>\n</div>\n\n";

    function ensureStyles() {
        if (document.getElementById('sls-app-details-styles')) return;
        var style = document.createElement('style');
        style.id = 'sls-app-details-styles';
        style.textContent = SLS_STYLES;
        var target = document.head || document.body || document.documentElement;
        if (target) target.appendChild(style);
    }

    // --- gamepad.js ---
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


    // --- appid.js ---
    /**
     * @file  appid.js
     * @brief AppId resolution and extraction from current library DOM state
     */
    function extractAppId(manageBtn) {
        var appid = null;
        var curr = manageBtn;
        while (curr && curr !== document.body) {
            var cls = (typeof curr.className === 'string') ? curr.className : '';
            var m = cls.match(/\bapp_([0-9]+)\b/);
            if (m) { appid = m[1]; break; }
            var da = curr.getAttribute('data-appid');
            if (da) { appid = da; break; }
            curr = curr.parentElement;
        }

        if (!appid) {
            try {
                var currFiber = manageBtn;
                while (currFiber && currFiber !== document.body && !appid) {
                    for (var key in currFiber) {
                        if (key.startsWith('__reactInternalInstance$') || key.startsWith('__reactFiber$')) {
                            var fiber = currFiber[key];
                            while (fiber) {
                                if (fiber.memoizedProps) {
                                    if (fiber.memoizedProps.appid) { appid = String(fiber.memoizedProps.appid); break; }
                                    if (fiber.memoizedProps.appID) { appid = String(fiber.memoizedProps.appID); break; }
                                    if (fiber.memoizedProps.unAppID) { appid = String(fiber.memoizedProps.unAppID); break; }
                                    if (fiber.memoizedProps.overview && fiber.memoizedProps.overview.appid) { appid = String(fiber.memoizedProps.overview.appid); break; }
                                    if (fiber.memoizedProps.app && fiber.memoizedProps.app.appid) { appid = String(fiber.memoizedProps.app.appid); break; }
                                    if (fiber.memoizedProps.details && fiber.memoizedProps.details.appid) { appid = String(fiber.memoizedProps.details.appid); break; }
                                }
                                fiber = fiber.return;
                            }
                        }
                        if (appid) break;
                    }
                    currFiber = currFiber.parentElement;
                }
            } catch(e) {}
        }

        if (!appid) {
            var match = window.location.href.match(/\/app\/([0-9]+)/);
            if (match) appid = match[1];
            if (!appid) {
                match = window.location.hash.match(/\/app\/([0-9]+)/);
                if (match) appid = match[1];
            }
        }

        if (!appid) {
            var elWithStyle = document.querySelector('[style*="/assets/"]');
            if (elWithStyle) {
                var m = elWithStyle.style.backgroundImage.match(/\/assets\/([0-9]+)\//);
                if (m) appid = m[1];
            }
        }
        if (!appid) {
            var links = document.querySelectorAll('a[href*="/app/"]');
            for (var i = 0; i < links.length; i++) {
                var m = links[i].href.match(/\/app\/([0-9]+)/);
                if (m) { appid = m[1]; break; }
            }
        }
        return appid;
    }


    // --- cloud-api.js ---
    /**
     * @file  cloud-api.js
     * @brief Client-side REST API wrapper for engine CloudRedirect endpoints
     */
    function fetchAppCloudStatus(appId) {
        if (window.NaviCloudManager && window.NaviCloudManager.fetchAppStatus) {
            return window.NaviCloudManager.fetchAppStatus(appId);
        }
        return fetch('http://127.0.0.1:9001/cloud/apps/' + appId + '/status', {
            headers: { 'X-Navi-Auth': window.__naviAuthToken || '' }
        }).then(function(r) { return r.json(); });
    }

    function toggleAppCloudRedirect(appId, redirect) {
        if (window.NaviCloudManager && window.NaviCloudManager.toggleAppRedirect) {
            return window.NaviCloudManager.toggleAppRedirect(appId, redirect);
        }
        return fetch('http://127.0.0.1:9001/cloud/apps/' + appId + '/toggle', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'X-Navi-Auth': window.__naviAuthToken || ''
            },
            body: JSON.stringify({ redirect: !!redirect })
        }).then(function(r) { return r.json(); });
    }

    function backupAppCloudSave(appId) {
        if (window.NaviCloudManager && window.NaviCloudManager.backupAppSave) {
            return window.NaviCloudManager.backupAppSave(appId);
        }
        return fetch('http://127.0.0.1:9001/cloud/apps/' + appId + '/backup', {
            method: 'POST',
            headers: { 'X-Navi-Auth': window.__naviAuthToken || '' }
        }).then(function(r) { return r.json(); });
    }


    // --- game-manager-modal.js ---
    /**
     * @file  game-manager-modal.js
     * @brief Modal UI for configuring game-specific depot and save settings
     */
    function openSlsConfig(appid) {
        if (document.getElementById('sls-overlay-modal')) return;
        ensureStyles();
        var overlay = document.createElement('div');
        overlay.id = 'sls-overlay-modal';
        overlay.setAttribute('role', 'dialog');
        overlay.setAttribute('aria-modal', 'true');
        overlay.setAttribute('data-testid', 'sls-overlay-modal');
        overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.8);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);transition:all 0.3s ease;opacity:0;';

        var cardHtml = GAME_MANAGER_MODAL_HTML.replace(/\{\{APPID\}\}/g, appid);
        overlay.innerHTML = cardHtml;
        document.body.appendChild(overlay);

        requestAnimationFrame(function() {
            overlay.style.opacity = '1';
            var card = document.getElementById('sls-modal-card');
            if (card) {
                card.style.transform = 'scale(1)';
                card.style.opacity = '1';
            }
        });

        var fixBtn = document.getElementById('sls-btn-fix');
        var crackBtn = document.getElementById('sls-btn-crack');
        var closeX = document.getElementById('sls-close-x');
        var cancelBtn = document.getElementById('sls-btn-cancel');

        trapSteamGamepadModal(overlay, [closeX, fixBtn, crackBtn, cancelBtn]);

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

        fetch('http://127.0.0.1:9001/check?id=' + appid)
            .then(function(r) { return r.json(); })
            .then(function(data) {
                function updateFixBtn(installed) {
                    fixBtn.className = 'sls-btn-action Focusable';
                    if (installed) {
                        fixBtn.innerText = 'Remove Online-Fix';
                        fixBtn.onclick = function(e) {
                            e.preventDefault(); e.stopPropagation();
                            fixBtn.innerText = 'Removing...';
                            log('Remove Online-Fix clicked for AppID ' + appid);
                            fetch('http://127.0.0.1:9001/remove-fix?id=' + appid, { mode: 'no-cors' })
                                .then(function() { updateFixBtn(false); });
                        };
                    } else {
                        fixBtn.innerText = 'Install Online-Fix';
                        fixBtn.onclick = function(e) {
                            e.preventDefault(); e.stopPropagation();
                            fixBtn.innerText = 'Installing...';
                            log('Install Online-Fix clicked for AppID ' + appid);
                            fetch('http://127.0.0.1:9001/install-fix?id=' + appid, { mode: 'no-cors' })
                                .then(function() { updateFixBtn(true); });
                        };
                    }
                }
                updateFixBtn(data.onlineFixInstalled);

                var pollInterval = null;
                function pollCrackStatus() {
                    if (pollInterval) clearInterval(pollInterval);
                    pollInterval = setInterval(function() {
                        fetch('http://127.0.0.1:9001/crack-status?id=' + appid)
                            .then(function(r) { return r.json(); })
                            .then(function(st) {
                                if (st.state === 'running' || st.state === 'queued' || st.state === 'restoring') {
                                    crackBtn.disabled = true;
                                    crackBtn.style.opacity = '0.7';
                                    if (st.stage === 'gameinfo') crackBtn.innerText = 'Fetching game info...';
                                    else if (st.stage === 'unpack') crackBtn.innerText = 'Unpacking SteamStub...';
                                    else if (st.stage === 'apply') crackBtn.innerText = 'Applying emulator...';
                                    else if (st.stage === 'restore') crackBtn.innerText = 'Restoring original files...';
                                    else crackBtn.innerText = st.message || 'Processing...';
                                } else {
                                    clearInterval(pollInterval);
                                    pollInterval = null;
                                    crackBtn.disabled = false;
                                    crackBtn.style.opacity = '1';
                                    if (st.state === 'needs_reapply') {
                                        updateCrackBtn(true, 'needs_reapply');
                                    } else if (st.state === 'succeeded') {
                                        updateCrackBtn(true, 'succeeded');
                                    } else if (st.state === 'failed') {
                                        crackBtn.className = 'sls-btn-action Focusable';
                                        crackBtn.style.background = 'rgba(235, 75, 75, 0.25)';
                                        crackBtn.innerText = 'Failed: ' + (st.message || 'Retry AutoCrack');
                                        crackBtn.onclick = function(e) {
                                            e.preventDefault(); e.stopPropagation();
                                            triggerCrack(false);
                                        };
                                    } else if (st.state === 'skipped') {
                                        crackBtn.innerText = 'Skipped: ' + (st.message || 'Not applicable');
                                        crackBtn.onclick = function(e) {
                                            e.preventDefault(); e.stopPropagation();
                                            triggerCrack(true);
                                        };
                                    } else {
                                        updateCrackBtn(false, 'idle');
                                    }
                                }
                            })
                            .catch(function() {
                                clearInterval(pollInterval);
                                pollInterval = null;
                            });
                    }, 1000);
                }

                function triggerCrack(force) {
                    crackBtn.disabled = true;
                    crackBtn.innerText = 'Installing...';
                    var url = 'http://127.0.0.1:9001/install-crack?id=' + appid + (force ? '&force=1' : '');
                    fetch(url)
                        .then(function(r) { return r.json(); })
                        .then(function(data) {
                            if (data && data.autoCrackInstalled !== undefined) {
                                updateCrackBtn(data.autoCrackInstalled, 'running');
                            }
                            pollCrackStatus();
                        })
                        .catch(function() {
                            crackBtn.disabled = false;
                            crackBtn.innerText = 'Failed to queue crack';
                        });
                }

                function triggerRestore() {
                    crackBtn.disabled = true;
                    crackBtn.innerText = 'Removing...';
                    fetch('http://127.0.0.1:9001/remove-crack?id=' + appid)
                        .then(function(r) { return r.json(); })
                        .then(function(data) {
                            if (data && data.autoCrackInstalled !== undefined) {
                                updateCrackBtn(data.autoCrackInstalled, 'idle');
                            }
                            pollCrackStatus();
                        })
                        .catch(function() {
                            crackBtn.disabled = false;
                            crackBtn.innerText = 'Failed to remove crack';
                        });
                }

                function updateCrackBtn(installed, state) {
                    crackBtn.className = 'sls-btn-action Focusable';
                    crackBtn.disabled = false;
                    crackBtn.style.opacity = '1';
                    crackBtn.style.background = '';

                    if (state === 'needs_reapply') {
                        crackBtn.style.background = 'rgba(235, 160, 50, 0.35)';
                        crackBtn.innerText = 'Game Updated — Re-apply AutoCrack';
                        crackBtn.onclick = function(e) {
                            e.preventDefault(); e.stopPropagation();
                            log('Re-apply AutoCrack clicked for AppID ' + appid);
                            triggerCrack(true);
                        };
                    } else if (installed) {
                        crackBtn.innerText = 'Remove AutoCrack';
                        crackBtn.onclick = function(e) {
                            e.preventDefault(); e.stopPropagation();
                            log('Remove AutoCrack clicked for AppID ' + appid);
                            triggerRestore();
                        };
                    } else {
                        crackBtn.innerText = 'Install AutoCrack';
                        crackBtn.onclick = function(e) {
                            e.preventDefault(); e.stopPropagation();
                            log('Install AutoCrack clicked for AppID ' + appid);
                            triggerCrack(false);
                        };
                    }
                }

                if (data.autoCrackState === 'running' || data.autoCrackState === 'queued') {
                    pollCrackStatus();
                } else {
                    updateCrackBtn(data.autoCrackInstalled, data.autoCrackState);
                }
            })
            .catch(function() {
                fixBtn.innerText = 'Failed to load Online-Fix status';
                crackBtn.innerText = 'Failed to load AutoCrack status';
            });
    }


    // --- provider-modal.js ---
    /**
     * @file  provider-modal.js
     * @brief Modal UI for configuring CloudRedirect cloud storage providers
     */
    var luaProviders = [
        { name: 'Ryuu' },
        { name: 'DepotBox' },
        { name: 'HubcapDB' },
        { name: 'Spinoza' }
    ];

    function openChangeProviderModal(appid, currentProvider) {
        if (document.getElementById('sls-provider-modal')) return;
        ensureStyles();
        var overlay = document.createElement('div');
        overlay.id = 'sls-provider-modal';
        overlay.setAttribute('role', 'dialog');
        overlay.setAttribute('aria-modal', 'true');
        overlay.setAttribute('data-testid', 'sls-provider-modal');
        overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.8);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);transition:all 0.3s ease;opacity:0;';

        var rows = '';
        for (var i = 0; i < luaProviders.length; i++) {
            var isCurrent = currentProvider && currentProvider.toLowerCase().indexOf(luaProviders[i].name.toLowerCase()) !== -1;
            var badgeHtml = isCurrent ? '<span style="margin-left:8px;font-size:12px;padding:2px 6px;background:rgba(172,178,201,0.2);border-radius:2px;color:#dcdedf;">Current</span>' : '';
            var btnStyle = isCurrent
                ? 'background:rgba(172,178,201,0.05);color:#969696;cursor:default;opacity:0.6;'
                : '';
            rows += '<div style="display:flex;align-items:center;justify-content:space-between;padding:12px 14px;margin-bottom:6px;background:rgba(0,0,0,0.2);border:1px solid rgba(172,178,201,0.1);border-radius:2px;">' +
                '<span style="flex:1;color:#dcdedf;font-size:16px;font-weight:400;font-family:\'Motiva Sans\', sans-serif;">' + luaProviders[i].name + badgeHtml + '</span>' +
                '<button data-sls-provider-idx="' + i + '" class="sls-btn-action Focusable" tabindex="-1" role="button" ' + (isCurrent ? 'disabled ' : '') + 'style="height: 36px; padding: 0 16px; ' + btnStyle + '">' + (isCurrent ? 'Active' : 'Use') + '</button>' +
            '</div>';
        }

        var cardHtml = PROVIDER_MODAL_HTML
            .replace(/\{\{APPID\}\}/g, appid)
            .replace('{{ROWS}}', rows);
        overlay.innerHTML = cardHtml;
        document.body.appendChild(overlay);

        requestAnimationFrame(function() {
            overlay.style.opacity = '1';
            var card = document.getElementById('sls-provider-card');
            if (card) { card.style.transform = 'scale(1)'; card.style.opacity = '1'; }
        });

        function closeModal() {
            overlay.style.opacity = '0';
            var card = document.getElementById('sls-provider-card');
            if (card) { card.style.transform = 'scale(0.95)'; card.style.opacity = '0'; }
            setTimeout(function() { overlay.remove(); }, 300);
        }

        document.getElementById('sls-prov-close').onclick = closeModal;
        document.getElementById('sls-prov-cancel').onclick = closeModal;

        var provBtns = overlay.querySelectorAll('[data-sls-provider-idx]');

        var modalButtons = [document.getElementById('sls-prov-close')];
        provBtns.forEach(function(b) { modalButtons.push(b); });
        modalButtons.push(document.getElementById('sls-prov-cancel'));
        trapSteamGamepadModal(overlay, modalButtons);
        provBtns.forEach(function(btn) {
            btn.onclick = function(e) {
                e.preventDefault(); e.stopPropagation();
                var idx = btn.getAttribute('data-sls-provider-idx');
                var statusEl = document.getElementById('sls-prov-status');
                statusEl.style.display = 'block';
                statusEl.innerText = 'Switching to ' + luaProviders[idx].name + '...';

                // Disable all buttons
                provBtns.forEach(function(b) { b.disabled = true; b.style.opacity = '0.5'; b.style.cursor = 'default'; });

                fetch('http://127.0.0.1:9001/change-provider?id=' + appid + '&provider=' + idx)
                    .then(function(r) { return r.json(); })
                    .then(function(data) {
                        if (data.success) {
                            statusEl.innerText = 'Download started from ' + luaProviders[idx].name + '. Please wait...';
                            statusEl.style.background = 'rgba(16,185,129,0.1)';
                            statusEl.style.borderColor = 'rgba(16,185,129,0.3)';
                            statusEl.style.color = '#6ee7b7';

                            var pollCount = 0;
                            var lastStatus = '';
                            var timer = setInterval(function() {
                                var btn = document.querySelector('.sls-lua-btn[data-sls-appid="' + appid + '"]');
                                if (!btn) return;
                                var currentStatus = btn.dataset.slsStatus;
                                if (currentStatus && currentStatus !== lastStatus) {
                                    lastStatus = currentStatus;
                                    statusEl.innerText = currentStatus;

                                    if (/installed!?/i.test(currentStatus)) {
                                        clearInterval(timer);
                                        statusEl.style.color = '#34d399';
                                        var pl = document.querySelector('.sls-provider-label');
                                        if (pl) pl.innerHTML = 'Lua Provider: <span style="color:#a5b4fc;font-weight:600;">' + luaProviders[idx].name + '</span>';
                                        var nb = document.querySelector('.sls-navi-btn[data-sls-appid="' + appid + '"]');
                                        if (nb) nb.dataset.slsProvider = luaProviders[idx].name;
                                        var np = document.getElementById('sls-navi-modal-provider');
                                        if (np) np.innerText = luaProviders[idx].name;
                                        provBtns.forEach(function(b) { b.disabled = false; b.style.opacity = '1'; b.style.cursor = 'pointer'; });
                                    }
                                    else if (/not available|failed|error|invalid/i.test(currentStatus)) {
                                        clearInterval(timer);
                                        statusEl.style.color = '#fca5a5';
                                        statusEl.style.background = 'rgba(239,68,68,0.1)';
                                        statusEl.style.borderColor = 'rgba(239,68,68,0.3)';
                                        provBtns.forEach(function(b) { b.disabled = false; b.style.opacity = '1'; b.style.cursor = 'pointer'; });
                                    }
                                }
                                pollCount++;
                                if (pollCount > 600) clearInterval(timer);
                            }, 500);

                        } else {
                            statusEl.innerText = 'Failed to start download.';
                            statusEl.style.color = '#fca5a5';
                        }
                    })
                    .catch(function() {
                        statusEl.innerText = 'Connection error.';
                        statusEl.style.color = '#fca5a5';
                    });
            };
        });
    }


    // --- remove-modal.js ---
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


    // --- restore-modal.js ---
    /**
     * @file  restore-modal.js
     * @brief Modal UI for browsing and restoring historical cloud save snapshots
     */
    function openRestoreModal(appId, onCompleted) {
        if (window.NaviCloudManager && window.NaviCloudManager.openRestoreModal) {
            return window.NaviCloudManager.openRestoreModal(appId, onCompleted);
        }
        if (document.getElementById('sls-restore-modal')) return;
        ensureStyles();
        var overlay = document.createElement('div');
        overlay.id = 'sls-restore-modal';
        overlay.setAttribute('role', 'dialog');
        overlay.setAttribute('aria-modal', 'true');
        overlay.setAttribute('data-testid', 'sls-restore-modal');
        overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.82);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);';
        overlay.innerHTML = RESTORE_MODAL_HTML.replace(/\{\{APPID\}\}/g, appId);
        document.body.appendChild(overlay);

        var closeBtn = overlay.querySelector('#sls-restore-close');
        var cancelBtn = overlay.querySelector('#sls-modal-cancel');
        var bodyEl = overlay.querySelector('#sls-restore-body');

        function closeModal() { overlay.remove(); }
        closeBtn.onclick = closeModal;
        cancelBtn.onclick = closeModal;

        fetchAppCloudStatus(appId).then(function(data) {
            var backups = data.backups || [];
            if (backups.length === 0) {
                bodyEl.innerHTML = '<div style="text-align:center;padding:24px;color:#9ca3af;font-size:14px;">No backups available for this game.</div>';
                trapSteamGamepadModal(overlay, [cancelBtn, closeBtn]);
                return;
            }
            bodyEl.innerHTML = '';
            var itemBtns = [];
            for (var i = 0; i < backups.length; i++) {
                var file = backups[i];
                var baseName = file.split('/').pop() || file;

                var row = document.createElement('div');
                row.style.cssText = 'display:flex;align-items:center;justify-content:space-between;padding:10px 12px;margin-bottom:6px;background:rgba(0,0,0,0.25);border:1px solid rgba(172,178,201,0.1);border-radius:3px;';

                var nameDiv = document.createElement('div');
                nameDiv.style.cssText = 'font-size:13px;color:#fff;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;margin-right:12px;';
                nameDiv.textContent = baseName;
                nameDiv.title = file;

                var btn = document.createElement('button');
                btn.className = 'sls-btn-restore-item Focusable';
                btn.setAttribute('tabindex', '-1');
                btn.setAttribute('role', 'button');
                btn.setAttribute('data-backup-path', file);
                btn.style.cssText = 'background:#2563eb;color:#fff;border:none;border-radius:2px;padding:6px 14px;font-size:13px;cursor:pointer;';
                btn.textContent = 'Restore';

                (function(restoreBtn, targetPath) {
                    restoreBtn.onclick = function() {
                        restoreBtn.disabled = true;
                        restoreBtn.textContent = 'Restoring...';
                        fetch('http://127.0.0.1:9001/cloud/apps/' + appId + '/restore', {
                            method: 'POST',
                            headers: { 'Content-Type': 'application/json', 'X-Navi-Auth': window.__naviAuthToken || '' },
                            body: JSON.stringify({ backupFile: targetPath })
                        }).then(function() {
                            closeModal();
                            alert('Restore enqueued. Please restart Steam via Navi to safely apply changes.');
                            if (onCompleted) onCompleted(true);
                        }).catch(function(e) {
                            restoreBtn.disabled = false;
                            restoreBtn.textContent = 'Restore';
                            alert('Restore error: ' + (e && e.message ? e.message : String(e)));
                        });
                    };
                })(btn, file);

                row.appendChild(nameDiv);
                row.appendChild(btn);
                bodyEl.appendChild(row);
                itemBtns.push(btn);
            }
            trapSteamGamepadModal(overlay, itemBtns.concat([cancelBtn, closeBtn]));
        }).catch(function(e) {
            bodyEl.innerHTML = '';
            var errEl = document.createElement('div');
            errEl.style.cssText = 'text-align:center;padding:24px;color:#f87171;';
            errEl.textContent = 'Error loading backups: ' + (e && e.message ? e.message : String(e));
            bodyEl.appendChild(errEl);
            trapSteamGamepadModal(overlay, [cancelBtn, closeBtn]);
        });
    }


    // --- reset-modal.js ---
    /**
     * @file  reset-modal.js
     * @brief Confirmation modal for resetting local save games from cloud backups
     */
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


    // --- navi-modal.js ---
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


    // --- buttons.js ---
    /**
     * @file  buttons.js
     * @brief DOM button injectors for Navi and CloudRedirect library actions
     */
    function makeSteamButton(appid, className, text, extraAttrs) {
        var isDesktop = !!((document.body && document.body.classList.contains('DesktopUI')) || document.querySelector('.DesktopUI'));
        var isDeckOrBigPicture = !isDesktop;
        var buttonMargin = 'margin-right: 10px;';
        var buttonHeight = isDeckOrBigPicture ? '48px' : '32px';
        var buttonPadding = isDeckOrBigPicture ? '8px 16px' : '0 16px';

        var steamBtnStyle = 'display: flex; align-items: center; justify-content: center; ' +
            'background: rgba(172, 178, 201, 0.14); color: rgb(220, 222, 223); ' +
            'border: none; border-radius: 2px; padding: ' + buttonPadding + '; ' + buttonMargin + ' ' +
            'font-size: 16px; font-weight: 400; font-family: "Motiva Sans", sans-serif; ' +
            'height: ' + buttonHeight + '; box-sizing: border-box; cursor: pointer; ' +
            'text-decoration: none; transition: background 0.1s ease;';

        var btn = document.createElement('div');
        btn.className = className + ' Focusable';
        btn.setAttribute('tabindex', '-1');
        btn.setAttribute('role', 'button');
        btn.dataset.slsAppid = appid;
        btn.dataset.slsAppId = appid;
        btn.style.cssText = steamBtnStyle;
        if (extraAttrs) {
            for (var k in extraAttrs) btn.setAttribute(k, extraAttrs[k]);
        }

        var label = document.createElement('span');
        label.style.cssText = 'white-space: nowrap;';
        label.innerText = text;
        btn.appendChild(label);

        // Gamepad focus styling (match Steam's focus ring)
        btn.addEventListener('vgp_onfocus', function() {
            btn.style.background = 'rgba(172, 178, 201, 0.30)';
            btn.style.outline = '2px solid white';
            btn.style.outlineOffset = '2px';
        });
        btn.addEventListener('vgp_onblur', function() {
            btn.style.background = 'rgba(172, 178, 201, 0.14)';
            btn.style.outline = 'none';
        });

        // Hover styling
        btn.addEventListener('mouseenter', function() {
            btn.style.background = 'rgba(172, 178, 201, 0.25)';
        });
        btn.addEventListener('mouseleave', function() {
            btn.style.background = 'rgba(172, 178, 201, 0.14)';
        });

        return btn;
    }

    function createRemoveButton(manageContainer, appid, isPending, onlineFixInstalled, autoCrackInstalled, provider) {
        var parentNode = manageContainer.parentNode;

        // Remove any existing SLS buttons from this container before re-inserting
        parentNode.querySelectorAll(
            '.sls-remove-lua-btn, .sls-config-btn, .sls-provider-label, .sls-change-provider-btn, .sls-navi-btn, .sls-cloud-toggle-btn, .sls-cloud-backup-btn, .sls-cloud-restore-btn, .sls-cloud-reset-btn'
        ).forEach(function(el) { el.remove(); });

        // Detect Steam Deck / Big Picture Mode (GamepadUI) vs Desktop Mode
        var isDesktop = !!((document.body && document.body.classList.contains('DesktopUI')) || document.querySelector('.DesktopUI'));
        var isDeckOrBigPicture = !isDesktop;
        var buttonMargin = 'margin-right: 10px;';
        var buttonHeight = isDeckOrBigPicture ? '48px' : '32px';

        // On Big Picture / Steam Deck, use a single compact "Navi" menu button
        // to prevent overflowing the action row and covering other elements.
        if (isDeckOrBigPicture) {
            var naviBtn = makeSteamButton(appid, 'sls-navi-btn sls-lua-btn', isPending ? 'Navi (Restart)' : 'Navi');
            naviBtn.dataset.slsProvider = provider || '';
            if (isPending) {
                naviBtn.dataset.slsPending = 'true';
                naviBtn.style.color = '#fde047';
            }
            naviBtn.style.marginRight = '10px';
            naviBtn.style.marginLeft = '0px';

            function setDeckRestartState() {
                isPending = true;
                naviBtn.dataset.slsPending = 'true';
                var span = naviBtn.querySelector('span');
                if (span) span.innerText = 'Navi (Restart)';
                naviBtn.style.color = '#fde047';
            }

            naviBtn.addEventListener('vgp_onok', function(e) {
                e.preventDefault(); e.stopPropagation();
                naviBtn.click();
            });

            naviBtn.onclick = function(e) {
                e.preventDefault();
                e.stopPropagation();
                var currentProv = naviBtn.dataset.slsProvider || provider;
                var currentPending = naviBtn.dataset.slsPending === 'true' || isPending;
                openNaviModal(appid, currentPending, onlineFixInstalled, autoCrackInstalled, currentProv, function() {
                    setDeckRestartState();
                });
            };

            parentNode.dataset.slsInjected = appid;
            parentNode.insertBefore(naviBtn, parentNode.firstChild);

            setTimeout(function() {
                registerSteamFocusNode(naviBtn);
            }, 10);
            return;
        }

        // 1. Remove Lua Button (Desktop Mode)
        var removeBtn = makeSteamButton(appid, 'sls-remove-lua-btn sls-lua-btn', 'Remove Lua');
        var removeBtnLabel = removeBtn.querySelector('span');

        removeBtn.addEventListener('vgp_onok', function(e) {
            e.preventDefault(); e.stopPropagation();
            removeBtn.click();
        });

        // 2. Config Button (Desktop Mode)
        var configBtn = makeSteamButton(appid, 'sls-config-btn', 'Config');

        configBtn.addEventListener('vgp_onok', function(e) {
            e.preventDefault(); e.stopPropagation();
            configBtn.click();
        });

        configBtn.onclick = function(e) {
            e.preventDefault();
            e.stopPropagation();
            openSlsConfig(appid);
        };

        function setRestartState() {
            removeBtnLabel.innerText = 'Restart Steam...';
            removeBtn.onclick = function(e) {
                e.preventDefault(); e.stopPropagation();
                log('Restart requested');
                fetch('http://127.0.0.1:9001/restart', { mode: 'no-cors' }).catch(function(){});
            };
        }

        if (isPending) {
            setRestartState();
        }

        removeBtn.onclick = function(e) {
            e.preventDefault();
            e.stopPropagation();
            openRemoveConfirmModal(appid, function() {
                setRestartState();
            });
        };

        // 3. Change Provider Button (Desktop Mode)
        var changeProvBtn = makeSteamButton(appid, 'sls-change-provider-btn', 'Change Provider');

        changeProvBtn.addEventListener('vgp_onok', function(e) {
            e.preventDefault(); e.stopPropagation();
            changeProvBtn.click();
        });

        changeProvBtn.onclick = function(e) {
            e.preventDefault();
            e.stopPropagation();
            openChangeProviderModal(appid, provider || '');
        };

        // 4. Provider label (Desktop Mode)
        var providerLabel = document.createElement('div');
        providerLabel.className = 'sls-provider-label';
        providerLabel.style.cssText = 'display: flex; align-items: center; justify-content: center; ' +
            'background: transparent; color: rgb(139, 147, 164); ' +
            'border: none; border-radius: 2px; padding: 0 12px; ' + buttonMargin + ' ' +
            'font-size: 14px; font-weight: 400; font-family: "Motiva Sans", sans-serif; ' +
            'height: ' + buttonHeight + '; box-sizing: border-box; white-space: nowrap;';
        if (provider) {
            providerLabel.innerHTML = 'Provider: <span style="color:rgb(220, 222, 223);font-weight:500;margin-left:4px;">' + provider + '</span>';
        }

        // 5. Cloud Save Redirection Controls (Desktop Mode)
        var cloudToggleBtn = makeSteamButton(appid, 'sls-cloud-toggle-btn', 'Cloud Save: ...');
        var cloudBackupBtn = makeSteamButton(appid, 'sls-cloud-backup-btn', 'Backup Save');
        var cloudRestoreBtn = makeSteamButton(appid, 'sls-cloud-restore-btn', 'Restore Save');
        var cloudResetBtn = makeSteamButton(appid, 'sls-cloud-reset-btn', 'Reset Save');
        cloudResetBtn.style.color = '#f87171';

        var isRowCloudActive = false;
        function updateRowCloudUI(active) {
            isRowCloudActive = !!active;
            var span = cloudToggleBtn.querySelector('span');
            if (span) {
                if (isRowCloudActive) {
                    span.innerHTML = 'Cloud Save: <b style="color:#34d399;">Active</b>';
                } else {
                    span.innerHTML = 'Cloud Save: <b style="color:#9ca3af;">Inactive</b>';
                }
            }
        }

        fetchAppCloudStatus(appid).then(function(data) {
            updateRowCloudUI(data.redirected);
        }).catch(function() {
            updateRowCloudUI(false);
        });

        cloudToggleBtn.addEventListener('vgp_onok', function(e) {
            e.preventDefault(); e.stopPropagation();
            cloudToggleBtn.click();
        });
        cloudToggleBtn.onclick = function(e) {
            e.preventDefault(); e.stopPropagation();
            var target = !isRowCloudActive;
            var span = cloudToggleBtn.querySelector('span');
            if (span) span.innerText = 'Updating...';
            toggleAppCloudRedirect(appid, target).then(function() {
                updateRowCloudUI(target);
            }).catch(function() {
                updateRowCloudUI(isRowCloudActive);
            });
        };

        cloudBackupBtn.addEventListener('vgp_onok', function(e) {
            e.preventDefault(); e.stopPropagation();
            cloudBackupBtn.click();
        });
        cloudBackupBtn.onclick = function(e) {
            e.preventDefault(); e.stopPropagation();
            var span = cloudBackupBtn.querySelector('span');
            if (span) span.innerText = 'Backing up...';
            backupAppCloudSave(appid).then(function() {
                if (span) span.innerText = 'Saved!';
                setTimeout(function() { if (span) span.innerText = 'Backup Save'; }, 2000);
            }).catch(function() {
                if (span) span.innerText = 'Backup Save';
            });
        };

        cloudRestoreBtn.addEventListener('vgp_onok', function(e) {
            e.preventDefault(); e.stopPropagation();
            cloudRestoreBtn.click();
        });
        cloudRestoreBtn.onclick = function(e) {
            e.preventDefault(); e.stopPropagation();
            openRestoreModal(appid);
        };

        cloudResetBtn.addEventListener('vgp_onok', function(e) {
            e.preventDefault(); e.stopPropagation();
            cloudResetBtn.click();
        });
        cloudResetBtn.onclick = function(e) {
            e.preventDefault(); e.stopPropagation();
            openResetModal(appid);
        };

        // Mark this container as injected BEFORE inserting DOM nodes so the
        // MutationObserver callback that fires during insertion sees the guard
        // already set and skips re-entry.
        parentNode.dataset.slsInjected = appid;

        // Insert SLS buttons BEFORE the Manage gear (manageContainer),
        // so they appear first in the row. Reverse insertion order keeps
        // visual order: Provider Label → Remove Lua → Config → Change Provider → Cloud Toggle → Backup → Restore → Reset.
        parentNode.insertBefore(cloudResetBtn, parentNode.firstChild);
        parentNode.insertBefore(cloudRestoreBtn, parentNode.firstChild);
        parentNode.insertBefore(cloudBackupBtn, parentNode.firstChild);
        parentNode.insertBefore(cloudToggleBtn, parentNode.firstChild);
        parentNode.insertBefore(changeProvBtn, parentNode.firstChild);
        parentNode.insertBefore(configBtn, parentNode.firstChild);
        parentNode.insertBefore(removeBtn, parentNode.firstChild);
        if (provider) parentNode.insertBefore(providerLabel, parentNode.firstChild);

        // Register for Gamepad Focus
        setTimeout(function() {
            registerSteamFocusNode(removeBtn);
            registerSteamFocusNode(configBtn);
            registerSteamFocusNode(changeProvBtn);
            registerSteamFocusNode(cloudToggleBtn);
            registerSteamFocusNode(cloudBackupBtn);
            registerSteamFocusNode(cloudRestoreBtn);
            registerSteamFocusNode(cloudResetBtn);
        }, 10);
    }


    // --- main.js ---
    /**
     * @file  main.js
     * @brief Modular entry point for library app details page enhancements
     */
    var noLuaAppIds = {};
    var pendingChecks = {}; // keyed by appid + '_' + container-random-key

    function checkAndCreateButton(manageContainer, appid, retryCount) {
        if (!retryCount) retryCount = 0;

        var parentNode = manageContainer.parentNode;
        var containerKey = appid + '_' + (parentNode ? parentNode.dataset.slsKey : appid);

        if (retryCount > 5) {
            delete pendingChecks[containerKey];
            return;
        }

        fetch('http://127.0.0.1:9001/check?id=' + appid)
            .then(function(r) { return r.json(); })
            .then(function(data) {
                delete pendingChecks[containerKey];

                if (data.exists || data.hasLua || (data.provider && data.provider.length > 0) || data.pending) {
                    if (parentNode) delete parentNode.dataset.slsNoLua;
                    createRemoveButton(manageContainer, appid, data.pending, data.onlineFixInstalled, data.autoCrackInstalled, data.provider);
                } else {
                    if (parentNode) parentNode.dataset.slsNoLua = appid;
                }
            })
            .catch(function() {
                var delay = Math.min(1000 * Math.pow(2, retryCount), 8000);
                log('check failed for ' + appid + ', retry #' + (retryCount + 1) + ' in ' + delay + 'ms');
                setTimeout(function() {
                    if (manageContainer.parentNode) {
                        checkAndCreateButton(manageContainer, appid, retryCount + 1);
                    } else {
                        delete pendingChecks[containerKey];
                    }
                }, delay);
            });
    }

    function addRemoveLuaButton() {
        var btns = document.querySelectorAll('div[aria-label="Manage"]');

        btns.forEach(function(manageBtn) {
            var manageContainer = manageBtn.parentNode;
            if (!manageContainer || !manageContainer.parentNode) return;

            var appid = extractAppId(manageBtn);
            if (!appid) return;

            var parentNode = manageContainer.parentNode;

            // If this exact container is already injected for this appid, skip
            if (parentNode.dataset.slsInjected === appid) return;

            // If this container was already checked and confirmed to have no lua, skip
            if (parentNode.dataset.slsNoLua === appid) return;

            // If a /check is already in-flight for this appid+container, skip
            var containerKey = appid + '_' + (parentNode.dataset.slsKey || (parentNode.dataset.slsKey = Math.random().toString(36).slice(2)));
            if (pendingChecks[containerKey]) return;
            pendingChecks[containerKey] = true;

            checkAndCreateButton(manageContainer, appid, 0);
        });
    }

    var debounceTimer = null;
    function debouncedAddButtons() {
        if (debounceTimer) return;
        debounceTimer = requestAnimationFrame(function() {
            debounceTimer = null;
            addRemoveLuaButton();
        });
    }

    window.__slsRefreshButtons = function(targetAppId) {
        pendingChecks = {};
        document.querySelectorAll('[data-sls-injected], [data-sls-no-lua]').forEach(function(el) {
            if (!targetAppId || el.dataset.slsInjected === String(targetAppId) || el.dataset.slsNoLua === String(targetAppId)) {
                delete el.dataset.slsInjected;
                delete el.dataset.slsNoLua;
            }
        });
        addRemoveLuaButton();
    };

    var observer = new MutationObserver(debouncedAddButtons);
    if (document.body) observer.observe(document.body, { childList: true, subtree: true });
    ensureStyles();
    addRemoveLuaButton();

})();
