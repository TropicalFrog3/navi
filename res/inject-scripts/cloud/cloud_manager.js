/**
 * @file  cloud_manager.js
 * @brief Client-side CloudRedirect UI manager for Steam CEF settings
 */
/**
 * Navi Cloud Manager
 *
 * Implements Phase 5 CEF Manager UI & Security Guardrails:
 * - window.NaviCloudManager namespace
 * - Origin validation & secure backend requests with X-Navi-Auth header
 * - Polling of /cloud/status and /cloud/pending-ops
 * - Persistent restart warning banner for pending operations
 * - Master Cloud Saves toggle & Cloud Provider authentication controls
 * - Per-App Cloud Save status, toggle, backup, restore, and typed-reset guardrail
 */
(function() {
    'use strict';

    if (window.__naviCloudManagerLoaded) {
        return;
    }
    window.__naviCloudManagerLoaded = true;

    var BACKEND_ORIGIN = 'http://127.0.0.1:9001';
    var POLL_INTERVAL_MS = 5000;
    var pollTimer = null;
    var state = {
        enabled: false,
        provider: 'local-only',
        managedAppsCount: 0,
        pendingOpsCount: 0,
        hasPending: false,
        operations: []
    };

    /**
     * Check that the current window execution context is a trusted Steam CEF origin.
     */
    function isOriginCompliant() {
        try {
            var origin = window.location.origin;
            if (!origin || origin === 'null' || origin.indexOf('file:') === 0) {
                var href = window.location.href || '';
                if (href.indexOf('steamloopback.host') !== -1 ||
                    href.indexOf('steampowered.com') !== -1 ||
                    href.indexOf('chrome-extension:') === 0 ||
                    href.indexOf('devtools:') === 0 ||
                    href.indexOf('file:') === 0) {
                    return true;
                }
            }
            var host = window.location.hostname || '';
            if (host === 'steamloopback.host' ||
                host === '127.0.0.1' ||
                host === 'localhost' ||
                host === 'store.steampowered.com' ||
                host === 'steamcommunity.com' ||
                host.endsWith('.steampowered.com')) {
                return true;
            }
        } catch (e) {
            return false;
        }
        return false;
    }

    /**
     * Ensure the target URL is strictly directed to the local backend port 9001.
     */
    function validateTargetUrl(pathOrUrl) {
        if (typeof pathOrUrl !== 'string' || !pathOrUrl) return null;
        var parsed;
        try {
            parsed = new URL(pathOrUrl, BACKEND_ORIGIN);
        } catch (e) {
            return null;
        }
        if ((parsed.hostname !== '127.0.0.1' && parsed.hostname !== 'localhost') ||
            parsed.port !== '9001') {
            return null;
        }
        return parsed.href;
    }

    /**
     * Secure fetch wrapper:
     * - Enforces origin compliance
     * - Injects X-Navi-Auth header from window.__naviAuthToken
     * - Enforces loopback destination
     * - Supports naviFetch bridge fallback if CSP restricts direct fetch
     */
    function secureFetch(pathOrUrl, options) {
        if (!isOriginCompliant()) {
            return Promise.reject(new Error('Security check failed: untrusted origin ' + window.location.origin));
        }
        var targetUrl = validateTargetUrl(pathOrUrl);
        if (!targetUrl) {
            return Promise.reject(new Error('Security check failed: target URL is not a valid Navi backend loopback address'));
        }

        var opts = Object.assign({}, options || {});
        var method = String(opts.method || 'GET').toUpperCase();
        var headers = Object.assign({}, opts.headers || {});
        headers['X-Navi-Auth'] = window.__naviAuthToken || '';
        headers['Connection'] = 'close';
        if (opts.body && typeof opts.body === 'string' && !headers['Content-Type']) {
            headers['Content-Type'] = 'application/json';
        }
        opts.headers = headers;

        if (typeof window.naviFetch === 'function' && window.location.hostname.indexOf('steampowered.com') !== -1) {
            return window.naviFetch(targetUrl, opts);
        }

        return fetch(targetUrl, opts).catch(function(err) {
            if (method === 'GET') {
                return fetch(targetUrl);
            }
            if (method === 'POST' && opts.mode !== 'no-cors') {
                var fallbackBody = 'X-Navi-Auth: ' + (window.__naviAuthToken || '') + '\r\n' + (typeof opts.body === 'string' ? opts.body : '{}');
                return fetch(targetUrl, {
                    method: 'POST',
                    headers: { 'Content-Type': 'text/plain' },
                    body: fallbackBody
                });
            }
            throw err;
        });
    }

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

    /**
     * Render or update the persistent warning banner at the top of the viewport
     * when pending operations exist.
     */
    function updatePendingBanner(hasPending) {
        var existingBanner = document.getElementById('navi-cloud-pending-banner');
        if (!hasPending) {
            if (existingBanner) {
                existingBanner.remove();
            }
            return;
        }

        if (existingBanner) {
            return; // Banner is already visible
        }

        if (!document.body) {
            setTimeout(function() { updatePendingBanner(hasPending); }, 250);
            return;
        }

        var banner = document.createElement('div');
        banner.id = 'navi-cloud-pending-banner';
        banner.style.cssText = 'position: fixed; top: 0; left: 0; right: 0; z-index: 999998; ' +
            'background: linear-gradient(90deg, #78350f, #92400e); border-bottom: 2px solid #f59e0b; ' +
            'color: #fef3c7; padding: 10px 18px; display: flex; align-items: center; justify-content: space-between; ' +
            'font-family: "Motiva Sans", -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; ' +
            'box-shadow: 0 4px 14px rgba(0,0,0,0.6); box-sizing: border-box;';

        var textContainer = document.createElement('div');
        textContainer.style.cssText = 'display: flex; align-items: center; gap: 10px; font-size: 14px; font-weight: 500;';

        var icon = document.createElement('span');
        icon.style.cssText = 'font-size: 18px; color: #fbbf24; flex-shrink: 0;';
        icon.innerText = '⚠️';

        var text = document.createElement('span');
        text.innerText = 'Pending cloud save operation waiting for Steam restart. Please restart Steam via Navi to safely apply changes.';

        textContainer.appendChild(icon);
        textContainer.appendChild(text);

        var restartBtn = document.createElement('button');
        restartBtn.id = 'navi-banner-restart-btn';
        restartBtn.className = 'Focusable';
        restartBtn.setAttribute('tabindex', '-1');
        restartBtn.setAttribute('role', 'button');
        restartBtn.style.cssText = 'background: #f59e0b; color: #1e1e1e; border: none; border-radius: 3px; ' +
            'padding: 6px 16px; font-size: 13px; font-weight: 700; font-family: "Motiva Sans", sans-serif; ' +
            'cursor: pointer; white-space: nowrap; transition: background 0.15s ease; flex-shrink: 0; margin-left: 12px;';
        restartBtn.innerText = 'Restart Steam Now';

        restartBtn.addEventListener('mouseenter', function() { restartBtn.style.background = '#fbbf24'; });
        restartBtn.addEventListener('mouseleave', function() { restartBtn.style.background = '#f59e0b'; });
        restartBtn.onclick = function(e) {
            e.preventDefault();
            e.stopPropagation();
            restartBtn.innerText = 'Restarting...';
            restartBtn.disabled = true;
            restartBtn.style.opacity = '0.7';
            restartBtn.style.cursor = 'default';
            window.NaviCloudManager.restartSteam().catch(function(err) {
                console.warn('[NaviCloudManager] Restart error:', err);
            });
        };

        banner.appendChild(textContainer);
        banner.appendChild(restartBtn);
        document.body.appendChild(banner);
    }

    /**
     * API Functions
     */

    function fetchStatus() {
        return secureFetch('/cloud/status')
            .then(function(res) { return res.json(); })
            .then(function(data) {
                state.enabled = !!data.enabled;
                state.provider = data.provider || 'local-only';
                state.managedAppsCount = data.managedAppsCount || 0;
                state.pendingOpsCount = data.pendingOpsCount || 0;
                return data;
            });
    }

    function fetchPendingOps() {
        return secureFetch('/cloud/pending-ops')
            .then(function(res) { return res.json(); })
            .then(function(data) {
                state.operations = data.operations || [];
                var hasPending = !!data.hasPending;
                if (!hasPending && state.operations.length > 0) {
                    for (var i = 0; i < state.operations.length; i++) {
                        var st = String(state.operations[i].status || '').toUpperCase();
                        if (st === 'PENDING') {
                            hasPending = true;
                            break;
                        }
                    }
                }
                state.hasPending = hasPending;
                updatePendingBanner(hasPending || state.pendingOpsCount > 0);
                return data;
            });
    }

    function toggleMaster(enable) {
        var targetState = !!enable;
        var msg = targetState
            ? 'Enabling Cloud Saves requires a Steam restart to mount virtual save filesystems. Continue?'
            : 'Disabling Cloud Saves requires a Steam restart to detach virtual save filesystems. Continue?';
        var confirmed = window.confirm(msg);
        if (!confirmed) {
            return Promise.reject(new Error('User cancelled Cloud Saves toggle'));
        }

        return secureFetch('/cloud/toggle', {
            method: 'POST',
            body: JSON.stringify({ enable: targetState })
        })
            .then(function(res) { return res.json(); })
            .then(function(data) {
                state.enabled = targetState;
                poll();
                return data;
            });
    }

    function fetchProviderStatus(provider) {
        var p = provider || state.provider || 'local-only';
        return secureFetch('/cloud/provider/status?provider=' + encodeURIComponent(p))
            .then(function(res) { return res.json(); });
    }

    function signInProvider(provider) {
        var p = provider || state.provider || 'gdrive';
        return secureFetch('/cloud/provider/signin', {
            method: 'POST',
            body: JSON.stringify({ provider: p })
        })
            .then(function(res) { return res.json(); })
            .then(function(data) {
                if (data.authUrl) {
                    window.open(data.authUrl, '_blank');
                }
                return data;
            });
    }

    function signOutProvider(provider) {
        var p = provider || state.provider || 'gdrive';
        return secureFetch('/cloud/provider/signout', {
            method: 'POST',
            body: JSON.stringify({ provider: p })
        })
            .then(function(res) { return res.json(); });
    }

    function restartSteam() {
        return secureFetch('/restart', { method: 'POST', mode: 'no-cors' });
    }

    function fetchAppStatus(appId) {
        return secureFetch('/cloud/apps/' + appId + '/status')
            .then(function(res) { return res.json(); });
    }

    function toggleAppRedirect(appId, redirect) {
        return secureFetch('/cloud/apps/' + appId + '/toggle', {
            method: 'POST',
            body: JSON.stringify({ redirect: !!redirect })
        })
            .then(function(res) { return res.json(); });
    }

    function backupAppSave(appId) {
        return secureFetch('/cloud/apps/' + appId + '/backup', {
            method: 'POST'
        })
            .then(function(res) { return res.json(); });
    }

    function restoreAppSave(appId, backupFile) {
        return secureFetch('/cloud/apps/' + appId + '/restore', {
            method: 'POST',
            body: JSON.stringify({ backupFile: backupFile })
        })
            .then(function(res) { return res.json(); })
            .then(function(data) {
                poll();
                return data;
            });
    }

    function executeResetAppSave(appId) {
        return secureFetch('/cloud/apps/' + appId + '/reset', {
            method: 'POST',
            body: JSON.stringify({ confirm: 'RESET' })
        })
            .then(function(res) { return res.json(); })
            .then(function(data) {
                poll();
                return data;
            });
    }

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

        var card = document.createElement('div');
        card.style.cssText = 'background: #1e2024; border: 1px solid #7f1d1d; border-radius: 4px; padding: 24px; ' +
            'width: 460px; box-shadow: 0 8px 24px rgba(0,0,0,0.7); font-family: "Motiva Sans", sans-serif; color: #dcdedf;';

        card.innerHTML = '<div style="display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:16px;">' +
            '<div>' +
                '<h2 style="margin:0;font-size:20px;font-weight:600;color:#f87171;text-transform:uppercase;letter-spacing:1px;">Reset Cloud Save</h2>' +
                '<p style="margin:4px 0 0;font-size:13px;color:#969696;">AppID: ' + appId + '</p>' +
            '</div>' +
            '<button id="sls-reset-close" class="sls-btn-close Focusable" tabindex="-1" role="button" aria-label="Close" style="background:transparent;border:none;color:#969696;font-size:24px;cursor:pointer;">&times;</button>' +
        '</div>' +
        '<div style="background:rgba(185,28,28,0.15);border:1px solid rgba(185,28,28,0.3);border-radius:3px;padding:12px;margin-bottom:16px;">' +
            '<p style="margin:0;font-size:13px;line-height:1.5;color:#fca5a5;">' +
                '⚠️ <b>Warning:</b> This will permanently erase cloud-redirected save data for this app. This action cannot be reversed.' +
            '</p>' +
        '</div>' +
        '<div style="margin-bottom:20px;">' +
            '<label style="display:block;font-size:13px;color:#dcdedf;margin-bottom:8px;">' +
                'To confirm, type <b style="color:#fff;letter-spacing:1px;">RESET</b> into the box below:' +
            '</label>' +
            '<input type="text" id="sls-reset-input" autocomplete="off" spellcheck="false" placeholder="Type RESET to confirm" ' +
                'style="width:100%;height:38px;padding:0 12px;background:#121417;border:1px solid #4b5563;border-radius:2px;color:#fff;font-size:14px;box-sizing:border-box;outline:none;" />' +
        '</div>' +
        '<div style="display:flex;justify-content:flex-end;gap:10px;">' +
            '<button id="sls-modal-cancel" class="sls-btn-action Focusable" tabindex="-1" role="button" style="height:36px;padding:0 20px;background:rgba(172,178,201,0.14);border:none;border-radius:2px;color:#dcdedf;cursor:pointer;">Cancel</button>' +
            '<button id="sls-modal-confirm" class="sls-btn-action Focusable" tabindex="-1" role="button" disabled style="height:36px;padding:0 20px;background:#dc2626;border:none;border-radius:2px;color:#fff;cursor:default;opacity:0.4;font-weight:600;">Confirm Reset</button>' +
        '</div>';

        overlay.appendChild(card);
        document.body.appendChild(overlay);

        var closeBtn = card.querySelector('#sls-reset-close');
        var cancelBtn = card.querySelector('#sls-modal-cancel');
        var confirmBtn = card.querySelector('#sls-modal-confirm');
        var input = card.querySelector('#sls-reset-input');

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

    /**
     * Modal listing available backups with Restore button
     */
    function openRestoreModal(appId, onCompleted) {
        if (document.getElementById('sls-restore-modal')) return;

        var overlay = document.createElement('div');
        overlay.id = 'sls-restore-modal';
        overlay.setAttribute('role', 'dialog');
        overlay.setAttribute('aria-modal', 'true');
        overlay.setAttribute('data-testid', 'sls-restore-modal');
        overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.82);' +
            'z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);';

        var card = document.createElement('div');
        card.style.cssText = 'background: #1e2024; border: 1px solid #3d4450; border-radius: 4px; padding: 24px; ' +
            'width: 480px; box-shadow: 0 8px 24px rgba(0,0,0,0.7); font-family: "Motiva Sans", sans-serif; color: #dcdedf;';

        card.innerHTML = '<div style="display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:16px;">' +
            '<div>' +
                '<h2 style="margin:0;font-size:20px;font-weight:600;color:#fff;text-transform:uppercase;letter-spacing:1px;">Restore Cloud Save</h2>' +
                '<p style="margin:4px 0 0;font-size:13px;color:#969696;">AppID: ' + appId + '</p>' +
            '</div>' +
            '<button id="sls-restore-close" class="sls-btn-close Focusable" tabindex="-1" role="button" aria-label="Close" style="background:transparent;border:none;color:#969696;font-size:24px;cursor:pointer;">&times;</button>' +
        '</div>' +
        '<div id="sls-restore-body" style="max-height:300px;overflow-y:auto;margin-bottom:20px;">' +
            '<div style="text-align:center;padding:24px;color:#969696;">Loading backups...</div>' +
        '</div>' +
        '<div style="display:flex;justify-content:flex-end;gap:10px;">' +
            '<button id="sls-modal-cancel" class="sls-btn-action Focusable" tabindex="-1" role="button" style="height:36px;padding:0 20px;background:rgba(172,178,201,0.14);border:none;border-radius:2px;color:#dcdedf;cursor:pointer;">Close</button>' +
        '</div>';

        overlay.appendChild(card);
        document.body.appendChild(overlay);

        var closeBtn = card.querySelector('#sls-restore-close');
        var cancelBtn = card.querySelector('#sls-modal-cancel');
        var bodyEl = card.querySelector('#sls-restore-body');

        function closeModal() {
            overlay.remove();
        }

        closeBtn.onclick = closeModal;
        cancelBtn.onclick = closeModal;

        fetchAppStatus(appId).then(function(data) {
            var backups = data.backups || [];
            if (backups.length === 0) {
                bodyEl.innerHTML = '<div style="text-align:center;padding:24px;color:#9ca3af;font-size:14px;">No backups available for this game.</div>';
                trapSteamGamepadModal(overlay, [cancelBtn, closeBtn]);
                return;
            }

            var html = '';
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
                btn.style.cssText = 'background:#2563eb;color:#fff;border:none;border-radius:2px;padding:6px 14px;font-size:13px;font-weight:500;cursor:pointer;white-space:nowrap;';
                btn.textContent = 'Restore';

                (function(restoreBtn, targetPath) {
                    restoreBtn.onclick = function(e) {
                        e.preventDefault();
                        restoreBtn.disabled = true;
                        restoreBtn.textContent = 'Restoring...';
                        restoreAppSave(appId, targetPath)
                            .then(function() {
                                closeModal();
                                alert('Restore enqueued for Steam restart. Please restart Steam to complete restore.');
                                if (typeof onCompleted === 'function') onCompleted(true);
                            })
                            .catch(function(err) {
                                restoreBtn.disabled = false;
                                restoreBtn.textContent = 'Restore';
                                alert('Failed to restore backup: ' + (err && err.message ? err.message : String(err)));
                            });
                    };
                })(btn, file);

                row.appendChild(nameDiv);
                row.appendChild(btn);
                bodyEl.appendChild(row);
                itemBtns.push(btn);
            }

            trapSteamGamepadModal(overlay, itemBtns.concat([cancelBtn, closeBtn]));
        }).catch(function(err) {
            bodyEl.innerHTML = '';
            var errEl = document.createElement('div');
            errEl.style.cssText = 'text-align:center;padding:24px;color:#f87171;font-size:14px;';
            errEl.textContent = 'Failed to load backups: ' + (err && err.message ? err.message : String(err));
            bodyEl.appendChild(errEl);
            trapSteamGamepadModal(overlay, [cancelBtn, closeBtn]);
        });
    }

    /**
     * Global Settings & Provider Authentication Modal
     */
    function openSettingsModal() {
        if (document.getElementById('sls-cloud-settings-modal')) return;

        var overlay = document.createElement('div');
        overlay.id = 'sls-cloud-settings-modal';
        overlay.setAttribute('role', 'dialog');
        overlay.setAttribute('aria-modal', 'true');
        overlay.setAttribute('data-testid', 'sls-cloud-settings-modal');
        overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.82);' +
            'z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);';

        var card = document.createElement('div');
        card.style.cssText = 'background: #1e2024; border: 1px solid #3d4450; border-radius: 4px; padding: 24px; ' +
            'width: 500px; box-shadow: 0 8px 24px rgba(0,0,0,0.7); font-family: "Motiva Sans", sans-serif; color: #dcdedf;';

        var providers = [
            { id: 'local-only', name: 'Local Diverted Storage (Offline)' },
            { id: 'gdrive', name: 'Google Drive' },
            { id: 'onedrive', name: 'OneDrive' },
            { id: 'r2', name: 'Cloudflare R2' },
            { id: 's3', name: 'Amazon S3' }
        ];

        var optHtml = '';
        for (var i = 0; i < providers.length; i++) {
            optHtml += '<option value="' + providers[i].id + '">' + providers[i].name + '</option>';
        }

        card.innerHTML = '<div style="display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:20px;">' +
            '<div>' +
                '<h2 style="margin:0;font-size:22px;font-weight:300;color:#fff;text-transform:uppercase;letter-spacing:1px;">Cloud Saves Manager</h2>' +
                '<p style="margin:4px 0 0;font-size:13px;color:#969696;">Configure cloud save redirection & storage providers</p>' +
            '</div>' +
            '<button id="sls-settings-close" class="sls-btn-close Focusable" tabindex="-1" role="button" aria-label="Close" style="background:transparent;border:none;color:#969696;font-size:24px;cursor:pointer;">&times;</button>' +
        '</div>' +
        '<!-- Master Toggle -->' +
        '<div style="display:flex;align-items:center;justify-content:space-between;padding:14px;background:rgba(0,0,0,0.25);border:1px solid rgba(172,178,201,0.12);border-radius:3px;margin-bottom:16px;">' +
            '<div>' +
                '<div style="font-size:15px;font-weight:500;color:#fff;">Master Cloud Saves</div>' +
                '<div style="font-size:12px;color:#9ca3af;">Enable or disable save redirection globally</div>' +
            '</div>' +
            '<button id="sls-master-toggle-btn" class="Focusable" tabindex="-1" role="button" style="height:34px;padding:0 18px;border-radius:2px;border:none;font-weight:600;font-size:13px;cursor:pointer;">Checking...</button>' +
        '</div>' +
        '<!-- Provider Selector -->' +
        '<div style="margin-bottom:16px;">' +
            '<label style="display:block;font-size:13px;color:#9ca3af;margin-bottom:6px;">Cloud Provider</label>' +
            '<select id="sls-provider-select" style="width:100%;height:38px;background:#121417;border:1px solid #4b5563;border-radius:2px;color:#fff;font-size:14px;padding:0 10px;outline:none;">' +
                optHtml +
            '</select>' +
        '</div>' +
        '<!-- Provider Auth Block -->' +
        '<div id="sls-provider-auth-block" style="padding:14px;background:rgba(0,0,0,0.25);border:1px solid rgba(172,178,201,0.12);border-radius:3px;margin-bottom:20px;">' +
            '<div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:8px;">' +
                '<span style="font-size:14px;color:#dcdedf;">Authentication Status:</span>' +
                '<span id="sls-auth-badge" role="status" aria-live="polite" style="padding:3px 8px;border-radius:2px;font-size:12px;font-weight:600;">Checking...</span>' +
            '</div>' +
            '<div id="sls-auth-email" style="font-size:12px;color:#9ca3af;margin-bottom:10px;display:none;"></div>' +
            '<div id="sls-auth-actions" style="display:flex;justify-content:flex-end;"></div>' +
        '</div>' +
        '<!-- Footer Actions -->' +
        '<div style="display:flex;justify-content:flex-end;gap:10px;">' +
            '<button id="sls-modal-cancel" class="sls-btn-action Focusable" tabindex="-1" role="button" style="height:36px;padding:0 24px;background:rgba(172,178,201,0.14);border:none;border-radius:2px;color:#dcdedf;cursor:pointer;">Close</button>' +
        '</div>';

        overlay.appendChild(card);
        document.body.appendChild(overlay);

        var closeBtn = card.querySelector('#sls-settings-close');
        var cancelBtn = card.querySelector('#sls-modal-cancel');
        var masterBtn = card.querySelector('#sls-master-toggle-btn');
        var selectEl = card.querySelector('#sls-provider-select');
        var badgeEl = card.querySelector('#sls-auth-badge');
        var emailEl = card.querySelector('#sls-auth-email');
        var actionsEl = card.querySelector('#sls-auth-actions');

        function closeModal() {
            overlay.remove();
        }

        closeBtn.onclick = closeModal;
        cancelBtn.onclick = closeModal;

        function refreshMasterUI(enabled) {
            if (enabled) {
                masterBtn.innerText = 'Enabled';
                masterBtn.style.background = '#059669';
                masterBtn.style.color = '#fff';
            } else {
                masterBtn.innerText = 'Disabled';
                masterBtn.style.background = 'rgba(172,178,201,0.14)';
                masterBtn.style.color = '#9ca3af';
            }
        }

        masterBtn.onclick = function(e) {
            e.preventDefault();
            toggleMaster(!state.enabled)
                .then(function() {
                    refreshMasterUI(state.enabled);
                })
                .catch(function() {});
        };

        function refreshProviderAuth(p) {
            badgeEl.innerText = 'Checking...';
            badgeEl.style.background = 'rgba(172,178,201,0.1)';
            badgeEl.style.color = '#9ca3af';
            actionsEl.innerHTML = '';
            emailEl.style.display = 'none';

            fetchProviderStatus(p).then(function(res) {
                if (res.authenticated) {
                    badgeEl.innerText = 'Connected ✓';
                    badgeEl.style.background = 'rgba(16,185,129,0.2)';
                    badgeEl.style.color = '#34d399';
                    badgeEl.style.border = '1px solid rgba(16,185,129,0.4)';
                    if (res.accountEmail) {
                        emailEl.innerText = 'Account: ' + res.accountEmail;
                        emailEl.style.display = 'block';
                    }
                } else {
                    badgeEl.innerText = 'Not Connected';
                    badgeEl.style.background = 'rgba(239,68,68,0.2)';
                    badgeEl.style.color = '#f87171';
                    badgeEl.style.border = '1px solid rgba(239,68,68,0.4)';
                }

                if (p === 'gdrive' || p === 'onedrive') {
                    if (res.authenticated) {
                        var signoutBtn = document.createElement('button');
                        signoutBtn.className = 'Focusable';
                        signoutBtn.setAttribute('tabindex', '-1');
                        signoutBtn.setAttribute('role', 'button');
                        signoutBtn.style.cssText = 'background:rgba(239,68,68,0.2);color:#fca5a5;border:1px solid rgba(239,68,68,0.4);border-radius:2px;padding:6px 14px;font-size:13px;cursor:pointer;font-weight:500;';
                        signoutBtn.innerText = 'Disconnect';
                        signoutBtn.onclick = function() {
                            signoutBtn.innerText = 'Disconnecting...';
                            signOutProvider(p).then(function() {
                                refreshProviderAuth(p);
                            });
                        };
                        actionsEl.appendChild(signoutBtn);
                    } else {
                        var signinBtn = document.createElement('button');
                        signinBtn.className = 'Focusable';
                        signinBtn.setAttribute('tabindex', '-1');
                        signinBtn.setAttribute('role', 'button');
                        signinBtn.style.cssText = 'background:#2563eb;color:#fff;border:none;border-radius:2px;padding:6px 14px;font-size:13px;cursor:pointer;font-weight:500;';
                        signinBtn.innerText = 'Connect';
                        signinBtn.onclick = function() {
                            signinBtn.innerText = 'Opening auth...';
                            signInProvider(p).then(function() {
                                var checkInterval = setInterval(function() {
                                    if (!document.body.contains(overlay)) {
                                        clearInterval(checkInterval);
                                        return;
                                    }
                                    fetchProviderStatus(p).then(function(s) {
                                        if (s.authenticated) {
                                            clearInterval(checkInterval);
                                            refreshProviderAuth(p);
                                        }
                                    });
                                }, 2000);
                            });
                        };
                        actionsEl.appendChild(signinBtn);
                    }
                }
            }).catch(function() {
                badgeEl.innerText = 'Unavailable';
            });
        }

        fetchStatus().then(function(s) {
            refreshMasterUI(s.enabled);
            if (s.provider) {
                selectEl.value = s.provider;
            }
            refreshProviderAuth(selectEl.value);
        });

        selectEl.onchange = function() {
            refreshProviderAuth(selectEl.value);
        };

        trapSteamGamepadModal(overlay, [masterBtn, selectEl, cancelBtn, closeBtn]);
    }

    /**
     * Format byte count matching Valve's Steam Cloud page ("4.46 KB", "1.42 MB", "581 B")
     */
    function formatBytes(bytes) {
        var n = Number(bytes) || 0;
        if (n < 1024) return n + ' B';
        if (n < 1024 * 1024) return (n / 1024).toFixed(2).replace(/\.00$/, '') + ' KB';
        if (n < 1024 * 1024 * 1024) return (n / (1024 * 1024)).toFixed(2).replace(/\.00$/, '') + ' MB';
        return (n / (1024 * 1024 * 1024)).toFixed(2) + ' GB';
    }

    /**
     * Format Unix seconds matching Valve's Steam Cloud page ("3 Mar @ 1:48pm")
     */
    function formatSteamCloudDate(unixSec) {
        var ts = Number(unixSec) || 0;
        if (!ts) return '—';
        var d = new Date(ts * 1000);
        if (isNaN(d.getTime())) return '—';
        var months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
        var day = d.getDate();
        var mon = months[d.getMonth()];
        var hrs = d.getHours();
        var mins = d.getMinutes();
        var ampm = hrs >= 12 ? 'pm' : 'am';
        hrs = hrs % 12;
        if (hrs === 0) hrs = 12;
        var minStr = mins < 10 ? ('0' + mins) : String(mins);
        return day + ' ' + mon + ' @ ' + hrs + ':' + minStr + ampm;
    }

    var titleCache = {};
    try {
        titleCache = JSON.parse(localStorage.getItem('navi_app_titles_cache') || '{}');
    } catch (e) {}

    function isPlaceholderTitle(title, appId) {
        if (!title || typeof title !== 'string') return true;
        var t = title.trim();
        if (t.indexOf('App ') === 0 || t.indexOf('app ') === 0 ||
            t.indexOf('App_') === 0 || t.indexOf('app_') === 0) {
            return true;
        }
        if (appId && t === String(appId)) return true;
        return false;
    }

    /**
     * Resolve human-readable Game Title from Steam client appStore, backend title, or AppID fallback
     */
    function resolveGameTitle(appId, backendTitle) {
        var idNum = Number(appId);
        var sId = String(appId);
        if (titleCache[sId]) {
            if (!isPlaceholderTitle(titleCache[sId], sId)) {
                return titleCache[sId];
            }
            delete titleCache[sId];
            try { localStorage.setItem('navi_app_titles_cache', JSON.stringify(titleCache)); } catch (e) {}
        }
        try {
            if (window.appStore && typeof window.appStore.GetAppOverviewByAppID === 'function') {
                var ov = window.appStore.GetAppOverviewByAppID(idNum);
                if (ov && ov.display_name && !isPlaceholderTitle(ov.display_name, sId)) return String(ov.display_name);
            }
            if (window.opener && window.opener.appStore && typeof window.opener.appStore.GetAppOverviewByAppID === 'function') {
                var ov2 = window.opener.appStore.GetAppOverviewByAppID(idNum);
                if (ov2 && ov2.display_name && !isPlaceholderTitle(ov2.display_name, sId)) return String(ov2.display_name);
            }
        } catch (e) {}
        if (backendTitle && !isPlaceholderTitle(backendTitle, sId)) {
            return backendTitle;
        }
        return backendTitle || ('App ' + appId);
    }

    /**
     * Fetch full Steam Cloud apps inventory (local + remote cloud scan)
     */
    function fetchCloudApps(provider) {
        var url = '/cloud/apps';
        if (provider) {
            url += '?provider=' + encodeURIComponent(provider);
        }
        return secureFetch(url, { method: 'GET' })
            .then(function(res) { return res.json(); });
    }

    /**
     * Fetch per-game Steam Cloud file list (FOLDER, FILE NAME, FILE SIZE, DATE WRITTEN)
     */
    function fetchAppCloudFiles(appId, provider) {
        var url = '/cloud/apps/' + encodeURIComponent(appId) + '/files';
        if (provider) {
            url += '?provider=' + encodeURIComponent(provider);
        }
        return secureFetch(url, { method: 'GET' })
            .then(function(res) { return res.json(); });
    }

    /**
     * Trigger immediate Cloud Save sync for a specific game
     */
    function syncAppNow(appId) {
        return secureFetch('/cloud/apps/' + encodeURIComponent(appId) + '/sync', {
            method: 'POST',
            body: JSON.stringify({})
        }).then(function(res) { return res.json(); });
    }

    /**
     * Download a specific cloud save file to ~/Downloads/<appId>_<filename> and trigger browser download
     */
    function downloadAppCloudFile(appId, fileName, provider) {
        return secureFetch('/cloud/apps/' + encodeURIComponent(appId) + '/files/download', {
            method: 'POST',
            body: JSON.stringify({
                fileName: String(fileName || ''),
                provider: String(provider || state.provider || '')
            })
        }).then(function(res) {
            return res.json();
        });
    }

    /**
     * Fetch R2/S3 provider credentials (masked secret)
     */
    function fetchProviderCredentials(provider) {
        var p = provider || state.provider || 'r2';
        return secureFetch('/cloud/provider/credentials?provider=' + encodeURIComponent(p), {
            method: 'GET'
        }).then(function(res) {
            return res.json();
        });
    }

    /**
     * Save R2/S3 provider credentials and verify connection
     */
    function saveProviderCredentials(payload) {
        return secureFetch('/cloud/provider/credentials', {
            method: 'POST',
            body: JSON.stringify(payload || {})
        }).then(function(res) {
            return res.json();
        });
    }

    /**
     * Pre-flight scan of a cloud save provider for migration
     */
    function scanMigrationSource(provider) {
        var p = provider || state.provider || 'local-only';
        return secureFetch('/cloud/migrate/scan?provider=' + encodeURIComponent(p), {
            method: 'GET'
        }).then(function(res) {
            return res.json();
        });
    }

    /**
     * Migrate cloud save blobs from sourceProvider to targetProvider
     */
    function migrateProviderData(sourceProvider, targetProvider, switchActiveProvider) {
        return secureFetch('/cloud/migrate', {
            method: 'POST',
            body: JSON.stringify({
                sourceProvider: String(sourceProvider || ''),
                targetProvider: String(targetProvider || ''),
                switchActiveProvider: switchActiveProvider !== false
            })
        }).then(function(res) {
            return res.json();
        });
    }

    /**
     * Scan for orphaned save directories and unreferenced cloud blobs
     */
    function fetchOrphanedSaves(provider) {
        var p = provider || state.provider || 'local-only';
        return secureFetch('/cloud/orphans?provider=' + encodeURIComponent(p), {
            method: 'GET'
        }).then(function(res) {
            return res.json();
        });
    }

    /**
     * Garbage-collect unreferenced blobs or clean orphaned save directory for an appId
     */
    function cleanOrphanedApp(appId, provider, deleteFiles) {
        return secureFetch('/cloud/orphans/clean', {
            method: 'POST',
            body: JSON.stringify({
                appId: Number(appId) || 0,
                provider: String(provider || state.provider || 'local-only'),
                deleteFiles: !!deleteFiles
            })
        }).then(function(res) {
            return res.json();
        });
    }

    /**
     * Fetch global list of all .tar.gz save backups across all games
     */
    function fetchAllBackups() {
        return secureFetch('/cloud/backups', {
            method: 'GET'
        }).then(function(res) {
            return res.json();
        });
    }

    /**
     * Delete a specific .tar.gz backup archive for an appId
     */
    function deleteBackupArchive(appId, fileName) {
        return secureFetch('/cloud/backups/delete', {
            method: 'POST',
            body: JSON.stringify({
                appId: Number(appId) || 0,
                fileName: String(fileName || '')
            })
        }).then(function(res) {
            return res.json();
        });
    }

    /**
     * Inject shared Steam Cloud table styles following modern-web-guidance responsive-table pattern
     */
    function ensureSteamCloudTableStyles(targetDoc) {
        var doc = targetDoc || document;
        if (!doc || !doc.head) return;
        var existing = doc.getElementById('navi-steam-cloud-table-styles');
        if (existing) {
            existing.remove();
        }
        var style = doc.createElement('style');
        style.id = 'navi-steam-cloud-table-styles';
        style.textContent = [
            '.navi-cloud-table-region {',
            '  overflow-x: auto;',
            '  max-width: 100%;',
            '  border: 1px solid #2a475e;',
            '  border-radius: 2px;',
            '  background: #101822;',
            '}',
            '.navi-cloud-table-region:focus-visible {',
            '  outline: 2px solid #66c0f4;',
            '  outline-offset: 2px;',
            '}',
            '.navi-steam-cloud-table {',
            '  width: 100%;',
            '  border-collapse: collapse;',
            '  font-family: "Motiva Sans", Arial, Helvetica, sans-serif;',
            '  font-size: 13px;',
            '  color: #c6d4df;',
            '}',
            '.navi-steam-cloud-table thead th {',
            '  position: sticky;',
            '  top: 0;',
            '  z-index: 2;',
            '  background: #2a475e;',
            '  color: #8f98a0;',
            '  font-size: 11px;',
            '  font-weight: 500;',
            '  text-transform: uppercase;',
            '  letter-spacing: 0.04em;',
            '  padding: 7px 12px;',
            '  text-align: left;',
            '  border-bottom: 1px solid #000;',
            '  white-space: nowrap;',
            '}',
            '.navi-steam-cloud-table tbody tr {',
            '  background: #16202d;',
            '  border-bottom: 2px solid #0e141b;',
            '  transition: background 0.12s ease;',
            '}',
            '.navi-steam-cloud-table tbody tr:nth-child(even) {',
            '  background: #192533;',
            '}',
            '.navi-steam-cloud-table tbody tr:hover {',
            '  background: #213245;',
            '}',
            '.navi-steam-cloud-table td {',
            '  padding: 7px 12px;',
            '  vertical-align: middle;',
            '  color: #c6d4df;',
            '}',
            '.navi-steam-cloud-table td.navi-col-num {',
            '  font-variant-numeric: tabular-nums;',
            '  white-space: nowrap;',
            '}',
            '.navi-steam-cloud-link {',
            '  background: transparent;',
            '  border: none;',
            '  color: #ebebeb;',
            '  font-family: inherit;',
            '  font-size: 13px;',
            '  cursor: pointer;',
            '  padding: 2px 6px;',
            '  text-align: left;',
            '  text-decoration: none;',
            '}',
            '.navi-steam-cloud-link:hover, .navi-steam-cloud-link:focus-visible {',
            '  color: #66c0f4;',
            '  text-decoration: underline;',
            '}',
            '@media (max-width: 560px) {',
            '  .navi-steam-cloud-table thead {',
            '    position: absolute;',
            '    width: 1px;',
            '    height: 1px;',
            '    overflow: hidden;',
            '    clip: rect(0 0 0 0);',
            '  }',
            '  .navi-steam-cloud-table tbody tr {',
            '    display: block;',
            '    padding: 8px 10px;',
            '    margin-bottom: 6px;',
            '  }',
            '  .navi-steam-cloud-table td {',
            '    display: flex;',
            '    justify-content: space-between;',
            '    align-items: center;',
            '    gap: 10px;',
            '    padding: 4px 4px;',
            '    text-align: right;',
            '  }',
            '  .navi-steam-cloud-table td::before {',
            '    content: attr(data-label);',
            '    font-weight: 600;',
            '    font-size: 11px;',
            '    color: #8f98a0;',
            '    text-transform: uppercase;',
            '    text-align: left;',
            '  }',
            '}'
        ].join('\n');
        doc.head.appendChild(style);
    }

    /**
     * Open Steam-styled "View Steam Cloud" per-game modal (used from Library Navi Menu or Settings)
     */
    function openCloudFilesModal(appId, gameTitleHint) {
        ensureSteamCloudTableStyles(document);
        var existing = document.getElementById('navi-cloud-files-modal-overlay');
        if (existing) existing.remove();

        var overlay = document.createElement('div');
        overlay.id = 'navi-cloud-files-modal-overlay';
        overlay.style.cssText = 'position:fixed;top:0;left:0;width:100vw;height:100vh;background:rgba(0,0,0,0.82);z-index:999999;display:flex;align-items:center;justify-content:center;font-family:"Motiva Sans",Arial,sans-serif;backdrop-filter:blur(4px);';

        var modal = document.createElement('div');
        modal.style.cssText = 'background:#1b2838;border:1px solid #2a475e;border-top:2px solid #66c0f4;border-radius:3px;width:880px;max-width:94vw;max-height:86vh;padding:22px 26px;color:#c6d4df;box-shadow:0 20px 50px rgba(0,0,0,0.85);display:flex;flex-direction:column;gap:14px;overflow:hidden;';

        var topBar = document.createElement('div');
        topBar.style.cssText = 'display:flex;justify-content:space-between;align-items:flex-start;gap:12px;';

        var titleWrap = document.createElement('div');
        var breadcrumb = document.createElement('div');
        breadcrumb.style.cssText = 'font-size:11px;color:#8f98a0;margin-bottom:4px;letter-spacing:0.02em;';
        breadcrumb.innerText = 'Home > Account > View Steam Cloud';

        var heading = document.createElement('h2');
        heading.style.cssText = 'margin:0;font-size:24px;font-weight:400;color:#ffffff;letter-spacing:0.01em;';
        heading.innerText = 'View Steam Cloud';

        titleWrap.appendChild(breadcrumb);
        titleWrap.appendChild(heading);

        var closeX = document.createElement('button');
        closeX.id = 'navi-close-x';
        closeX.className = 'Focusable';
        closeX.setAttribute('tabindex', '-1');
        closeX.style.cssText = 'background:transparent;border:none;color:#8f98a0;font-size:18px;cursor:pointer;padding:4px 8px;';
        closeX.innerText = '✕';
        closeX.onclick = function() { overlay.remove(); };

        topBar.appendChild(titleWrap);
        topBar.appendChild(closeX);

        var subHeader = document.createElement('div');
        subHeader.style.cssText = 'display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px;padding-bottom:6px;border-bottom:1px solid #2a475e;';

        var gameInfoWrap = document.createElement('div');
        var gameNameEl = document.createElement('div');
        gameNameEl.style.cssText = 'font-size:15px;font-weight:600;color:#ffffff;margin-bottom:4px;';
        var currentTitle = resolveGameTitle(appId, gameTitleHint);
        gameNameEl.innerText = currentTitle;
        if (isPlaceholderTitle(currentTitle, appId)) {
            fetch('https://store.steampowered.com/api/appdetails?appids=' + encodeURIComponent(appId))
                .then(function(r) { return r.json(); })
                .then(function(data) {
                    var sId = String(appId);
                    if (data && data[sId] && data[sId].success && data[sId].data && data[sId].data.name) {
                        var realName = data[sId].data.name;
                        if (!isPlaceholderTitle(realName, sId)) {
                            titleCache[sId] = realName;
                            try { localStorage.setItem('navi_app_titles_cache', JSON.stringify(titleCache)); } catch (e) {}
                            if (gameNameEl) gameNameEl.innerText = realName;
                        }
                    }
                }).catch(function() {});
        }

        var subtitleEl = document.createElement('div');
        subtitleEl.style.cssText = 'font-size:12px;color:#8f98a0;';
        subtitleEl.innerText = 'This game files are saved in your Steam Cloud :';

        gameInfoWrap.appendChild(gameNameEl);
        gameInfoWrap.appendChild(subtitleEl);

        var actionToolbar = document.createElement('div');
        actionToolbar.style.cssText = 'display:flex;align-items:center;gap:8px;flex-wrap:wrap;';

        function makeActionBtn(label, bg, color) {
            var b = document.createElement('button');
            b.className = 'Focusable';
            b.setAttribute('tabindex', '-1');
            b.style.cssText = 'background:' + bg + ';color:' + color + ';border:1px solid rgba(255,255,255,0.12);border-radius:2px;padding:6px 12px;font-size:12px;font-weight:600;cursor:pointer;';
            b.innerText = label;
            return b;
        }

        var syncBtn = makeActionBtn('Sync Now', '#2a475e', '#66c0f4');
        var backupBtn = makeActionBtn('Backup', '#273b4b', '#c6d4df');
        var restoreBtn = makeActionBtn('Restore', '#273b4b', '#c6d4df');
        var resetBtn = makeActionBtn('Reset', '#4c1d1d', '#fca5a5');

        actionToolbar.appendChild(syncBtn);
        actionToolbar.appendChild(backupBtn);
        actionToolbar.appendChild(restoreBtn);
        actionToolbar.appendChild(resetBtn);

        subHeader.appendChild(gameInfoWrap);
        subHeader.appendChild(actionToolbar);

        var statusToast = document.createElement('div');
        statusToast.style.cssText = 'display:none;padding:8px 12px;border-radius:2px;font-size:12px;background:#16202d;border:1px solid #66c0f4;color:#66c0f4;';

        function showStatusMessage(msg, isError) {
            statusToast.style.display = 'block';
            statusToast.style.borderColor = isError ? '#ef4444' : '#66c0f4';
            statusToast.style.color = isError ? '#fca5a5' : '#66c0f4';
            statusToast.innerText = msg;
        }

        var tableRegion = document.createElement('div');
        tableRegion.className = 'navi-cloud-table-region';
        tableRegion.setAttribute('tabindex', '0');
        tableRegion.setAttribute('role', 'region');
        tableRegion.setAttribute('aria-label', 'Steam Cloud Files');
        tableRegion.style.cssText = 'flex:1;overflow-y:auto;min-height:220px;';

        var footerBar = document.createElement('div');
        footerBar.style.cssText = 'display:flex;justify-content:space-between;align-items:center;padding-top:6px;';

        var summaryText = document.createElement('span');
        summaryText.style.cssText = 'font-size:12px;color:#8f98a0;';
        summaryText.innerText = 'Scanning cloud storage...';

        var closeBtn = makeActionBtn('Close', '#32373d', '#c6d4df');
        closeBtn.id = 'navi-modal-cancel';
        closeBtn.onclick = function() { overlay.remove(); };

        footerBar.appendChild(summaryText);
        footerBar.appendChild(closeBtn);

        modal.appendChild(topBar);
        modal.appendChild(subHeader);
        modal.appendChild(statusToast);
        modal.appendChild(tableRegion);
        modal.appendChild(footerBar);
        overlay.appendChild(modal);
        document.body.appendChild(overlay);

        var focusableButtons = [syncBtn, backupBtn, restoreBtn, resetBtn, closeBtn, closeX];

        function loadFiles() {
            tableRegion.innerHTML = '<div style="padding:32px;text-align:center;color:#8f98a0;font-size:13px;">Scanning cloud storage for AppID ' + appId + '...</div>';
            fetchAppCloudFiles(appId).then(function(data) {
                var resolvedName = resolveGameTitle(appId, data && data.gameTitle ? data.gameTitle : gameTitleHint);
                gameNameEl.innerText = resolvedName + ' (' + appId + ')';
                var files = (data && Array.isArray(data.files)) ? data.files : [];
                summaryText.innerText = files.length + (files.length === 1 ? ' file' : ' files') + ' • Total size: ' + formatBytes(data && data.totalSizeBytes ? data.totalSizeBytes : 0);

                if (files.length === 0) {
                    tableRegion.innerHTML = '<div style="padding:36px 20px;text-align:center;color:#8f98a0;font-size:13px;">No cloud save files found for this game yet. Play the game or click <b>Sync Now</b> to upload local saves.</div>';
                    return;
                }

                var table = document.createElement('table');
                table.className = 'navi-steam-cloud-table';

                var thead = document.createElement('thead');
                thead.innerHTML = '<tr>' +
                    '<th scope="col">FOLDER</th>' +
                    '<th scope="col">FILE NAME</th>' +
                    '<th scope="col">FILE SIZE</th>' +
                    '<th scope="col">DATE WRITTEN</th>' +
                    '<th scope="col"></th>' +
                    '</tr>';
                table.appendChild(thead);

                var tbody = document.createElement('tbody');
                files.forEach(function(f) {
                    var tr = document.createElement('tr');

                    var tdFolder = document.createElement('td');
                    tdFolder.setAttribute('data-label', 'FOLDER');
                    tdFolder.innerText = f.folder || 'SteamCloud';

                    var tdName = document.createElement('td');
                    tdName.setAttribute('data-label', 'FILE NAME');
                    tdName.style.wordBreak = 'break-all';
                    tdName.innerText = f.fileName || '';

                    var tdSize = document.createElement('td');
                    tdSize.className = 'navi-col-num';
                    tdSize.setAttribute('data-label', 'FILE SIZE');
                    tdSize.innerText = formatBytes(f.sizeBytes);

                    var tdDate = document.createElement('td');
                    tdDate.className = 'navi-col-num';
                    tdDate.setAttribute('data-label', 'DATE WRITTEN');
                    tdDate.innerText = formatSteamCloudDate(f.timestamp);

                    var tdDl = document.createElement('td');
                    tdDl.setAttribute('data-label', 'ACTION');
                    tdDl.style.textAlign = 'right';

                    var dlBtn = document.createElement('button');
                    dlBtn.className = 'navi-steam-cloud-link Focusable';
                    dlBtn.setAttribute('tabindex', '-1');
                    dlBtn.innerText = 'Download';
                    dlBtn.onclick = function() {
                        dlBtn.innerText = 'Downloading...';
                        dlBtn.disabled = true;
                        downloadAppCloudFile(appId, f.fileName, data.provider).then(function(res) {
                            dlBtn.innerText = 'Download';
                            dlBtn.disabled = false;
                            if (res && res.success) {
                                showStatusMessage('Downloaded to ' + (res.savedPath || '~/Downloads'), false);
                            } else {
                                showStatusMessage((res && res.error) ? res.error : 'Download failed', true);
                            }
                        }).catch(function(err) {
                            dlBtn.innerText = 'Download';
                            dlBtn.disabled = false;
                            showStatusMessage('Download error: ' + err.message, true);
                        });
                    };

                    tdDl.appendChild(dlBtn);
                    tr.appendChild(tdFolder);
                    tr.appendChild(tdName);
                    tr.appendChild(tdSize);
                    tr.appendChild(tdDate);
                    tr.appendChild(tdDl);
                    tbody.appendChild(tr);
                    focusableButtons.push(dlBtn);
                });

                table.appendChild(tbody);
                tableRegion.innerHTML = '';
                tableRegion.appendChild(table);
            }).catch(function(err) {
                tableRegion.innerHTML = '<div style="padding:28px;text-align:center;color:#fca5a5;">Failed to load cloud files: ' + (err && err.message ? err.message : 'Unknown error') + '</div>';
            });
        }

        syncBtn.onclick = function() {
            syncBtn.innerText = 'Syncing...';
            syncBtn.disabled = true;
            syncAppNow(appId).then(function(res) {
                syncBtn.innerText = 'Sync Now';
                syncBtn.disabled = false;
                showStatusMessage((res && res.message) ? res.message : 'Cloud sync complete.', false);
                loadFiles();
            }).catch(function(err) {
                syncBtn.innerText = 'Sync Now';
                syncBtn.disabled = false;
                showStatusMessage('Sync failed: ' + err.message, true);
            });
        };

        backupBtn.onclick = function() {
            backupBtn.innerText = 'Backing up...';
            backupBtn.disabled = true;
            backupAppSave(appId).then(function(res) {
                backupBtn.innerText = 'Backup';
                backupBtn.disabled = false;
                showStatusMessage('Backup saved: ' + ((res && res.backupFile) ? res.backupFile : 'created'), false);
            }).catch(function(err) {
                backupBtn.innerText = 'Backup';
                backupBtn.disabled = false;
                showStatusMessage('Backup failed: ' + err.message, true);
            });
        };

        restoreBtn.onclick = function() {
            openRestoreModal(appId, resolveGameTitle(appId, gameTitleHint));
        };

        resetBtn.onclick = function() {
            resetAppSave(appId, resolveGameTitle(appId, gameTitleHint));
        };

        loadFiles();
        trapSteamGamepadModal(overlay, focusableButtons);
    }

    /**
     * Polling tick
     */
    function poll() {
        return Promise.all([
            fetchStatus().catch(function() {}),
            fetchPendingOps().catch(function() {})
        ]);
    }

    function startPolling(intervalMs) {
        stopPolling();
        poll();
        pollTimer = setInterval(poll, intervalMs || POLL_INTERVAL_MS);
    }

    function stopPolling() {
        if (pollTimer) {
            clearInterval(pollTimer);
            pollTimer = null;
        }
    }

    function init() {
        startPolling(POLL_INTERVAL_MS);
    }

    // Public API
    window.NaviCloudManager = {
        fetchStatus: fetchStatus,
        fetchPendingOps: fetchPendingOps,
        toggleMaster: toggleMaster,
        fetchProviderStatus: fetchProviderStatus,
        fetchProviderCredentials: fetchProviderCredentials,
        saveProviderCredentials: saveProviderCredentials,
        scanMigrationSource: scanMigrationSource,
        migrateProviderData: migrateProviderData,
        fetchOrphanedSaves: fetchOrphanedSaves,
        cleanOrphanedApp: cleanOrphanedApp,
        fetchAllBackups: fetchAllBackups,
        deleteBackupArchive: deleteBackupArchive,
        signInProvider: signInProvider,
        signOutProvider: signOutProvider,
        restartSteam: restartSteam,
        fetchAppStatus: fetchAppStatus,
        fetchCloudApps: fetchCloudApps,
        fetchAppCloudFiles: fetchAppCloudFiles,
        syncAppNow: syncAppNow,
        downloadAppCloudFile: downloadAppCloudFile,
        formatBytes: formatBytes,
        formatSteamCloudDate: formatSteamCloudDate,
        resolveGameTitle: resolveGameTitle,
        ensureSteamCloudTableStyles: ensureSteamCloudTableStyles,
        openCloudFilesModal: openCloudFilesModal,
        toggleAppRedirect: toggleAppRedirect,
        backupAppSave: backupAppSave,
        restoreAppSave: restoreAppSave,
        resetAppSave: resetAppSave,
        executeResetAppSave: executeResetAppSave,
        openResetModal: resetAppSave,
        openRestoreModal: openRestoreModal,
        openSettingsModal: openSettingsModal,
        updatePendingBanner: updatePendingBanner,
        secureFetch: secureFetch,
        startPolling: startPolling,
        stopPolling: stopPolling,
        getState: function() { return Object.assign({}, state); },
        init: init
    };

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
})();
