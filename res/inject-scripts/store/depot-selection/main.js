/**
 * @file  main.js
 * @brief Modal lifecycle, depot list fetcher, checkbox state coordinator, and download trigger
 */
// Prevent double injection if somehow triggered twice rapidly
    if (document.getElementById('sls-depot-overlay-modal')) return;

    // Never inject into supernav dropdowns, context menus, or headless contexts
    if (document.title.indexOf('Supernav') !== -1 ||
        (document.title.indexOf('Menu') !== -1 && document.title.indexOf('MainMenu_uid') === -1) ||
        document.title === 'SharedJSContext') return;

    var appid = "%APPID%";
var overlay = document.createElement('div');
overlay.id = 'sls-depot-overlay-modal';
overlay.setAttribute('role', 'dialog');
overlay.setAttribute('aria-modal', 'true');
overlay.setAttribute('data-testid', 'sls-depot-overlay-modal');
overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(10,12,18,0.85);z-index:2147483647;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);transition:all 0.3s ease;opacity:1;';

overlay.innerHTML = DEPOT_MODAL_HTML.replace(/\{\{APPID\}\}/g, appid);
ensureDepotStyles(overlay);
document.body.appendChild(overlay);

setTimeout(function() {
    overlay.style.opacity = '1';
    var card = document.getElementById('sls-depot-modal-card');
    if (card) {
        card.style.transform = 'scale(1)';
        card.style.opacity = '1';
    }
}, 10);

var closeX = document.getElementById('sls-depot-close-x');
var cancelBtn = document.getElementById('sls-depot-btn-cancel');
var downloadBtn = document.getElementById('sls-depot-btn-download');
var contentDiv = document.getElementById('sls-depot-content');

// Focus cancelBtn immediately so controller and keyboard navigation work during loading
setupGamepadTrap([[closeX], [cancelBtn]], 1, 0);

function closeAndCancel() {
    if (activeGamepadCleanup) activeGamepadCleanup();
    overlay.style.pointerEvents = 'none'; // Prevent double clicks
    fetch('http://127.0.0.1:9001/cancel-download?id=' + appid).catch(function(){});
    
    overlay.style.opacity = '0';
    var card = document.getElementById('sls-depot-modal-card');
    if (card) {
        card.style.transform = 'scale(0.95)';
        card.style.opacity = '0';
    }
    setTimeout(function() { overlay.remove(); }, 300);
}
    
    function closeSuccess() {
        if (activeGamepadCleanup) activeGamepadCleanup();
        overlay.style.pointerEvents = 'none'; // Prevent double clicks
        overlay.style.opacity = '0';
        var card = document.getElementById('sls-depot-modal-card');
        if (card) {
            card.style.transform = 'scale(0.95)';
            card.style.opacity = '0';
        }
        setTimeout(function() { overlay.remove(); }, 300);
    }

    closeX.onclick = closeAndCancel;
    cancelBtn.onclick = closeAndCancel;

    // Fetch depots
    fetch('http://127.0.0.1:9001/get-depots?id=' + appid)
        .then(function(res) { return res.json(); })
        .then(function(data) {
            if (!data.success) {
                contentDiv.innerHTML = '<div style="display:flex; justify-content:center; align-items:center; height: 100px; color:#ef4444; font-size: 14px;">Error: ' + data.message + '</div>';
                setupGamepadTrap([[closeX], [cancelBtn]], 1, 0);
                return;
            }

            if (!data.depots || data.depots.length === 0) {
                contentDiv.innerHTML = '<div style="display:flex; justify-content:center; align-items:center; height: 100px; color:#f59e0b; font-size: 14px;">No depots found in Lua plugin.</div>';
                setupGamepadTrap([[closeX], [cancelBtn]], 1, 0);
                return;
            }

            var hasLinuxDepot = false;
            for (var i = 0; i < data.depots.length; i++) {
                var dOs = (data.depots[i].os || '').toLowerCase();
                if (dOs.indexOf('linux') !== -1) {
                    hasLinuxDepot = true;
                    break;
                }
            }

            var html = '';
            for (var i = 0; i < data.depots.length; i++) {
                var depot = data.depots[i];
                var sizeStr = depot.size ? formatBytes(parseInt(depot.size, 10)) : 'Unknown size';
                
                var displayName = depot.name;
                if (!displayName || displayName.indexOf('Depot ') === 0) {
                    var depotIdNum = parseInt(depot.id, 10);
                    var appIdNum = parseInt(appid, 10);
                    // Depots within appId to appId+10 without a specific name are base game content
                    // (covers multi-OS depots like appId+1=win, +2=mac, +3=linux)
                    if (depotIdNum >= appIdNum && depotIdNum <= appIdNum + 10) {
                        displayName = 'Base Game Content';
                    } else {
                        displayName = 'Additional Content (' + depot.id + ')';
                    }
                }

                var osBadge = '';
                if (depot.os) {
                    osBadge = buildOsBadge(depot.os);
                }

                var hasKey = (depot.hasKey !== false);
                var keyBadge = hasKey ? '' : ' <span class="sls-os-badge" style="background:rgba(239,68,68,0.15);color:#ef4444;border:1px solid rgba(239,68,68,0.25);">No Key</span>';

                var dOs = (depot.os || '').toLowerCase();
                var shouldCheck = false;
                if (hasKey) {
                    if (hasLinuxDepot) {
                        shouldCheck = (!dOs || dOs.indexOf('linux') !== -1);
                    } else {
                        shouldCheck = (!dOs || dOs.indexOf('windows') !== -1);
                    }
                }
                
                html += '<label class="sls-depot-item"' + (!hasKey ? ' style="opacity:0.5;cursor:not-allowed;" title="Cannot download: No AES decryption key available"' : '') + '>' +
                    '<input type="checkbox" class="sls-depot-checkbox" value="' + depot.id + '"' + (!hasKey ? ' disabled' : '') + (depot.os ? ' data-os="' + depot.os + '"' : '') + (shouldCheck ? ' checked' : '') + '>' +
                    '<div class="sls-depot-info">' +
                        '<div class="sls-depot-name">' + displayName + osBadge + keyBadge + '</div>' +
                        '<div class="sls-depot-meta">' +
                            '<span>ID: ' + depot.id + '</span>' +
                            '<span>' + sizeStr + '</span>' +
                        '</div>' +
                    '</div>' +
                '</label>';
            }

            // Check if extra metadata needs to be resolved from SteamCMD
            var tempDiv = document.createElement('div');
            tempDiv.innerHTML = html;
            var tempItems = tempDiv.querySelectorAll('.sls-depot-item');
            var needsSteamCmdFetch = false;
            for (var l = 0; l < tempItems.length; l++) {
                var el = tempItems[l];
                var nameEl = el.querySelector('.sls-depot-name');
                var sizeSpan = el.querySelector('.sls-depot-meta').querySelectorAll('span')[1];
                var cb = el.querySelector('.sls-depot-checkbox');
                var hasOs = cb.getAttribute('data-os');
                if (nameEl.innerText.indexOf('Additional Content') === 0 || sizeSpan.innerText === 'Unknown size' || !hasOs) {
                    needsSteamCmdFetch = true;
                    break;
                }
            }

            var resolvingHeader = needsSteamCmdFetch ? '<div id="sls-resolving-pill" class="sls-resolving-pill"><div class="sls-mini-spinner"></div><span>Resolving DLC details…</span></div>' : '';
            contentDiv.innerHTML = resolvingHeader + html;

            var depotItems = contentDiv.querySelectorAll('.sls-depot-item');
if (needsSteamCmdFetch) {
    resolveSteamCmdMetadata(appid, depotItems, updateButtonState, function() {
        return userInteractedWithCheckboxes;
    });
}

var userInteractedWithCheckboxes = false;
            var checkboxes = contentDiv.querySelectorAll('.sls-depot-checkbox');

            function updateButtonState() {
                var anyChecked = false;
                for (var k = 0; k < checkboxes.length; k++) {
                    if (checkboxes[k].checked) {
                        anyChecked = true;
                        break;
                    }
                }
                if (anyChecked) {
                    downloadBtn.style.opacity = '1';
                    downloadBtn.style.pointerEvents = 'auto';
                } else {
                    downloadBtn.style.opacity = '0.5';
                    downloadBtn.style.pointerEvents = 'none';
                }
            }

            // Add event listeners to checkboxes to enable/disable the install button
            for (var j = 0; j < checkboxes.length; j++) {
                checkboxes[j].addEventListener('change', function() {
                    userInteractedWithCheckboxes = true;
                    updateButtonState();
                });
            }
            // Set initial button state based on pre-checked boxes
            updateButtonState();

            // Controller support: collect all interactive elements
            var trapButtons = [
                [closeX]
            ];
            for (var t = 0; t < checkboxes.length; t++) {
                // Use the parent label so the outline wraps the whole row
                var label = checkboxes[t].closest ? checkboxes[t].closest('.sls-depot-item') : checkboxes[t].parentElement;
                trapButtons.push([label || checkboxes[t]]);
            }
            trapButtons.push([cancelBtn, downloadBtn]);
            setupGamepadTrap(trapButtons, 1, 0); // Focus first depot row!
            
            downloadBtn.onclick = function() {
                var selected = [];
                for (var k = 0; k < checkboxes.length; k++) {
                    if (checkboxes[k].checked) {
                        selected.push(checkboxes[k].value);
                    }
                }
                
                if (selected.length === 0) return;
                
                downloadBtn.innerText = 'Starting...';
                downloadBtn.style.opacity = '0.5';
                downloadBtn.style.pointerEvents = 'none';
                cancelBtn.style.pointerEvents = 'none';
                closeX.style.pointerEvents = 'none';
                
                var depotsStr = encodeURIComponent(selected.join(','));
                fetch('http://127.0.0.1:9001/start-download?id=' + appid + '&depots=' + depotsStr)
                    .then(function(res) {
                        if (!res.ok) throw new Error('HTTP ' + res.status);
                        return res.json();
                    })
                    .then(function(data) {
                        if (!data || !data.success) {
                            throw new Error((data && data.message) ? data.message : 'Install failed');
                        }
                        closeSuccess();
                    })
                    .catch(function(err) {
                        downloadBtn.innerText = 'Failed';
                        setTimeout(function() {
                            downloadBtn.innerText = 'Install';
                            updateButtonState();
                            cancelBtn.style.pointerEvents = 'auto';
                            closeX.style.pointerEvents = 'auto';
                        }, 2000);
                    });
            };
        })
        .catch(function(err) {
            contentDiv.innerHTML = '<div style="display:flex; justify-content:center; align-items:center; height: 100px; color:#ef4444; font-size: 14px;">Failed to fetch depot list.</div>';
            setupGamepadTrap([[closeX], [cancelBtn]], 1, 0);
        });
