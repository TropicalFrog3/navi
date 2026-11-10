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

