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
            if (!origin || origin === 'null') {
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
        var headers = Object.assign({}, opts.headers || {});
        headers['X-Navi-Auth'] = window.__naviAuthToken || '';
        if (opts.body && typeof opts.body === 'string' && !headers['Content-Type']) {
            headers['Content-Type'] = 'application/json';
        }
        opts.headers = headers;

        if (typeof window.naviFetch === 'function' && window.location.hostname.indexOf('steampowered.com') !== -1) {
            return window.naviFetch(targetUrl, opts);
        }

        return fetch(targetUrl, opts);
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
            '<button id="sls-reset-close" class="sls-btn-close Focusable" tabindex="-1" role="button" style="background:transparent;border:none;color:#969696;font-size:24px;cursor:pointer;">&times;</button>' +
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
            '<button id="sls-restore-close" class="sls-btn-close Focusable" tabindex="-1" role="button" style="background:transparent;border:none;color:#969696;font-size:24px;cursor:pointer;">&times;</button>' +
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
            '<button id="sls-settings-close" class="sls-btn-close Focusable" tabindex="-1" role="button" style="background:transparent;border:none;color:#969696;font-size:24px;cursor:pointer;">&times;</button>' +
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
                '<span id="sls-auth-badge" style="padding:3px 8px;border-radius:2px;font-size:12px;font-weight:600;">Checking...</span>' +
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
        signInProvider: signInProvider,
        signOutProvider: signOutProvider,
        restartSteam: restartSteam,
        fetchAppStatus: fetchAppStatus,
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
