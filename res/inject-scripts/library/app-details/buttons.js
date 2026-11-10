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

