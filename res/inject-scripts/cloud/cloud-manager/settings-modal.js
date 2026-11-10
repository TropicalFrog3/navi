/**
 * @file  settings-modal.js
 * @brief Global cloud save settings and provider authentication modal controller
 */
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

    overlay.innerHTML = SETTINGS_MODAL_HTML.replace('{{PROVIDER_OPTIONS}}', optHtml);
    document.body.appendChild(overlay);

    var closeBtn = overlay.querySelector('#sls-settings-close');
    var cancelBtn = overlay.querySelector('#sls-modal-cancel');
    var masterBtn = overlay.querySelector('#sls-master-toggle-btn');
    var selectEl = overlay.querySelector('#sls-provider-select');
    var badgeEl = overlay.querySelector('#sls-auth-badge');
    var emailEl = overlay.querySelector('#sls-auth-email');
    var actionsEl = overlay.querySelector('#sls-auth-actions');

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

