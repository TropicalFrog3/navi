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

