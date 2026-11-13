/**
 * @file  config-controller.js
 * @brief Navi configuration page controller, settings editor, saves table, and save pipeline
 */
function hideConfigPage() {
        if (configKeyDownHandler) {
            window.removeEventListener('keydown', configKeyDownHandler);
            configKeyDownHandler = null;
        }
        var container = document.getElementById('sls-config-page-container');
        if (container) {
            container.style.display = 'none';
        }
        var configTab = document.getElementById('sls-config-top-tab');
        if (configTab) {
            configTab.classList.remove(STEAM_TAB_ACTIVE_CLASS);
        }
    }

    function showConfigPage() {
        hideSlsPage();
        ensureCustomTabStyles();

        var container = document.getElementById('sls-config-page-container');
        if (!container) {
            container = document.createElement('div');
            container.id = 'sls-config-page-container';
            container.style.cssText = 'position: fixed; left: 0; right: 0; bottom: 0; z-index: 999999; background: rgb(23, 29, 37); display: none; font-family: "Motiva Sans", Arial, Helvetica, sans-serif; color: rgb(150, 150, 150); padding: 0; box-sizing: border-box; overflow: hidden;';
            document.body.appendChild(container);
        } else {
            container.style.cssText = 'position: fixed; left: 0; right: 0; bottom: 0; z-index: 999999; background: rgb(23, 29, 37); display: none; font-family: "Motiva Sans", Arial, Helvetica, sans-serif; color: rgb(150, 150, 150); padding: 0; box-sizing: border-box; overflow: hidden;';
        }

        container.style.top = getTopBarHeight() + 'px';
        container.style.display = 'block';

        var configTab = document.getElementById('sls-config-top-tab');
        if (configTab) {
            setActiveTabStyle(configTab);
        }

        container.innerHTML = '<div style="display: flex; width: 100%; height: 100%; align-items: center; justify-content: center; flex-direction: column;">' +
            '<div style="font-size: 18px; font-weight: 700; color: rgb(26, 159, 255); margin-bottom: 8px;">Loading Navi Configuration...</div>' +
            '<div style="font-size: 13px; color: rgb(139, 146, 154);">Connecting to Navi backend (http://127.0.0.1:9001)...</div>' +
            '</div>';

        fetch('http://127.0.0.1:9001/config/full', { headers: { 'Connection': 'close' } })
            .then(function (r) { return r.json(); })
            .then(function (cfgData) {
                renderConfigPage(container, cfgData, {});
            }).catch(function (err) {
            container.innerHTML = '<div style="display: flex; width: 100%; height: 100%; align-items: center; justify-content: center; flex-direction: column;">' +
                '<h2 style="color: #ff4d4d; margin-bottom: 12px;">Failed to Load Configuration</h2>' +
                '<p style="color: rgb(139, 146, 154); font-size: 14px; margin-bottom: 24px;">' + escapeHtml(err.message) + '</p>' +
                '<button id="sls-cfg-retry-btn" class="sls-cfg-btn" style="padding: 10px 24px;">Retry</button>' +
                '</div>';
            var retryBtn = document.getElementById('sls-cfg-retry-btn');
            if (retryBtn) retryBtn.onclick = showConfigPage;
        });
    }
    window.__naviShowConfigPage = showConfigPage;

        function renderConfigPage(container, cfg, sync) {
        if (configKeyDownHandler) {
            window.removeEventListener('keydown', configKeyDownHandler);
            configKeyDownHandler = null;
        }
        cfg = JSON.parse(JSON.stringify(cfg || {}));
        sync = sync || {};

        container.style.padding = '0';
        container.style.overflow = 'hidden';
        container.style.background = 'rgb(23, 29, 37)';

        var currentLogLevel = typeof cfg.LogLevels === 'number' ? cfg.LogLevels : 255;
        var sections = buildConfigSectionsHtml(cfg);
        container.innerHTML = '<div class="sls-cfg-shell" style="display: flex; flex-direction: row; width: 100%; height: 100%; overflow: hidden;">' +
            sections.sidebarHtml +
            sections.dialogContentHtml +
        '</div>';

// ---------------------------------------------------------------------
        // Category pill switching + search
        // ---------------------------------------------------------------------
        var pills = container.querySelectorAll('.sls-cfg-pill');
        var dialogBody = container.querySelector('.sls-cfg-dialog-body');
        var activeTabId = 'sls-cfg-sec-cloudsaves';

        var TAB_METAS = {
            'sls-cfg-sec-cloudsaves': {
                title: 'Cloud saves',
                desc: 'Keeps saves for unlocked and family-shared games outside Steam Cloud. Choose this computer, or connect an account to sync between devices.'
            },
            'sls-cfg-sec-api': {
                title: 'Provider keys',
                desc: 'Some providers ask for a key before they\'ll send game files. Keys stay in config.json on this computer and are only sent to the provider that asked. Keys are never shown in logs.'
            },
            'sls-cfg-sec-general': {
                title: 'Unlocking',
                desc: 'Change how Navi unlocks games, redirects saves and adds features to Steam. You\'ll see it when a change needs a restart.'
            },
            'sls-cfg-sec-profile': {
                title: 'Account',
                desc: 'These changes are only visible in your Steam client. They are not sent to Steam.'
            },
            'sls-cfg-sec-overrides': {
                title: 'Library',
                desc: 'Games, DLC and download files Navi manages. Lists take one entry per line.'
            },
            'sls-cfg-sec-logging': {
                title: 'Diagnostics',
                desc: 'Logs help you and the Navi team find problems. Leave these on their defaults unless you\'re troubleshooting.'
            },
            'sls-cfg-sec-danger': {
                title: 'Danger zone',
                desc: 'These actions change or delete your Navi settings. You can\'t undo them.'
            }
        };

        function switchTab(targetId) {
            activeTabId = targetId;
            pills.forEach(function (p) {
                var isTarget = p.getAttribute('data-target') === targetId;
                if (isTarget) {
                    p.classList.add('active');
                    p.setAttribute('aria-selected', 'true');
                } else {
                    p.classList.remove('active');
                    p.setAttribute('aria-selected', 'false');
                }
            });
            var meta = TAB_METAS[targetId];
            if (meta) {
                var mainTitle = document.getElementById('sls-cfg-main-title');
                var mainDesc = document.getElementById('sls-cfg-main-desc');
                if (mainTitle) mainTitle.textContent = meta.title;
                if (mainDesc) mainDesc.textContent = meta.desc;
            }
            var searchBox = document.getElementById('sls-cfg-search');
            if (searchBox && searchBox.value) {
                searchBox.value = '';
                applySearch();
            } else {
                var sections = container.querySelectorAll('.sls-cfg-section');
                sections.forEach(function (sec) {
                    sec.style.display = (sec.id === targetId) ? 'block' : 'none';
                });
            }
            if (dialogBody) {
                dialogBody.scrollTop = 0;
            }
        }

        pills.forEach(function (pill) {
            function activateTab() {
                switchTab(pill.getAttribute('data-target'));
            }
            pill.onclick = activateTab;
            pill.onkeydown = function (e) {
                if (e.key === 'Enter' || e.key === ' ') {
                    e.preventDefault();
                    activateTab();
                }
            };
        });

        var searchInput = document.getElementById('sls-cfg-search');
        var searchClearBtn = document.getElementById('sls-cfg-search-clear');
        var noResultsEl = document.getElementById('sls-cfg-no-results');

        function visibleItemCount(sec) {
            var items = sec.querySelectorAll('.sls-cfg-item');
            var n = 0;
            for (var i = 0; i < items.length; i++) {
                if (items[i].style.display !== 'none') n++;
            }
            return n;
        }

        function applySearch() {
            if (!searchInput) return;
            var raw = searchInput.value;
            var q = raw.trim().toLowerCase();
            if (searchClearBtn) searchClearBtn.style.display = raw ? 'block' : 'none';

            var items = container.querySelectorAll('.sls-cfg-item');
            items.forEach(function (el) {
                if (!q) {
                    el.style.display = '';
                    return;
                }
                var text = (el.getAttribute('data-search') || el.textContent || '').toLowerCase();
                el.style.display = (text.indexOf(q) !== -1) ? '' : 'none';
            });

            var sections = container.querySelectorAll('.sls-cfg-section');
            var anyVisible = false;
            if (!q) {
                sections.forEach(function (sec) {
                    sec.style.display = (sec.id === activeTabId) ? 'block' : 'none';
                });
                anyVisible = true;
            } else {
                sections.forEach(function (sec) {
                    var show = visibleItemCount(sec) > 0;
                    sec.style.display = show ? 'block' : 'none';
                    if (show) anyVisible = true;
                });
            }

            if (noResultsEl) {
                if (q && !anyVisible) {
                    noResultsEl.textContent = 'No settings match "' + raw.trim() + '". Try "backup", "saves" or "reset".';
                    noResultsEl.style.display = 'block';
                } else {
                    noResultsEl.style.display = 'none';
                }
            }
        }

        if (searchInput) {
            searchInput.oninput = applySearch;
        }
        if (searchClearBtn && searchInput) {
            searchClearBtn.onclick = function (e) {
                e.preventDefault();
                searchInput.value = '';
                applySearch();
                searchInput.focus();
            };
        }
        // 3. Cloud Saves (CloudRedirect) event handlers and UI logic
        var csState = { enabled: (cfg.CloudSaves && typeof cfg.CloudSaves.Enabled === 'boolean') ? cfg.CloudSaves.Enabled : true, managedAppsCount: 0, pendingOpsCount: 0 };

        function refreshCloudSavesUI() {
            var masterBtn = document.getElementById('sls-cfg-cs-master-btn');
            var engineBadge = document.getElementById('sls-cfg-cs-engine-badge');
            var managedBadge = document.getElementById('sls-cfg-cs-managed-badge');
            var pendingBadge = document.getElementById('sls-cfg-cs-pending-badge');
            var pendingCallout = document.getElementById('sls-cfg-cs-pending-callout');
            var authBadge = document.getElementById('sls-cfg-cs-auth-badge');
            var authEmail = document.getElementById('sls-cfg-cs-auth-email');
            var authActions = document.getElementById('sls-cfg-cs-auth-actions');
            var localInfo = document.getElementById('sls-cfg-cs-local-info');
            var providerSel = document.getElementById('sls-cfg-cs-provider');
            var appsListContainer = document.getElementById('sls-cfg-managed-apps-list');

            function applyStatus(s) {
                csState.enabled = !!s.enabled;
                if (!cfg.CloudSaves) cfg.CloudSaves = {};
                cfg.CloudSaves.Enabled = csState.enabled;
                csState.managedAppsCount = s.managedAppsCount || 0;
                csState.pendingOpsCount = s.pendingOpsCount || 0;

                if (masterBtn) {
                    masterBtn.textContent = csState.enabled ? 'Turn off' : 'Turn on';
                    masterBtn.style.background = csState.enabled ? '#1a9fff' : '#4b5563';
                    masterBtn.style.color = '#ffffff';
                }
                var masterToggle = document.getElementById('sls-cfg-cs-master-toggle');
                if (masterToggle) masterToggle.checked = csState.enabled;
                var masterStatus = document.getElementById('sls-cfg-cs-master-status');
                if (masterStatus) masterStatus.textContent = csState.enabled ? ('On · ' + csState.managedAppsCount + ' games') : 'Off';
                if (engineBadge) {
                    if (csState.enabled) {
                        engineBadge.textContent = 'Running';
                        engineBadge.style.background = 'rgba(16, 185, 129, 0.15)';
                        engineBadge.style.color = '#10b981';
                        engineBadge.style.border = '1px solid rgba(16, 185, 129, 0.3)';
                    } else {
                        engineBadge.textContent = 'Not running';
                        engineBadge.style.background = 'rgba(172, 178, 201, 0.1)';
                        engineBadge.style.color = '#9ca3af';
                        engineBadge.style.border = '1px solid rgba(172, 178, 201, 0.2)';
                    }
                }
                if (managedBadge) {
                    managedBadge.textContent = csState.managedAppsCount + (csState.managedAppsCount === 1 ? ' game redirected' : ' games redirected');
                }
                var dangerRedirectDesc = document.getElementById('sls-cfg-danger-redirect-desc');
                if (dangerRedirectDesc) {
                    dangerRedirectDesc.textContent = csState.managedAppsCount > 0
                        ? 'Stops redirecting saves for ' + csState.managedAppsCount + (csState.managedAppsCount === 1 ? ' game.' : ' games.') + ' Files already redirected stay where they are.'
                        : 'No games have redirected saves right now.';
                }
                var removeRedirectsBtnEl = document.getElementById('sls-cfg-remove-redirects-btn');
                if (removeRedirectsBtnEl) {
                    removeRedirectsBtnEl.disabled = csState.managedAppsCount === 0;
                }
                if (csState.pendingOpsCount > 0) {
                    if (pendingCallout) pendingCallout.style.display = 'flex';
                    if (pendingBadge) {
                        pendingBadge.style.display = 'inline-block';
                        pendingBadge.textContent = csState.pendingOpsCount + (csState.pendingOpsCount === 1 ? ' change waits for a restart' : ' changes wait for a restart');
                        pendingBadge.style.background = 'rgba(245, 158, 11, 0.15)';
                        pendingBadge.style.color = '#f59e0b';
                        pendingBadge.style.border = '1px solid rgba(245, 158, 11, 0.3)';
                    }
                } else {
                    if (pendingCallout) pendingCallout.style.display = 'none';
                    if (pendingBadge) pendingBadge.style.display = 'none';
                }
            }

            if (window.NaviCloudManager && window.NaviCloudManager.fetchStatus) {
                window.NaviCloudManager.fetchStatus().then(function (s) {
                    applyStatus(s);
                }).catch(function (err) {
                    console.warn('[CloudSaves] fetchStatus error:', err);
                    applyStatus({
                        enabled: (cfg.CloudSaves && typeof cfg.CloudSaves.Enabled === 'boolean') ? cfg.CloudSaves.Enabled : true,
                        managedAppsCount: (cfg.CloudSaves && cfg.CloudSaves.ManagedApps && cfg.CloudSaves.ManagedApps.length) || 0,
                        pendingOpsCount: 0
                    });
                });
            } else {
                applyStatus({
                    enabled: (cfg.CloudSaves && typeof cfg.CloudSaves.Enabled === 'boolean') ? cfg.CloudSaves.Enabled : true,
                    managedAppsCount: (cfg.CloudSaves && cfg.CloudSaves.ManagedApps && cfg.CloudSaves.ManagedApps.length) || 0,
                    pendingOpsCount: 0
                });
            }

            var provider = providerSel ? providerSel.value : ((cfg.CloudSaves && cfg.CloudSaves.Provider) || 'local-only');
            var dangerCloudStatus = document.getElementById('sls-cfg-danger-cloud-status');
            if (dangerCloudStatus) {
                dangerCloudStatus.textContent = 'Signed in to ' + providerLabel(provider) + '. Manage this in Cloud saves.';
            }

            var credsFormEl = document.getElementById('sls-cfg-cs-creds-form');
            var credAccountIdWrap = document.getElementById('sls-cfg-cs-cred-account-id-wrap');
            var credAccountIdEl = document.getElementById('sls-cfg-cs-cred-account-id');
            var credBucketEl = document.getElementById('sls-cfg-cs-cred-bucket');
            var credAccessKeyEl = document.getElementById('sls-cfg-cs-cred-access-key');
            var credSecretKeyEl = document.getElementById('sls-cfg-cs-cred-secret-key');
            var credRegionEl = document.getElementById('sls-cfg-cs-cred-region');
            var credEndpointEl = document.getElementById('sls-cfg-cs-cred-endpoint');
            var credPrefixEl = document.getElementById('sls-cfg-cs-cred-prefix');

            if (credsFormEl) {
                var showCredsForm = (provider === 'r2' || provider === 's3');
                credsFormEl.style.display = showCredsForm ? 'flex' : 'none';
                if (credAccountIdWrap) {
                    credAccountIdWrap.style.display = (provider === 'r2') ? 'flex' : 'none';
                }
                if (credAccountIdEl) {
                    credAccountIdEl.style.display = (provider === 'r2') ? '' : 'none';
                }
                if (credRegionEl) {
                    credRegionEl.placeholder = (provider === 'r2') ? 'auto' : 'us-east-1';
                }
                if (showCredsForm) {
                    var credsPromise = (window.NaviCloudManager && window.NaviCloudManager.fetchProviderCredentials)
                        ? window.NaviCloudManager.fetchProviderCredentials(provider)
                        : fetch('http://127.0.0.1:9001/cloud/provider/credentials?provider=' + encodeURIComponent(provider), { headers: { 'Connection': 'close' } }).then(function (r) { return r.json(); });
                    credsPromise.then(function (cData) {
                        if (!cData) return;
                        if (credAccountIdEl) credAccountIdEl.value = cData.accountId || '';
                        if (credBucketEl) credBucketEl.value = cData.bucket || '';
                        if (credAccessKeyEl) credAccessKeyEl.value = cData.accessKeyId || '';
                        if (credRegionEl) credRegionEl.value = cData.region || '';
                        if (credEndpointEl) credEndpointEl.value = cData.endpoint || '';
                        if (credPrefixEl) credPrefixEl.value = cData.keyPrefix || '';
                        if (credSecretKeyEl) {
                            credSecretKeyEl.value = '';
                            credSecretKeyEl.placeholder = cData.hasSecretKey
                                ? '•••••••• (Saved — leave blank to keep)'
                                : 'Secret Access Key';
                            credSecretKeyEl.setAttribute('data-has-secret', cData.hasSecretKey ? 'true' : 'false');
                        }
                    }).catch(function (err) {
                        console.warn('[CloudSaves] fetchProviderCredentials error:', err);
                    });
                }
            }

            function renderProviderAuthButtons(prov, isAuthed) {
                if (!authActions) return;
                var isLocal = (prov === 'local-only');
                var isCredProv = (prov === 'r2' || prov === 's3');
                var loginStyle = isLocal
                    ? 'background: rgba(255, 255, 255, 0.04); border: 1px solid rgba(255, 255, 255, 0.08); color: rgba(255, 255, 255, 0.4); padding: 6px 14px; font-size: 12px; font-weight: 500; cursor: default; border-radius: 6px;'
                    : 'background: #1c202a; border: 1px solid rgba(255, 255, 255, 0.15); color: #ffffff; padding: 6px 14px; font-size: 12px; font-weight: 500; cursor: pointer; border-radius: 6px;';
                var signOutStyle = isLocal
                    ? 'background: rgba(239, 68, 68, 0.05); border: 1px solid rgba(239, 68, 68, 0.15); color: rgba(248, 113, 113, 0.4); padding: 6px 14px; font-size: 12px; font-weight: 500; cursor: default; border-radius: 6px;'
                    : 'background: rgba(239, 68, 68, 0.1); border: 1px solid rgba(239, 68, 68, 0.35); color: #f87171; padding: 6px 14px; font-size: 12px; font-weight: 500; cursor: pointer; border-radius: 6px;';
                if (isCredProv) {
                    authActions.innerHTML =
                        '<button id="sls-cfg-cs-disconnect-btn" type="button" class="sls-cfg-btn" style="' + signOutStyle + '">Clear Credentials</button>';
                } else {
                    authActions.innerHTML =
                        '<button id="sls-cfg-cs-connect-btn" type="button" class="sls-cfg-btn" style="' + loginStyle + '">' + (isAuthed ? 'Reconnect' : 'Connect') + '</button>' +
                        '<button id="sls-cfg-cs-disconnect-btn" type="button" class="sls-cfg-btn" style="' + signOutStyle + '">Disconnect</button>';
                }

                // Connect button handler
                var cBtn = document.getElementById('sls-cfg-cs-connect-btn');
                if (cBtn) {
                    cBtn.onclick = function (e) {
                        e.preventDefault();
                        var curProv = providerSel ? providerSel.value : prov;
                        if (curProv === 'local-only') {
                            showStatusBanner('Please select a remote Cloud Storage Provider (e.g. Google Drive) from the dropdown first.', '#f59e0b');
                            return;
                        }
                        cBtn.textContent = 'Logging in...';
                        var signIn = (window.NaviCloudManager && window.NaviCloudManager.signInProvider)
                            ? window.NaviCloudManager.signInProvider(curProv)
                            : fetch('http://127.0.0.1:9001/cloud/provider/signin', {
                                method: 'POST',
                                headers: { 'Content-Type': 'text/plain' },
                                body: 'X-Navi-Auth: ' + (window.__naviAuthToken || '') + '\r\n' + JSON.stringify({ provider: curProv })
                            }).then(function (r) { return r.json(); }).then(function (d) {
                                if (d && d.authUrl) window.open(d.authUrl, '_blank');
                                return d;
                            });
                        signIn.then(function (res) {
                            if (res && res.error) {
                                showStatusBanner('Provider login failed: ' + res.error, '#ff4d4d');
                            } else {
                                showStatusBanner('Initiated login for ' + curProv + '. Complete authentication in your browser.', '#2ecc71');
                            }
                            refreshCloudSavesUI();
                        }).catch(function (err) {
                            console.warn('[CloudSaves] Sign in error:', err);
                            showStatusBanner('Provider login error: ' + (err.message || 'Failed to connect'), '#ff4d4d');
                            refreshCloudSavesUI();
                        });
                    };
                }

                // Disconnect / Clear Credentials button handler
                var dBtn = document.getElementById('sls-cfg-cs-disconnect-btn');
                if (dBtn) {
                    dBtn.onclick = function (e) {
                        e.preventDefault();
                        var curProv = providerSel ? providerSel.value : prov;
                        if (curProv === 'local-only') {
                            showStatusBanner('Local offline storage does not use an authenticated cloud session.', '#f59e0b');
                            return;
                        }
                        dBtn.textContent = (curProv === 'r2' || curProv === 's3') ? 'Clearing...' : 'Signing out...';
                        var signOut = (window.NaviCloudManager && window.NaviCloudManager.signOutProvider)
                            ? window.NaviCloudManager.signOutProvider(curProv)
                            : fetch('http://127.0.0.1:9001/cloud/provider/signout', {
                                method: 'POST',
                                headers: { 'Content-Type': 'text/plain' },
                                body: 'X-Navi-Auth: ' + (window.__naviAuthToken || '') + '\r\n' + JSON.stringify({ provider: curProv })
                            }).then(function (r) { return r.json(); });
                        signOut.then(function (res) {
                            if (res && res.error) {
                                showStatusBanner('Sign out failed: ' + res.error, '#ff4d4d');
                            } else {
                                showStatusBanner((curProv === 'r2' || curProv === 's3') ? ('Cleared credentials for ' + providerLabel(curProv) + '.') : ('Signed out from ' + curProv + '.'), '#66c0f4');
                            }
                            refreshCloudSavesUI();
                        }).catch(function (err) {
                            console.warn('[CloudSaves] Sign out error:', err);
                            showStatusBanner('Sign out error: ' + (err.message || 'Failed to sign out'), '#ff4d4d');
                            refreshCloudSavesUI();
                        });
                    };
                }
            }

            if (provider === 'local-only') {
                if (authBadge) {
                    authBadge.textContent = 'Offline Storage';
                    authBadge.style.background = 'rgba(255, 255, 255, 0.06)';
                    authBadge.style.color = 'rgb(139, 146, 154)';
                    authBadge.style.border = '1px solid rgba(255, 255, 255, 0.12)';
                }
                if (authEmail) {
                    authEmail.textContent = '';
                    authEmail.style.display = 'none';
                }
                if (authActions) authActions.innerHTML = '';
                renderProviderAuthButtons(provider, false);
                if (localInfo) localInfo.style.display = 'block';
            } else {
                if (localInfo) localInfo.style.display = 'none';
                                // Unified provider status handling
                var statusPromise;
                if (window.NaviCloudManager && window.NaviCloudManager.fetchProviderStatus) {
                    statusPromise = window.NaviCloudManager.fetchProviderStatus(provider);
                } else {
                    statusPromise = fetch('http://127.0.0.1:9001/cloud/provider/status?provider=' + encodeURIComponent(provider))
                        .then(r => r.json());
                }
                statusPromise.then(function (data) {
                    var isAuthed = data && !!data.authenticated;
                    if (authBadge) {
                        authBadge.textContent = isAuthed ? 'Connected ✓' : 'Not Connected';
                        authBadge.style.background = isAuthed ? 'rgba(16, 185, 129, 0.15)' : 'rgba(245, 158, 11, 0.15)';
                        authBadge.style.color = isAuthed ? '#10b981' : '#f59e0b';
                        authBadge.style.border = isAuthed ? '1px solid rgba(16, 185, 129, 0.3)' : '1px solid rgba(245, 158, 11, 0.3)';
                    }
                    // Update email display
                    if (authEmail) {
                        if (isAuthed && data.accountEmail) {
                            authEmail.textContent = data.accountEmail;
                            authEmail.style.display = 'inline';
                        } else {
                            authEmail.textContent = '';
                            authEmail.style.display = 'none';
                        }
                    }
                    renderProviderAuthButtons(provider, isAuthed);
                }).catch(function (err) {
                    console.warn('[CloudSaves] Provider status error:', err);
                    if (authBadge) {
                        authBadge.textContent = 'Not Connected';
                        authBadge.style.background = 'rgba(245, 158, 11, 0.15)';
                        authBadge.style.color = '#f59e0b';
                        authBadge.style.border = '1px solid rgba(245, 158, 11, 0.3)';
                    }
                    renderProviderAuthButtons(provider, false);
                });
            }

            if (window.NaviCloudManager && window.NaviCloudManager.ensureSteamCloudTableStyles) {
                window.NaviCloudManager.ensureSteamCloudTableStyles(document);
            }

            var breadcrumbEl = document.getElementById('sls-cfg-cs-breadcrumb');
            var headingEl = document.getElementById('sls-cfg-cs-view-heading');
            var subtitleEl = document.getElementById('sls-cfg-cs-view-subtitle');
            var backBtnEl = document.getElementById('sls-cfg-cs-back-btn');
            var refreshBtnEl = document.getElementById('sls-cfg-cs-refresh-btn');
            var gameToolbarEl = document.getElementById('sls-cfg-cs-game-toolbar');

            function fmtBytes(b) {
                if (window.NaviCloudManager && window.NaviCloudManager.formatBytes) {
                    return window.NaviCloudManager.formatBytes(b);
                }
                var n = Number(b) || 0;
                if (n < 1024) return n + ' B';
                if (n < 1024 * 1024) return (n / 1024).toFixed(2).replace(/\.00$/, '') + ' KB';
                return (n / (1024 * 1024)).toFixed(2).replace(/\.00$/, '') + ' MB';
            }

            function fmtDate(ts) {
                if (window.NaviCloudManager && window.NaviCloudManager.formatSteamCloudDate) {
                    return window.NaviCloudManager.formatSteamCloudDate(ts);
                }
                if (!ts) return '—';
                return new Date(Number(ts) * 1000).toLocaleString();
            }

            var titleCache = {};
            try {
                titleCache = JSON.parse(localStorage.getItem('navi_app_titles_cache') || '{}');
            } catch (e) {}

            var _titleFetchQueue = [];
            var _activeTitleFetches = 0;
            function queueTitleFetch(aId, span, rRow) {
                _titleFetchQueue.push({ appId: aId, span: span, rRow: rRow });
                processTitleFetchQueue();
            }
            function processTitleFetchQueue() {
                if (_activeTitleFetches >= 3 || _titleFetchQueue.length === 0) return;
                _activeTitleFetches++;
                var item = _titleFetchQueue.shift();
                var sId = String(item.appId);
                fetch('https://store.steampowered.com/api/appdetails?appids=' + encodeURIComponent(sId))
                    .then(function (r) { return r.json(); })
                    .then(function (data) {
                        if (data && data[sId] && data[sId].success && data[sId].data && data[sId].data.name) {
                            var realName = data[sId].data.name;
                            if (!isPlaceholderTitle(realName, sId)) {
                                titleCache[sId] = realName;
                                try { localStorage.setItem('navi_app_titles_cache', JSON.stringify(titleCache)); } catch (e) {}
                                if (item.span) item.span.textContent = realName;
                                if (item.rRow) item.rRow.setAttribute('data-search', (realName + ' ' + sId).toLowerCase());
                            }
                        }
                    })
                    .catch(function () {})
                    .finally(function () {
                        _activeTitleFetches--;
                        setTimeout(processTitleFetchQueue, 80);
                    });
            }

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

            function resolveTitle(appId, backendTitle) {
                var sId = String(appId);
                var cfgTitle = (cfg.GameTitles && (cfg.GameTitles[appId] || cfg.GameTitles[sId])) || '';
                if (cfgTitle && !isPlaceholderTitle(cfgTitle, sId)) return cfgTitle;
                if (titleCache[sId]) {
                    if (!isPlaceholderTitle(titleCache[sId], sId)) {
                        return titleCache[sId];
                    }
                    delete titleCache[sId];
                    try { localStorage.setItem('navi_app_titles_cache', JSON.stringify(titleCache)); } catch (e) {}
                }
                if (window.NaviCloudManager && window.NaviCloudManager.resolveGameTitle) {
                    var gmTitle = window.NaviCloudManager.resolveGameTitle(appId, backendTitle);
                    if (gmTitle && !isPlaceholderTitle(gmTitle, sId)) return gmTitle;
                }
                if (backendTitle && !isPlaceholderTitle(backendTitle, sId)) return backendTitle;
                return backendTitle || ('App ' + appId);
            }

            function refreshBackupsList() {
                var backupsListEl = document.getElementById('sls-cfg-cs-backups-list');
                if (!backupsListEl) return;
                var backupsSearchInput = document.getElementById('sls-cfg-cs-backups-search');
                var currentFilter = backupsSearchInput ? backupsSearchInput.value.trim().toLowerCase() : '';

                backupsListEl.innerHTML = '<div style="padding: 20px 16px; text-align: center; color: #8f98a0; font-size: 13px;">Loading backups...</div>';

                var backupsPromise = (window.NaviCloudManager && window.NaviCloudManager.fetchAllBackups)
                    ? window.NaviCloudManager.fetchAllBackups()
                    : fetch('http://127.0.0.1:9001/cloud/backups').then(function (r) { return r.json(); });

                backupsPromise.then(function (bData) {
                    var backups = (bData && Array.isArray(bData.backups)) ? bData.backups : [];
                    if (backups.length === 0) {
                        backupsListEl.innerHTML = '<div style="padding: 24px 16px; text-align: center; color: #8f98a0; font-size: 13px;">No backups found. Backups are created automatically before resetting app data or manually from a game\'s toolbar.</div>';
                        return;
                    }

                    var bTable = document.createElement('table');
                    bTable.className = 'navi-steam-cloud-table';
                    bTable.style.tableLayout = 'fixed';
                    bTable.style.width = '100%';
                    bTable.innerHTML =
                        '<thead><tr>' +
                            '<th scope="col" style="width: 26%; text-align: left;">GAME</th>' +
                            '<th scope="col" style="width: 36%; text-align: left;">BACKUP ARCHIVE</th>' +
                            '<th scope="col" style="width: 18%; text-align: right;">DATE</th>' +
                            '<th scope="col" style="width: 10%; text-align: right;">SIZE</th>' +
                            '<th scope="col" style="width: 10%; text-align: right;">ACTIONS</th>' +
                        '</tr></thead>';

                    var bTbody = document.createElement('tbody');
                    backups.forEach(function (b) {
                        var bAppId = b.appId;
                        var bTitle = resolveTitle(bAppId, b.gameTitle);
                        var bFile = b.fileName || (b.fullPath ? b.fullPath.split('/').pop() : '');
                        var bPath = b.fullPath || bFile;

                        var bTr = document.createElement('tr');
                        bTr.className = 'navi-cloud-backup-row';
                        var bSearch = (bTitle + ' ' + bAppId + ' ' + bFile).toLowerCase();
                        bTr.setAttribute('data-search', bSearch);
                        if (currentFilter && bSearch.indexOf(currentFilter) === -1) {
                            bTr.style.display = 'none';
                        }

                        var tdBGame = document.createElement('td');
                        tdBGame.setAttribute('data-label', 'GAME');
                        tdBGame.style.textAlign = 'left';
                        tdBGame.style.overflow = 'hidden';
                        tdBGame.style.textOverflow = 'ellipsis';
                        tdBGame.style.whiteSpace = 'nowrap';
                        tdBGame.innerHTML = '<span style="color:#ffffff;font-weight:500;">' + escapeHtml(bTitle) + '</span>' +
                            '<span style="font-size:11px;color:#8f98a0;margin-left:6px;">(' + escapeHtml(String(bAppId)) + ')</span>';

                        var tdBFile = document.createElement('td');
                        tdBFile.setAttribute('data-label', 'BACKUP ARCHIVE');
                        tdBFile.style.textAlign = 'left';
                        tdBFile.style.fontFamily = 'monospace';
                        tdBFile.style.fontSize = '12px';
                        tdBFile.style.overflow = 'hidden';
                        tdBFile.style.textOverflow = 'ellipsis';
                        tdBFile.style.whiteSpace = 'nowrap';
                        tdBFile.textContent = bFile;

                        var tdBDate = document.createElement('td');
                        tdBDate.className = 'navi-col-num';
                        tdBDate.setAttribute('data-label', 'DATE');
                        tdBDate.style.textAlign = 'right';
                        tdBDate.textContent = fmtDate(b.timestamp);

                        var tdBSize = document.createElement('td');
                        tdBSize.className = 'navi-col-num';
                        tdBSize.setAttribute('data-label', 'SIZE');
                        tdBSize.style.textAlign = 'right';
                        tdBSize.textContent = fmtBytes(b.sizeBytes);

                        var tdBActions = document.createElement('td');
                        tdBActions.setAttribute('data-label', 'ACTIONS');
                        tdBActions.style.textAlign = 'right';
                        tdBActions.style.whiteSpace = 'nowrap';

                        var restoreBtn = document.createElement('button');
                        restoreBtn.type = 'button';
                        restoreBtn.className = 'sls-cfg-btn';
                        restoreBtn.style.cssText = 'padding: 4px 10px; font-size: 11.5px; margin-right: 6px; color: #38bdf8; border-color: rgba(56, 189, 248, 0.35); background: rgba(26, 159, 255, 0.12);';
                        restoreBtn.textContent = 'Restore';
                        restoreBtn.onclick = function (e) {
                            e.preventDefault();
                            restoreBtn.disabled = true;
                            restoreBtn.textContent = 'Queueing...';
                            var rProm = (window.NaviCloudManager && window.NaviCloudManager.restoreAppSave)
                                ? window.NaviCloudManager.restoreAppSave(bAppId, bPath)
                                : fetch('http://127.0.0.1:9001/cloud/apps/' + encodeURIComponent(bAppId) + '/restore', {
                                    method: 'POST',
                                    headers: { 'Content-Type': 'text/plain' },
                                    body: 'X-Navi-Auth: ' + (window.__naviAuthToken || '') + '\r\n' + JSON.stringify({ backupFile: bPath })
                                }).then(function (r) { return r.json(); });
                            rProm.then(function (res) {
                                restoreBtn.disabled = false;
                                restoreBtn.textContent = 'Restore';
                                if (res && res.error) {
                                    showStatusBanner('Restore failed: ' + res.error, '#ff4d4d');
                                } else {
                                    showStatusBanner('Queued restore of ' + bFile + ' for ' + bTitle + '. Restart Steam to apply.', '#2ecc71');
                                    refreshCloudSavesUI();
                                }
                            }).catch(function (err) {
                                restoreBtn.disabled = false;
                                restoreBtn.textContent = 'Restore';
                                showStatusBanner('Restore error: ' + (err.message || 'Unknown error'), '#ff4d4d');
                            });
                        };

                        var deleteBtn = document.createElement('button');
                        deleteBtn.type = 'button';
                        deleteBtn.className = 'sls-cfg-btn sls-cfg-btn-danger';
                        deleteBtn.style.cssText = 'padding: 4px 10px; font-size: 11.5px;';
                        deleteBtn.textContent = 'Delete';
                        deleteBtn.onclick = function (e) {
                            e.preventDefault();
                            deleteBtn.disabled = true;
                            deleteBtn.textContent = 'Deleting...';
                            var dProm = (window.NaviCloudManager && window.NaviCloudManager.deleteBackupArchive)
                                ? window.NaviCloudManager.deleteBackupArchive(bAppId, bFile)
                                : fetch('http://127.0.0.1:9001/cloud/backups/delete', {
                                    method: 'POST',
                                    headers: { 'Content-Type': 'text/plain' },
                                    body: 'X-Navi-Auth: ' + (window.__naviAuthToken || '') + '\r\n' + JSON.stringify({ appId: Number(bAppId), fileName: bFile })
                                }).then(function (r) { return r.json(); });
                            dProm.then(function (res) {
                                if (res && res.success !== false && !res.error) {
                                    showStatusBanner('Deleted backup archive ' + bFile + '.', '#66c0f4');
                                    refreshBackupsList();
                                } else {
                                    deleteBtn.disabled = false;
                                    deleteBtn.textContent = 'Delete';
                                    showStatusBanner('Could not delete backup: ' + ((res && (res.error || res.message)) || 'Unknown error'), '#ff4d4d');
                                }
                            }).catch(function (err) {
                                deleteBtn.disabled = false;
                                deleteBtn.textContent = 'Delete';
                                showStatusBanner('Delete error: ' + (err.message || 'Unknown error'), '#ff4d4d');
                            });
                        };

                        tdBActions.appendChild(restoreBtn);
                        tdBActions.appendChild(deleteBtn);
                        bTr.appendChild(tdBGame);
                        bTr.appendChild(tdBFile);
                        bTr.appendChild(tdBDate);
                        bTr.appendChild(tdBSize);
                        bTr.appendChild(tdBActions);
                        bTbody.appendChild(bTr);
                    });

                    bTable.appendChild(bTbody);
                    backupsListEl.innerHTML = '';
                    backupsListEl.appendChild(bTable);
                }).catch(function (err) {
                    backupsListEl.innerHTML = '<div style="padding: 20px; text-align: center; color: #f87171; font-size: 13px;">Failed to load backups: ' + escapeHtml(err.message || 'Unknown error') + '</div>';
                });
            }

            refreshBackupsList();

            var backupsRefreshBtn = document.getElementById('sls-cfg-cs-backups-refresh');
            if (backupsRefreshBtn) {
                backupsRefreshBtn.onclick = function (e) {
                    e.preventDefault();
                    refreshBackupsList();
                };
            }

            var scanOrphansBtnEl = document.getElementById('sls-cfg-cs-scan-orphans-btn');
            var orphansPanelEl = document.getElementById('sls-cfg-cs-orphans-panel');

            function scanOrphansAndRender() {
                if (!orphansPanelEl) return;
                orphansPanelEl.style.display = 'block';
                orphansPanelEl.innerHTML = '<div style="font-size: 12.5px; color: #8a929e; padding: 6px 2px;">Scanning for orphaned save directories and unreferenced cloud blobs...</div>';
                if (scanOrphansBtnEl) {
                    scanOrphansBtnEl.disabled = true;
                    scanOrphansBtnEl.textContent = 'Scanning...';
                }

                var orphansPromise = (window.NaviCloudManager && window.NaviCloudManager.fetchOrphanedSaves)
                    ? window.NaviCloudManager.fetchOrphanedSaves(provider)
                    : fetch('http://127.0.0.1:9001/cloud/orphans?provider=' + encodeURIComponent(provider)).then(function (r) { return r.json(); });

                orphansPromise.then(function (oData) {
                    if (scanOrphansBtnEl) {
                        scanOrphansBtnEl.disabled = false;
                        scanOrphansBtnEl.textContent = 'Scan Orphans';
                    }
                    var orphans = (oData && Array.isArray(oData.orphans)) ? oData.orphans : [];
                    orphansPanelEl.innerHTML = '';

                    var headerRow = document.createElement('div');
                    headerRow.style.cssText = 'display: flex; justify-content: space-between; align-items: center; gap: 8px; margin-bottom: 8px;';
                    var titleDiv = document.createElement('div');
                    titleDiv.style.cssText = 'font-size: 13px; font-weight: 600; color: #38bdf8;';
                    titleDiv.textContent = 'Orphaned saves & unreferenced blobs (' + orphans.length + ')';
                    var hideBtn = document.createElement('button');
                    hideBtn.type = 'button';
                    hideBtn.className = 'sls-cfg-btn';
                    hideBtn.style.cssText = 'padding: 3px 8px; font-size: 11px;';
                    hideBtn.textContent = 'Hide';
                    hideBtn.onclick = function (e) {
                        e.preventDefault();
                        orphansPanelEl.style.display = 'none';
                    };
                    headerRow.appendChild(titleDiv);
                    headerRow.appendChild(hideBtn);
                    orphansPanelEl.appendChild(headerRow);

                    if (orphans.length === 0) {
                        var emptyMsg = document.createElement('div');
                        emptyMsg.style.cssText = 'font-size: 12.5px; color: #94a3b8; padding: 6px 2px;';
                        emptyMsg.textContent = 'No orphaned save directories or unreferenced cloud blobs found.';
                        orphansPanelEl.appendChild(emptyMsg);
                        return;
                    }

                    var listDiv = document.createElement('div');
                    listDiv.style.cssText = 'display: flex; flex-direction: column; gap: 6px;';
                    orphans.forEach(function (orph) {
                        var oAppId = orph.appId;
                        var oTitle = resolveTitle(oAppId, orph.gameTitle);
                        var row = document.createElement('div');
                        row.style.cssText = 'display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 10px; padding: 8px 10px; background: #171a22; border: 1px solid rgba(255, 255, 255, 0.06); border-radius: 4px;';

                        var infoWrap = document.createElement('div');
                        infoWrap.style.cssText = 'display: flex; align-items: center; gap: 8px; flex-wrap: wrap;';
                        var badgesHtml = '';
                        if (orph.inLocalStorage) {
                            badgesHtml += '<span style="padding: 2px 6px; border-radius: 3px; font-size: 10.5px; background: rgba(56, 189, 248, 0.15); color: #38bdf8; border: 1px solid rgba(56, 189, 248, 0.3);">Local Storage</span>';
                        }
                        if (orph.inRemoteCloud) {
                            badgesHtml += '<span style="padding: 2px 6px; border-radius: 3px; font-size: 10.5px; background: rgba(16, 185, 129, 0.15); color: #34d399; border: 1px solid rgba(16, 185, 129, 0.3);">Remote Cloud</span>';
                        }
                        if (orph.inGoldbergPrefix) {
                            badgesHtml += '<span style="padding: 2px 6px; border-radius: 3px; font-size: 10.5px; background: rgba(245, 158, 11, 0.15); color: #fbbf24; border: 1px solid rgba(245, 158, 11, 0.3);">Wine Prefix</span>';
                        }
                        infoWrap.innerHTML = '<span style="color: #ffffff; font-weight: 500; font-size: 13px;">' + escapeHtml(oTitle) + '</span>' +
                            '<span style="color: #8a929e; font-size: 11.5px;">(' + escapeHtml(String(oAppId)) + ')</span>' +
                            '<span style="color: #cbd5e1; font-size: 12px;">' + (Number(orph.fileCount) || 0) + ' files &bull; ' + fmtBytes(orph.totalSizeBytes) + '</span>' +
                            badgesHtml;

                        var actionsWrap = document.createElement('div');
                        actionsWrap.style.cssText = 'display: flex; gap: 6px; align-items: center;';

                        var adoptBtn = document.createElement('button');
                        adoptBtn.type = 'button';
                        adoptBtn.className = 'sls-cfg-btn sls-cfg-btn-primary';
                        adoptBtn.style.cssText = 'padding: 4px 10px; font-size: 11.5px;';
                        adoptBtn.textContent = 'Adopt (Redirect)';
                        adoptBtn.onclick = function (e) {
                            e.preventDefault();
                            adoptBtn.disabled = true;
                            adoptBtn.textContent = 'Adopting...';
                            var aProm = (window.NaviCloudManager && window.NaviCloudManager.toggleAppRedirect)
                                ? window.NaviCloudManager.toggleAppRedirect(oAppId, true)
                                : fetch('http://127.0.0.1:9001/cloud/apps/' + encodeURIComponent(oAppId) + '/toggle', {
                                    method: 'POST',
                                    headers: { 'Content-Type': 'text/plain' },
                                    body: 'X-Navi-Auth: ' + (window.__naviAuthToken || '') + '\r\n' + JSON.stringify({ redirect: true })
                                }).then(function (r) { return r.json(); });
                            aProm.then(function () {
                                if (!cfg.CloudSaves) cfg.CloudSaves = {};
                                if (!Array.isArray(cfg.CloudSaves.ManagedApps)) cfg.CloudSaves.ManagedApps = [];
                                if (cfg.CloudSaves.ManagedApps.indexOf(Number(oAppId)) === -1) {
                                    cfg.CloudSaves.ManagedApps.push(Number(oAppId));
                                }
                                showStatusBanner('Adopted ' + oTitle + ' (' + oAppId + ') into redirected cloud saves.', '#2ecc71');
                                scanOrphansAndRender();
                                refreshCloudSavesUI();
                            }).catch(function (err) {
                                adoptBtn.disabled = false;
                                adoptBtn.textContent = 'Adopt (Redirect)';
                                showStatusBanner('Failed to adopt app: ' + (err.message || 'Unknown error'), '#ff4d4d');
                            });
                        };

                        var cleanBtn = document.createElement('button');
                        cleanBtn.type = 'button';
                        cleanBtn.className = 'sls-cfg-btn sls-cfg-btn-danger';
                        cleanBtn.style.cssText = 'padding: 4px 10px; font-size: 11.5px;';
                        cleanBtn.textContent = 'Clean / GC';
                        cleanBtn.onclick = function (e) {
                            e.preventDefault();
                            cleanBtn.disabled = true;
                            cleanBtn.textContent = 'Cleaning...';
                            var cProm = (window.NaviCloudManager && window.NaviCloudManager.cleanOrphanedApp)
                                ? window.NaviCloudManager.cleanOrphanedApp(oAppId, provider, false)
                                : fetch('http://127.0.0.1:9001/cloud/orphans/clean', {
                                    method: 'POST',
                                    headers: { 'Content-Type': 'text/plain' },
                                    body: 'X-Navi-Auth: ' + (window.__naviAuthToken || '') + '\r\n' + JSON.stringify({ appId: Number(oAppId), provider: provider, deleteFiles: false })
                                }).then(function (r) { return r.json(); });
                            cProm.then(function (res) {
                                if (res && res.success !== false && !res.error) {
                                    showStatusBanner('Cleaned unreferenced blobs for ' + oTitle + ' (' + (Number(res.blobsDeleted) || 0) + ' blobs removed).', '#66c0f4');
                                    scanOrphansAndRender();
                                } else {
                                    cleanBtn.disabled = false;
                                    cleanBtn.textContent = 'Clean / GC';
                                    showStatusBanner('Clean failed: ' + ((res && (res.error || res.message)) || 'Unknown error'), '#ff4d4d');
                                }
                            }).catch(function (err) {
                                cleanBtn.disabled = false;
                                cleanBtn.textContent = 'Clean / GC';
                                showStatusBanner('Clean error: ' + (err.message || 'Unknown error'), '#ff4d4d');
                            });
                        };

                        actionsWrap.appendChild(adoptBtn);
                        actionsWrap.appendChild(cleanBtn);
                        row.appendChild(infoWrap);
                        row.appendChild(actionsWrap);
                        listDiv.appendChild(row);
                    });
                    orphansPanelEl.appendChild(listDiv);
                }).catch(function (err) {
                    if (scanOrphansBtnEl) {
                        scanOrphansBtnEl.disabled = false;
                        scanOrphansBtnEl.textContent = 'Scan Orphans';
                    }
                    orphansPanelEl.innerHTML = '<div style="font-size: 12.5px; color: #f87171;">Failed to scan orphans: ' + escapeHtml(err.message || 'Unknown error') + '</div>';
                });
            }

            if (scanOrphansBtnEl) {
                scanOrphansBtnEl.onclick = function (e) {
                    e.preventDefault();
                    scanOrphansAndRender();
                };
            }

            if (backBtnEl) {
                backBtnEl.onclick = function (e) {
                    e.preventDefault();
                    csState.selectedAppId = null;
                    csState.selectedGameTitle = '';
                    refreshCloudSavesUI();
                };
            }
            if (refreshBtnEl) {
                refreshBtnEl.onclick = function (e) {
                    e.preventDefault();
                    refreshCloudSavesUI();
                };
            }

            if (!appsListContainer) return;

            // Drill-down Per-Game Files View (Steam Screenshot 2)
            if (csState.selectedAppId) {
                var selAppId = csState.selectedAppId;
                var selTitle = resolveTitle(selAppId, csState.selectedGameTitle);

                function updateDrilldownBreadcrumb(title) {
                    if (!breadcrumbEl) return;
                    breadcrumbEl.innerHTML = 'Home &gt; Account &gt; <a href="#" id="sls-cfg-cs-crumb-back" style="color: #38bdf8; text-decoration: none; cursor: pointer; font-weight: 500;">View Steam Cloud</a> &gt; ' + escapeHtml(title);
                    var crumbBack = document.getElementById('sls-cfg-cs-crumb-back');
                    if (crumbBack) {
                        crumbBack.onclick = function (e) {
                            e.preventDefault();
                            csState.selectedAppId = null;
                            csState.selectedGameTitle = '';
                            refreshCloudSavesUI();
                        };
                    }
                }

                updateDrilldownBreadcrumb(selTitle);
                if (headingEl) headingEl.textContent = selTitle;
                if (subtitleEl) subtitleEl.textContent = 'This game files are saved in your Steam Cloud :';
                if (backBtnEl) backBtnEl.style.display = 'inline-block';
                if (scanOrphansBtnEl) scanOrphansBtnEl.style.display = 'none';
                if (addAppInput) addAppInput.placeholder = 'Search files in cloud...';

                if (isPlaceholderTitle(selTitle, selAppId)) {
                    (function (sId) {
                        fetch('https://store.steampowered.com/api/appdetails?appids=' + encodeURIComponent(sId))
                            .then(function (r) { return r.json(); })
                            .then(function (data) {
                                if (data && data[sId] && data[sId].success && data[sId].data && data[sId].data.name) {
                                    var realName = data[sId].data.name;
                                    if (!isPlaceholderTitle(realName, sId)) {
                                        titleCache[sId] = realName;
                                        try { localStorage.setItem('navi_app_titles_cache', JSON.stringify(titleCache)); } catch (e) {}
                                        if (headingEl) headingEl.textContent = realName;
                                        updateDrilldownBreadcrumb(realName);
                                    }
                                }
                            })
                            .catch(function () {});
                    })(String(selAppId));
                }

                if (gameToolbarEl) {
                    gameToolbarEl.style.display = 'flex';
                    gameToolbarEl.innerHTML =
                        '<div style="display: flex; align-items: center; gap: 8px; flex-wrap: wrap;">' +
                            '<span style="background: rgba(102, 192, 244, 0.12); color: #66c0f4; border: 1px solid rgba(102, 192, 244, 0.28); border-radius: 3px; padding: 3px 8px; font-size: 11.5px; font-weight: 600;">AppID ' + escapeHtml(String(selAppId)) + '</span>' +
                            '<span style="background: rgba(255, 255, 255, 0.05); color: #c6d4df; border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 3px; padding: 3px 8px; font-size: 11.5px;">Provider: <b style="color: #66c0f4;">' + escapeHtml(provider) + '</b></span>' +
                        '</div>' +
                        '<div style="display: flex; gap: 6px; align-items: center; flex-wrap: wrap;">' +
                            '<button id="sls-cfg-cs-app-sync" class="sls-cfg-btn" style="background: #2a475e; border: 1px solid rgba(102, 192, 244, 0.4); color: #66c0f4; padding: 5px 12px; font-size: 12px; cursor: pointer; border-radius: 3px; font-weight: 600;">Sync Now</button>' +
                            '<button id="sls-cfg-cs-app-backup" class="sls-cfg-btn" style="background: #273b4b; border: 1px solid rgba(255, 255, 255, 0.14); color: #c6d4df; padding: 5px 12px; font-size: 12px; cursor: pointer; border-radius: 3px;">Backup</button>' +
                            '<button id="sls-cfg-cs-app-restore" class="sls-cfg-btn" style="background: #273b4b; border: 1px solid rgba(255, 255, 255, 0.14); color: #c6d4df; padding: 5px 12px; font-size: 12px; cursor: pointer; border-radius: 3px;">Restore</button>' +
                            '<button id="sls-cfg-cs-app-reset" class="sls-cfg-btn" style="background: rgba(239, 68, 68, 0.16); border: 1px solid rgba(239, 68, 68, 0.38); color: #fca5a5; padding: 5px 12px; font-size: 12px; cursor: pointer; border-radius: 3px;">Reset</button>' +
                            '<button id="sls-cfg-cs-app-remove" class="sls-cfg-btn" style="background: rgba(255, 255, 255, 0.05); border: 1px solid rgba(255, 255, 255, 0.12); color: #9ca3af; padding: 5px 12px; font-size: 12px; cursor: pointer; border-radius: 3px;">Remove Redirect</button>' +
                        '</div>';

                    var btnToolbarBack = document.getElementById('sls-cfg-cs-toolbar-back');
                    if (btnToolbarBack) {
                        btnToolbarBack.onclick = function (e) {
                            e.preventDefault();
                            csState.selectedAppId = null;
                            csState.selectedGameTitle = '';
                            refreshCloudSavesUI();
                        };
                    }

                    var btnSync = document.getElementById('sls-cfg-cs-app-sync');
                    if (btnSync) {
                        btnSync.onclick = function (e) {
                            e.preventDefault();
                            btnSync.textContent = 'Syncing...';
                            btnSync.disabled = true;
                            var pSync = (window.NaviCloudManager && window.NaviCloudManager.syncAppNow)
                                ? window.NaviCloudManager.syncAppNow(selAppId)
                                : fetch('http://127.0.0.1:9001/cloud/apps/' + encodeURIComponent(selAppId) + '/sync', {
                                    method: 'POST',
                                    headers: { 'Content-Type': 'text/plain' },
                                    body: 'X-Navi-Auth: ' + (window.__naviAuthToken || '') + '\r\n{}'
                                }).then(function (r) { return r.json(); });
                            pSync.then(function (res) {
                                showStatusBanner((res && res.message) ? res.message : ('Synced App ' + selAppId), '#2ecc71');
                                refreshCloudSavesUI();
                            }).catch(function (err) {
                                showStatusBanner('Sync failed: ' + (err.message || 'Unknown error'), '#ff4d4d');
                                refreshCloudSavesUI();
                            });
                        };
                    }
                    var btnBackup = document.getElementById('sls-cfg-cs-app-backup');
                    if (btnBackup) {
                        btnBackup.onclick = function (e) {
                            e.preventDefault();
                            if (window.NaviCloudManager && window.NaviCloudManager.backupAppSave) {
                                window.NaviCloudManager.backupAppSave(selAppId).then(function (res) {
                                    showStatusBanner('Save backup created: ' + ((res && res.backupFile) || ('App ' + selAppId)), '#2ecc71');
                                    refreshBackupsList();
                                }).catch(function (err) {
                                    showStatusBanner('Backup failed: ' + (err.message || 'Unknown error'), '#ff4d4d');
                                });
                            }
                        };
                    }
                    var btnRestore = document.getElementById('sls-cfg-cs-app-restore');
                    if (btnRestore) {
                        btnRestore.onclick = function (e) {
                            e.preventDefault();
                            if (window.NaviCloudManager && window.NaviCloudManager.openRestoreModal) {
                                window.NaviCloudManager.openRestoreModal(selAppId, selTitle);
                            }
                        };
                    }
                    var btnReset = document.getElementById('sls-cfg-cs-app-reset');
                    if (btnReset) {
                        btnReset.onclick = function (e) {
                            e.preventDefault();
                            if (window.NaviCloudManager && window.NaviCloudManager.openResetModal) {
                                window.NaviCloudManager.openResetModal(selAppId, selTitle);
                            }
                        };
                    }
                    var btnRemove = document.getElementById('sls-cfg-cs-app-remove');
                    if (btnRemove) {
                        btnRemove.onclick = function (e) {
                            e.preventDefault();
                            function finishRemove() {
                                if (cfg.CloudSaves && Array.isArray(cfg.CloudSaves.ManagedApps)) {
                                    cfg.CloudSaves.ManagedApps = cfg.CloudSaves.ManagedApps.filter(function (x) { return String(x) !== String(selAppId); });
                                }
                                csState.selectedAppId = null;
                                refreshCloudSavesUI();
                                checkDirty();
                                showStatusBanner('Removed App ' + selAppId + ' from managed redirects.', '#66c0f4');
                            }
                            if (window.NaviCloudManager && window.NaviCloudManager.toggleAppRedirect) {
                                window.NaviCloudManager.toggleAppRedirect(selAppId, false).then(finishRemove).catch(finishRemove);
                            } else {
                                finishRemove();
                            }
                        };
                    }
                }

                appsListContainer.innerHTML = '<div style="padding: 28px 16px; text-align: center; color: #8f98a0; font-size: 13px;">Scanning cloud storage for ' + escapeHtml(selTitle) + ' (' + escapeHtml(String(selAppId)) + ')...</div>';

                var filesPromise = (window.NaviCloudManager && window.NaviCloudManager.fetchAppCloudFiles)
                    ? window.NaviCloudManager.fetchAppCloudFiles(selAppId, provider)
                    : fetch('http://127.0.0.1:9001/cloud/apps/' + encodeURIComponent(selAppId) + '/files?provider=' + encodeURIComponent(provider)).then(function (r) { return r.json(); });

                filesPromise.then(function (data) {
                    if (csState.selectedAppId !== selAppId) return;
                    var finalTitle = resolveTitle(selAppId, (data && data.gameTitle) ? data.gameTitle : selTitle);
                    if (headingEl) headingEl.textContent = finalTitle;
                    updateDrilldownBreadcrumb(finalTitle);

                    var files = (data && Array.isArray(data.files)) ? data.files : [];
                    if (files.length === 0) {
                        appsListContainer.innerHTML = '<div style="padding: 32px 16px; text-align: center; color: #8f98a0; font-size: 13px;">No files are currently stored in your Steam Cloud for <b>' + escapeHtml(finalTitle) + '</b>. Play the game or click <b>Sync Now</b> above to upload saves.</div>';
                        return;
                    }

                    CloudFileViewerModule.render(appsListContainer, files, selAppId, finalTitle, provider, showStatusBanner);
                }).catch(function (err) {
                    appsListContainer.innerHTML = '<div style="padding: 24px; text-align: center; color: #f87171; font-size: 13px;">Failed to load cloud files: ' + escapeHtml(err.message || 'Unknown error') + '</div>';
                });
                return;
            }

            // Overview Games Table (Steam Screenshot 1)
            if (breadcrumbEl) breadcrumbEl.textContent = 'Home > Account > View Steam Cloud';
            if (headingEl) headingEl.textContent = 'View Steam Cloud';
            if (subtitleEl) subtitleEl.textContent = 'View game files saved to your Steam Cloud';
            if (backBtnEl) backBtnEl.style.display = 'none';
            if (scanOrphansBtnEl) scanOrphansBtnEl.style.display = 'inline-block';
            if (gameToolbarEl) gameToolbarEl.style.display = 'none';
            if (addAppInput) addAppInput.placeholder = 'Search games or AppID...';

            appsListContainer.innerHTML = '<div style="padding: 28px 16px; text-align: center; color: #8f98a0; font-size: 13px;">Scanning cloud storage (' + escapeHtml(provider) + ')...</div>';

            var appsPromise = (window.NaviCloudManager && window.NaviCloudManager.fetchCloudApps)
                ? window.NaviCloudManager.fetchCloudApps(provider)
                : fetch('http://127.0.0.1:9001/cloud/apps?provider=' + encodeURIComponent(provider)).then(function (r) { return r.json(); });

            appsPromise.then(function (data) {
                if (csState.selectedAppId) return;
                var apps = (data && Array.isArray(data.apps)) ? data.apps.slice() : [];
                var seen = {};
                apps.forEach(function (a) {
                    seen[String(a.appId)] = true;
                    if (a.childAppIds && Array.isArray(a.childAppIds)) {
                        a.childAppIds.forEach(function (cid) {
                            seen[String(cid)] = true;
                        });
                    }
                });

                var managedApps = (cfg.CloudSaves && cfg.CloudSaves.ManagedApps) || [];
                managedApps.forEach(function (mId) {
                    if (mId && !seen[String(mId)]) {
                        apps.push({
                            appId: Number(mId),
                            gameTitle: resolveTitle(mId, ''),
                            fileCount: 0,
                            totalSizeBytes: 0,
                            redirected: true
                        });
                        seen[String(mId)] = true;
                    }
                });

                if (managedBadge) {
                    managedBadge.textContent = apps.length + (apps.length === 1 ? ' Game in Cloud' : ' Games in Cloud');
                }

                if (apps.length === 0) {
                    appsListContainer.innerHTML = '<div style="color: #8f98a0; font-size: 13px; padding: 28px 16px; text-align: center;">No games found in your Steam Cloud yet. Play a game with Cloud Saves enabled.</div>';
                    return;
                }

                var table = document.createElement('table');
                table.className = 'navi-steam-cloud-table';
                table.style.tableLayout = 'fixed';
                table.style.width = '100%';
                table.innerHTML =
                    '<thead><tr>' +
                        '<th scope="col" style="width: 48%; text-align: left;">GAME</th>' +
                        '<th scope="col" style="width: 18%; text-align: right;">NUMBER OF FILES</th>' +
                        '<th scope="col" style="width: 18%; text-align: right;">TOTAL SIZE OF FILES</th>' +
                        '<th scope="col" style="width: 16%; text-align: right;"></th>' +
                    '</tr></thead>';

                var tbody = document.createElement('tbody');
                apps.forEach(function (app) {
                    var appId = app.appId;
                    var gameTitle = resolveTitle(appId, app.gameTitle);

                    var tr = document.createElement('tr');
                    tr.className = 'sls-cfg-item navi-cloud-game-row';
                    var searchTerms = gameTitle + ' ' + appId;
                    if (app.childAppIds && app.childAppIds.length > 0) {
                        searchTerms += ' ' + app.childAppIds.join(' ');
                    }
                    tr.setAttribute('data-search', searchTerms.toLowerCase());

                    var tdGame = document.createElement('td');
                    tdGame.setAttribute('data-label', 'GAME');
                    tdGame.style.textAlign = 'left';
                    tdGame.style.overflow = 'hidden';
                    tdGame.style.textOverflow = 'ellipsis';
                    tdGame.style.whiteSpace = 'nowrap';

                    var titleSpan = document.createElement('span');
                    titleSpan.style.cssText = 'color: #ffffff; font-weight: 500; font-size: 13px;';
                    titleSpan.textContent = gameTitle;
                    tdGame.appendChild(titleSpan);

                    var idSpan = document.createElement('span');
                    idSpan.style.cssText = 'font-size: 11px; color: #8f98a0; margin-left: 6px;';
                    idSpan.textContent = '(' + escapeHtml(String(appId)) + ')';
                    tdGame.appendChild(idSpan);

                    if (isPlaceholderTitle(gameTitle, appId)) {
                        queueTitleFetch(appId, titleSpan, tr);
                    }

                    var tdCount = document.createElement('td');
                    tdCount.className = 'navi-col-num';
                    tdCount.setAttribute('data-label', 'NUMBER OF FILES');
                    tdCount.style.textAlign = 'right';
                    tdCount.textContent = String(Number(app.fileCount) || 0);

                    var tdSize = document.createElement('td');
                    tdSize.className = 'navi-col-num';
                    tdSize.setAttribute('data-label', 'TOTAL SIZE OF FILES');
                    tdSize.style.textAlign = 'right';
                    tdSize.textContent = fmtBytes(app.totalSizeBytes);

                    var tdShow = document.createElement('td');
                    tdShow.setAttribute('data-label', 'ACTION');
                    tdShow.style.textAlign = 'right';

                    var showBtn = document.createElement('button');
                    showBtn.className = 'navi-steam-cloud-link';
                    showBtn.textContent = 'Show Files';
                    showBtn.onclick = function (e) {
                        e.preventDefault();
                        csState.selectedAppId = appId;
                        csState.selectedGameTitle = gameTitle;
                        refreshCloudSavesUI();
                    };

                    tdShow.appendChild(showBtn);
                    tr.appendChild(tdGame);
                    tr.appendChild(tdCount);
                    tr.appendChild(tdSize);
                    tr.appendChild(tdShow);
                    tbody.appendChild(tr);
                });

                table.appendChild(tbody);
                appsListContainer.innerHTML = '';
                appsListContainer.appendChild(table);
            }).catch(function (err) {
                appsListContainer.innerHTML = '<div style="padding: 20px; text-align: center; color: #f87171; font-size: 13px;">Failed to scan cloud games: ' + escapeHtml(err.message || 'Unknown error') + '</div>';
            });
        }

        var masterBtn = document.getElementById('sls-cfg-cs-master-btn');
        if (masterBtn) {
            masterBtn.onclick = function (e) {
                e.preventDefault();
                if (window.NaviCloudManager && window.NaviCloudManager.toggleMaster) {
                    window.NaviCloudManager.toggleMaster(!csState.enabled).then(function () {
                        if (!cfg.CloudSaves) cfg.CloudSaves = {};
                        cfg.CloudSaves.Enabled = !csState.enabled;
                        refreshCloudSavesUI();
                        checkDirty();
                    }).catch(function (err) {
                        console.warn('[CloudSaves] toggleMaster cancelled or failed:', err);
                    });
                } else {
                    if (!cfg.CloudSaves) cfg.CloudSaves = {};
                    cfg.CloudSaves.Enabled = !csState.enabled;
                    refreshCloudSavesUI();
                    checkDirty();
                }
            };
        }

        var csRestartBtn = document.getElementById('sls-cfg-cs-restart-btn');
        if (csRestartBtn) {
            csRestartBtn.onclick = function (e) {
                e.preventDefault();
                if (window.NaviCloudManager && window.NaviCloudManager.restartSteam) {
                    window.NaviCloudManager.restartSteam().catch(function (err) {
                        console.warn('[CloudSaves] Restart error:', err);
                    });
                } else {
                    fetch('http://127.0.0.1:9001/restart', { method: 'POST' }).catch(function () {});
                }
            };
        }

        var providerSel = document.getElementById('sls-cfg-cs-provider');
        if (providerSel) {
            providerSel.onchange = function () {
                if (!cfg.CloudSaves) cfg.CloudSaves = {};
                cfg.CloudSaves.Provider = providerSel.value;
                refreshCloudSavesUI();
                checkDirty();
            };
        }

        // R2 & S3 Credentials form handlers
        var credSecretToggleBtn = document.getElementById('sls-cfg-cs-cred-secret-toggle');
        var credSecretKeyInput = document.getElementById('sls-cfg-cs-cred-secret-key');
        if (credSecretToggleBtn && credSecretKeyInput) {
            credSecretToggleBtn.onclick = function (e) {
                e.preventDefault();
                var isPwd = (credSecretKeyInput.type === 'password');
                credSecretKeyInput.type = isPwd ? 'text' : 'password';
                credSecretToggleBtn.textContent = isPwd ? 'Hide' : 'Show';
                credSecretToggleBtn.setAttribute('aria-pressed', isPwd ? 'true' : 'false');
            };
        }

        var credSaveBtn = document.getElementById('sls-cfg-cs-cred-save-btn');
        if (credSaveBtn) {
            credSaveBtn.onclick = function (e) {
                e.preventDefault();
                var curProv = providerSel ? providerSel.value : ((cfg.CloudSaves && cfg.CloudSaves.Provider) || 'r2');
                if (curProv !== 'r2' && curProv !== 's3') return;

                var cAcc = document.getElementById('sls-cfg-cs-cred-account-id');
                var cBkt = document.getElementById('sls-cfg-cs-cred-bucket');
                var cAk = document.getElementById('sls-cfg-cs-cred-access-key');
                var cSk = document.getElementById('sls-cfg-cs-cred-secret-key');
                var cReg = document.getElementById('sls-cfg-cs-cred-region');
                var cEnd = document.getElementById('sls-cfg-cs-cred-endpoint');
                var cPfx = document.getElementById('sls-cfg-cs-cred-prefix');

                var credPayload = {
                    provider: curProv,
                    accountId: (cAcc && curProv === 'r2') ? cAcc.value.trim() : '',
                    bucket: cBkt ? cBkt.value.trim() : '',
                    accessKeyId: cAk ? cAk.value.trim() : '',
                    secretAccessKey: cSk ? cSk.value.trim() : '',
                    region: cReg ? cReg.value.trim() : '',
                    endpoint: cEnd ? cEnd.value.trim() : '',
                    keyPrefix: cPfx ? cPfx.value.trim() : ''
                };

                credSaveBtn.disabled = true;
                credSaveBtn.textContent = 'Saving & testing...';

                var saveProm = (window.NaviCloudManager && window.NaviCloudManager.saveProviderCredentials)
                    ? window.NaviCloudManager.saveProviderCredentials(credPayload)
                    : fetch('http://127.0.0.1:9001/cloud/provider/credentials', {
                        method: 'POST',
                        headers: { 'Content-Type': 'text/plain' },
                        body: 'X-Navi-Auth: ' + (window.__naviAuthToken || '') + '\r\n' + JSON.stringify(credPayload)
                    }).then(function (r) { return r.json(); });

                saveProm.then(function (res) {
                    var curBtn = document.getElementById('sls-cfg-cs-cred-save-btn') || credSaveBtn;
                    if (curBtn) {
                        curBtn.disabled = false;
                        curBtn.textContent = 'Save & Test Connection';
                    }
                    if (res && res.success !== false && !res.error) {
                        if (res.authenticated) {
                            showStatusBanner(res.message || ('Saved ' + providerLabel(curProv) + ' credentials and verified connection.'), '#2ecc71');
                        } else {
                            showStatusBanner(res.message || ('Saved ' + providerLabel(curProv) + ' credentials. Connection test reported unauthenticated.'), '#f59e0b');
                        }
                        refreshCloudSavesUI();
                    } else {
                        showStatusBanner('Could not save credentials: ' + ((res && (res.error || res.message)) || 'Unknown error'), '#ff4d4d');
                    }
                }).catch(function (err) {
                    var curBtn = document.getElementById('sls-cfg-cs-cred-save-btn') || credSaveBtn;
                    if (curBtn) {
                        curBtn.disabled = false;
                        curBtn.textContent = 'Save & Test Connection';
                    }
                    showStatusBanner('Error saving credentials: ' + (err.message || 'Unknown error'), '#ff4d4d');
                });
            };
        }

        // Stats Sync sub-toggle dimming
        var statsSyncMasterToggle = document.getElementById('sls-cfg-StatsSync-Enabled');
        var statsSyncAchToggle = document.getElementById('sls-cfg-StatsSync-Achievements');
        var statsSyncPlayToggle = document.getElementById('sls-cfg-StatsSync-Playtime');
        var statsSyncSubrowsEl = document.getElementById('sls-cfg-StatsSync-subrows');

        function syncStatsSyncSubrowsUI() {
            var isStatsOn = !!(statsSyncMasterToggle && statsSyncMasterToggle.checked);
            if (statsSyncSubrowsEl) {
                statsSyncSubrowsEl.style.opacity = isStatsOn ? '1' : '0.5';
            }
            if (statsSyncAchToggle) statsSyncAchToggle.disabled = !isStatsOn;
            if (statsSyncPlayToggle) statsSyncPlayToggle.disabled = !isStatsOn;
        }
        if (statsSyncMasterToggle) {
            statsSyncMasterToggle.addEventListener('change', syncStatsSyncSubrowsUI);
        }
        syncStatsSyncSubrowsUI();

        // Migrate Cloud Saves handlers
        var migrateSrcSel = document.getElementById('sls-cfg-cs-migrate-src');
        var migrateDstSel = document.getElementById('sls-cfg-cs-migrate-dst');
        var migrateScanBtn = document.getElementById('sls-cfg-cs-migrate-scan-btn');
        var migrateStartBtn = document.getElementById('sls-cfg-cs-migrate-start-btn');
        var migrateStatusEl = document.getElementById('sls-cfg-cs-migrate-status');

        if (migrateSrcSel && migrateDstSel) {
            migrateSrcSel.onchange = function () {
                if (migrateSrcSel.value === migrateDstSel.value) {
                    migrateDstSel.value = (migrateSrcSel.value === 'local-only') ? 'gdrive' : 'local-only';
                }
            };
        }

        function formatMigrationBytes(b) {
            if (window.NaviCloudManager && window.NaviCloudManager.formatBytes) {
                return window.NaviCloudManager.formatBytes(b);
            }
            var n = Number(b) || 0;
            return (n / (1024 * 1024)).toFixed(2).replace(/\.00$/, '') + ' MB';
        }

        if (migrateScanBtn) {
            migrateScanBtn.onclick = function (e) {
                e.preventDefault();
                var srcProv = migrateSrcSel ? migrateSrcSel.value : 'local-only';
                migrateScanBtn.disabled = true;
                migrateScanBtn.textContent = 'Scanning...';
                if (migrateStatusEl) {
                    migrateStatusEl.style.display = 'block';
                    migrateStatusEl.style.borderColor = 'rgba(56, 189, 248, 0.35)';
                    migrateStatusEl.style.color = '#cbd5e1';
                    migrateStatusEl.textContent = 'Scanning ' + providerLabel(srcProv) + '...';
                }

                var scanProm = (window.NaviCloudManager && window.NaviCloudManager.scanMigrationSource)
                    ? window.NaviCloudManager.scanMigrationSource(srcProv)
                    : fetch('http://127.0.0.1:9001/cloud/migrate/scan?provider=' + encodeURIComponent(srcProv)).then(function (r) { return r.json(); });

                scanProm.then(function (data) {
                    migrateScanBtn.disabled = false;
                    migrateScanBtn.textContent = 'Scan Source';
                    if (!migrateStatusEl) return;
                    if (data && data.success !== false && !data.error) {
                        var gCount = Number(data.appCount) || (Array.isArray(data.apps) ? data.apps.length : 0);
                        var fCount = Number(data.fileCount) || 0;
                        var mbStr = formatMigrationBytes(Number(data.totalSizeBytes) || 0);
                        migrateStatusEl.style.borderColor = 'rgba(16, 185, 129, 0.35)';
                        migrateStatusEl.style.color = '#e2e8f0';
                        migrateStatusEl.textContent = gCount + ' games · ' + fCount + ' files · ' + mbStr + ' ready to migrate';
                    } else {
                        migrateStatusEl.style.borderColor = 'rgba(239, 68, 68, 0.4)';
                        migrateStatusEl.style.color = '#fca5a5';
                        migrateStatusEl.textContent = 'Scan error: ' + ((data && (data.error || data.message)) || 'Could not scan source provider');
                    }
                }).catch(function (err) {
                    migrateScanBtn.disabled = false;
                    migrateScanBtn.textContent = 'Scan Source';
                    if (migrateStatusEl) {
                        migrateStatusEl.style.display = 'block';
                        migrateStatusEl.style.borderColor = 'rgba(239, 68, 68, 0.4)';
                        migrateStatusEl.style.color = '#fca5a5';
                        migrateStatusEl.textContent = 'Scan failed: ' + (err.message || 'Unknown error');
                    }
                });
            };
        }

        if (migrateStartBtn) {
            migrateStartBtn.onclick = function (e) {
                e.preventDefault();
                var srcProv = migrateSrcSel ? migrateSrcSel.value : 'local-only';
                var dstProv = migrateDstSel ? migrateDstSel.value : 'gdrive';
                if (srcProv === dstProv) {
                    if (migrateStatusEl) {
                        migrateStatusEl.style.display = 'block';
                        migrateStatusEl.style.borderColor = 'rgba(245, 158, 11, 0.4)';
                        migrateStatusEl.style.color = '#fde68a';
                        migrateStatusEl.textContent = 'Source and destination providers must be different.';
                    }
                    showStatusBanner('Select different source and destination providers to migrate.', '#f59e0b');
                    return;
                }

                migrateStartBtn.disabled = true;
                migrateStartBtn.textContent = 'Migrating...';
                if (migrateStatusEl) {
                    migrateStatusEl.style.display = 'block';
                    migrateStatusEl.style.borderColor = 'rgba(56, 189, 248, 0.35)';
                    migrateStatusEl.style.color = '#cbd5e1';
                    migrateStatusEl.textContent = 'Migrating cloud saves from ' + providerLabel(srcProv) + ' to ' + providerLabel(dstProv) + '...';
                }

                var migProm = (window.NaviCloudManager && window.NaviCloudManager.migrateProviderData)
                    ? window.NaviCloudManager.migrateProviderData(srcProv, dstProv, true)
                    : fetch('http://127.0.0.1:9001/cloud/migrate', {
                        method: 'POST',
                        headers: { 'Content-Type': 'text/plain' },
                        body: 'X-Navi-Auth: ' + (window.__naviAuthToken || '') + '\r\n' + JSON.stringify({
                            sourceProvider: srcProv,
                            targetProvider: dstProv,
                            switchActiveProvider: true
                        })
                    }).then(function (r) { return r.json(); });

                migProm.then(function (res) {
                    migrateStartBtn.disabled = false;
                    migrateStartBtn.textContent = 'Start Migration';
                    if (res && res.success !== false && !res.error) {
                        var nMig = Number(res.migrated) || 0;
                        var nSkip = Number(res.skipped) || 0;
                        var nFail = Number(res.failed) || 0;
                        if (migrateStatusEl) {
                            migrateStatusEl.style.borderColor = 'rgba(16, 185, 129, 0.4)';
                            migrateStatusEl.style.color = '#4ade80';
                            migrateStatusEl.textContent = 'Migrated ' + nMig + ' files (' + nSkip + ' skipped, ' + nFail + ' failed)';
                        }
                        if (!cfg.CloudSaves) cfg.CloudSaves = {};
                        cfg.CloudSaves.Provider = dstProv;
                        if (providerSel) providerSel.value = dstProv;
                        initialFormState = serializeCurrentForm();
                        markClean(true);
                        refreshCloudSavesUI();
                        showStatusBanner('Migrated ' + nMig + ' files to ' + providerLabel(dstProv) + '.', '#2ecc71');
                    } else {
                        if (migrateStatusEl) {
                            migrateStatusEl.style.borderColor = 'rgba(239, 68, 68, 0.4)';
                            migrateStatusEl.style.color = '#fca5a5';
                            migrateStatusEl.textContent = 'Migration failed: ' + ((res && (res.error || res.message)) || 'Unknown error');
                        }
                        showStatusBanner('Migration failed: ' + ((res && (res.error || res.message)) || 'Unknown error'), '#ff4d4d');
                    }
                }).catch(function (err) {
                    migrateStartBtn.disabled = false;
                    migrateStartBtn.textContent = 'Start Migration';
                    if (migrateStatusEl) {
                        migrateStatusEl.style.display = 'block';
                        migrateStatusEl.style.borderColor = 'rgba(239, 68, 68, 0.4)';
                        migrateStatusEl.style.color = '#fca5a5';
                        migrateStatusEl.textContent = 'Migration error: ' + (err.message || 'Unknown error');
                    }
                    showStatusBanner('Migration error: ' + (err.message || 'Unknown error'), '#ff4d4d');
                });
            };
        }

        // Backups search filter
        var backupsSearchEl = document.getElementById('sls-cfg-cs-backups-search');
        if (backupsSearchEl) {
            backupsSearchEl.oninput = function () {
                var q = (backupsSearchEl.value || '').trim().toLowerCase();
                var bRows = container.querySelectorAll('.navi-cloud-backup-row');
                bRows.forEach(function (r) {
                    var txt = (r.getAttribute('data-search') || r.textContent || '').toLowerCase();
                    r.style.display = (!q || txt.indexOf(q) !== -1) ? '' : 'none';
                });
            };
        }

        var addAppInput = document.getElementById('sls-cfg-add-cloud-app-input');
        var searchClearBtn = document.getElementById('sls-cfg-cs-search-clear');

        if (addAppInput) {
            addAppInput.oninput = function () {
                var rawVal = addAppInput.value;
                var q = rawVal.trim().toLowerCase();
                if (searchClearBtn) {
                    searchClearBtn.style.display = q ? 'inline-block' : 'none';
                }

                // Live table filtering for games and files
                var gameRows = container.querySelectorAll('.navi-cloud-game-row');
                gameRows.forEach(function (r) {
                    var txt = (r.getAttribute('data-search') || r.textContent || '').toLowerCase();
                    r.style.display = (!q || txt.indexOf(q) !== -1) ? '' : 'none';
                });

                var fileRows = container.querySelectorAll('.navi-cloud-file-row');
                fileRows.forEach(function (r) {
                    var txt = (r.getAttribute('data-search') || r.textContent || '').toLowerCase();
                    r.style.display = (!q || txt.indexOf(q) !== -1) ? '' : 'none';
                });

                if (q) {
                    var groupCards = container.querySelectorAll('.sls-cloud-group-card');
                    groupCards.forEach(function (card) {
                        var cardRows = card.querySelectorAll('.navi-cloud-file-row');
                        var hasVisible = false;
                        for (var i = 0; i < cardRows.length; i++) {
                            if (cardRows[i].style.display !== 'none') {
                                hasVisible = true;
                                break;
                            }
                        }
                        var body = card.querySelector('.sls-cloud-group-body');
                        if (body && hasVisible) {
                            body.style.display = 'block';
                            var chevron = card.querySelector('.sls-cloud-group-chevron');
                            if (chevron) chevron.textContent = '▼';
                            var header = card.querySelector('.sls-cloud-group-header');
                            if (header) header.setAttribute('aria-expanded', 'true');
                        }
                    });
                }
            };

            addAppInput.onkeydown = function (e) {
                if (e.key === 'Enter') {
                    e.preventDefault();
                } else if (e.key === 'Escape' && addAppInput.value) {
                    e.preventDefault();
                    addAppInput.value = '';
                    addAppInput.dispatchEvent(new Event('input'));
                }
            };
        }

        if (searchClearBtn && addAppInput) {
            searchClearBtn.onclick = function (e) {
                e.preventDefault();
                addAppInput.value = '';
                addAppInput.dispatchEvent(new Event('input'));
                addAppInput.focus();
            };
        }

        var linkToCloudSaves = document.getElementById('sls-cfg-link-to-cloudsaves');
        if (linkToCloudSaves) {
            linkToCloudSaves.onclick = function (e) {
                e.preventDefault();
                switchTab('sls-cfg-sec-cloudsaves');
            };
        }

        var linkToGeneral = document.getElementById('sls-cfg-link-to-general');
        if (linkToGeneral) {
            linkToGeneral.onclick = function (e) {
                e.preventDefault();
                switchTab('sls-cfg-sec-general');
            };
        }

        // 6. Bitmask sync and 1-Click Presets
        var logFlagInputs = container.querySelectorAll('.sls-cfg-log-flag');
        var hexInput = document.getElementById('sls-cfg-loglevel-hex');
        var presetChips = container.querySelectorAll('.sls-preset-chip');

        function updateActivePresetChip(mask) {
            presetChips.forEach(function (chip) {
                var p = parseInt(chip.getAttribute('data-preset'), 10);
                if (p === mask) {
                    chip.classList.add('active');
                } else {
                    chip.classList.remove('active');
                }
            });
        }

        presetChips.forEach(function (chip) {
            chip.onclick = function () {
                var pVal = parseInt(chip.getAttribute('data-preset'), 10);
                currentLogLevel = pVal;
                logFlagInputs.forEach(function (fi) {
                    var fl = parseInt(fi.getAttribute('data-flag'), 10);
                    fi.checked = (pVal & fl) !== 0;
                });
                if (hexInput) {
                    hexInput.value = '0x' + pVal.toString(16).toUpperCase() + ' (' + pVal + ')';
                }
                updateActivePresetChip(pVal);
                checkDirty();
            };
        });

        logFlagInputs.forEach(function (inp) {
            inp.onchange = function () {
                var newMask = 0;
                logFlagInputs.forEach(function (fi) {
                    if (fi.checked) {
                        newMask |= parseInt(fi.getAttribute('data-flag'), 10);
                    }
                });
                currentLogLevel = newMask;
                if (hexInput) {
                    hexInput.value = '0x' + newMask.toString(16).toUpperCase() + ' (' + newMask + ')';
                }
                updateActivePresetChip(newMask);
                checkDirty();
            };
        });

        if (hexInput) {
            hexInput.onchange = function () {
                var raw = hexInput.value.trim().split(' ')[0];
                var parsed = parseInt(raw, (raw.startsWith('0x') || raw.startsWith('0X')) ? 16 : 10);
                if (!isNaN(parsed) && parsed >= 0) {
                    currentLogLevel = parsed;
                    logFlagInputs.forEach(function (fi) {
                        var fl = parseInt(fi.getAttribute('data-flag'), 10);
                        fi.checked = (parsed & fl) !== 0;
                    });
                    hexInput.value = '0x' + parsed.toString(16).toUpperCase() + ' (' + parsed + ')';
                    updateActivePresetChip(parsed);
                    checkDirty();
                }
            };
        }

        // ---------------------------------------------------------------------
        // Cloud saves master toggle & cross-links
        // ---------------------------------------------------------------------
        var masterToggle = document.getElementById('sls-cfg-cs-master-toggle');
        var masterBtnEl = document.getElementById('sls-cfg-cs-master-btn');
        if (masterToggle && masterBtnEl) {
            masterToggle.onchange = function () {
                masterBtnEl.click();
            };
        }

        var linkToCloudSaves = document.getElementById('sls-cfg-link-to-cloudsaves');
        if (linkToCloudSaves) {
            linkToCloudSaves.onclick = function (e) {
                e.preventDefault();
                switchTab('sls-cfg-sec-cloudsaves');
            };
        }

        var linkToGeneral = document.getElementById('sls-cfg-link-to-general');
        if (linkToGeneral) {
            linkToGeneral.onclick = function (e) {
                e.preventDefault();
                switchTab('sls-cfg-sec-general');
            };
        }

        var disableCloudToggle = document.getElementById('sls-cfg-DisableCloud');
        function updateCloudConflictNote() {
            var note = document.getElementById('sls-cfg-steam-cloud-note');
            if (!note) return;
            var cloudOn = !(cfg.CloudSaves && cfg.CloudSaves.Enabled === false);
            var steamOn = !(disableCloudToggle && disableCloudToggle.checked);
            note.style.display = (cloudOn && steamOn) ? 'flex' : 'none';
        }
        if (disableCloudToggle) {
            disableCloudToggle.addEventListener('change', updateCloudConflictNote);
        }
        updateCloudConflictNote();

        // ---------------------------------------------------------------------
        // Log detail bitmask + presets
        // ---------------------------------------------------------------------
        var logFlagInputs = container.querySelectorAll('.sls-cfg-log-flag');
        var hexInput = document.getElementById('sls-cfg-loglevel-hex');
        var presetChips = container.querySelectorAll('.sls-preset-chip');

        function updateActivePresetChip(mask) {
            presetChips.forEach(function (chip) {
                var p = parseInt(chip.getAttribute('data-preset'), 10);
                if (p === mask) {
                    chip.classList.add('active');
                } else {
                    chip.classList.remove('active');
                }
            });
        }

        presetChips.forEach(function (chip) {
            chip.onclick = function () {
                var pVal = parseInt(chip.getAttribute('data-preset'), 10);
                currentLogLevel = pVal;
                logFlagInputs.forEach(function (fi) {
                    var fl = parseInt(fi.getAttribute('data-flag'), 10);
                    fi.checked = (pVal & fl) !== 0;
                });
                if (hexInput) {
                    hexInput.value = '0x' + pVal.toString(16).toUpperCase() + ' (' + pVal + ')';
                }
                updateActivePresetChip(pVal);
                checkDirty();
            };
        });

        logFlagInputs.forEach(function (inp) {
            inp.onchange = function () {
                var newMask = 0;
                logFlagInputs.forEach(function (fi) {
                    if (fi.checked) {
                        newMask |= parseInt(fi.getAttribute('data-flag'), 10);
                    }
                });
                currentLogLevel = newMask;
                if (hexInput) {
                    hexInput.value = '0x' + newMask.toString(16).toUpperCase() + ' (' + newMask + ')';
                }
                updateActivePresetChip(newMask);
                checkDirty();
            };
        });

        if (hexInput) {
            hexInput.onchange = function () {
                var raw = hexInput.value.trim().split(' ')[0];
                var parsed = parseInt(raw, (raw.startsWith('0x') || raw.startsWith('0X')) ? 16 : 10);
                if (!isNaN(parsed) && parsed >= 0) {
                    currentLogLevel = parsed;
                    logFlagInputs.forEach(function (fi) {
                        var fl = parseInt(fi.getAttribute('data-flag'), 10);
                        fi.checked = (parsed & fl) !== 0;
                    });
                    hexInput.value = '0x' + parsed.toString(16).toUpperCase() + ' (' + parsed + ')';
                    updateActivePresetChip(parsed);
                    checkDirty();
                }
            };
        }

        // ---------------------------------------------------------------------
        // Provider keys: reveal + clear + live verification
        // ---------------------------------------------------------------------
        ['RyuuKey', 'DepotBoxKey', 'HubcapKey'].forEach(function (k) {
            var btn = document.getElementById('sls-cfg-toggle-' + k);
            var inp = document.getElementById('sls-cfg-' + k);
            var clearBtn = document.getElementById('sls-cfg-clear-' + k);
            if (btn && inp) {
                btn.onclick = function (e) {
                    e.preventDefault();
                    if (inp.type === 'password') {
                        inp.type = 'text';
                        btn.textContent = 'Hide';
                    } else {
                        inp.type = 'password';
                        btn.textContent = 'Show';
                    }
                };
            }
            if (clearBtn && inp) {
                clearBtn.onclick = function (e) {
                    e.preventDefault();
                    inp.value = '';
                    checkDirty();
                };
            }
        });

        function verifyProviderKey(inp) {
            if (!inp) return;
            var prov = inp.getAttribute('data-provider') || '';
            var bannerId = inp.getAttribute('data-banner') || '';
            var banner = bannerId ? document.getElementById(bannerId) : null;
            var val = (inp.value || '').trim();
            if (!val) {
                if (banner) banner.style.display = 'none';
                return;
            }
            fetch('http://127.0.0.1:9001/api/keys/verify', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ provider: prov, key: val })
            }).then(function (r) {
                return r.json();
            }).then(function (res) {
                if (banner) {
                    if (res && res.valid === false) {
                        banner.style.display = 'flex';
                    } else {
                        banner.style.display = 'none';
                    }
                }
            }).catch(function () {});
        }

        var keyInputs = container.querySelectorAll('.sls-cfg-provider-key');
        keyInputs.forEach(function (inp) {
            inp.addEventListener('blur', function () {
                verifyProviderKey(inp);
            });
        });

        var clearBannerBtns = container.querySelectorAll('.sls-cfg-clear-banner-btn');
        clearBannerBtns.forEach(function (btn) {
            btn.onclick = function (e) {
                e.preventDefault();
                var targetInputId = btn.getAttribute('data-input');
                var targetBannerId = btn.getAttribute('data-banner');
                var inp = document.getElementById(targetInputId);
                var banner = targetBannerId ? document.getElementById(targetBannerId) : null;
                if (inp) {
                    inp.value = '';
                    checkDirty();
                }
                if (banner) {
                    banner.style.display = 'none';
                }
            };
        });

        var clearNameBtn = document.getElementById('sls-cfg-clear-FakeName');
        if (clearNameBtn) {
            clearNameBtn.onclick = function (e) {
                e.preventDefault();
                var inp = document.getElementById('sls-cfg-FakeName');
                if (inp) {
                    inp.value = '';
                    checkDirty();
                    inp.focus();
                }
            };
        }

        var addAppBtn = document.getElementById('sls-cfg-add-app-btn');
        if (addAppBtn) {
            addAppBtn.onclick = function (e) {
                e.preventDefault();
                slsConfirmDialog({
                    title: 'Add unlocked game',
                    body: 'Enter the AppID of the game to unlock (adding a game also includes its DLC):',
                    confirmLabel: 'Add game',
                    cancelLabel: 'Cancel',
                    input: { label: 'AppID', placeholder: '440' },
                    onConfirm: function (val) {
                        var appIdsEl = document.getElementById('sls-cfg-AppIds');
                        if (appIdsEl && val && val.trim()) {
                            var cur = appIdsEl.value.trim();
                            appIdsEl.value = cur ? (cur + '\n' + val.trim()) : val.trim();
                            checkDirty();
                        }
                    }
                });
            };
        }

        // ---------------------------------------------------------------------
        // Form serialization + dirty tracking
        // ---------------------------------------------------------------------
        var saveBtn = document.getElementById('sls-cfg-save-btn');
        var discardBtn = document.getElementById('sls-cfg-discard-btn');

        function serializeCurrentForm() {
            function getVal(id) { var el = document.getElementById(id); return el ? el.value : ''; }
            function getCheckbox(id) { var el = document.getElementById(id); return el ? el.checked : false; }
            var state = {
                cloudAutoBackup: getCheckbox('sls-cfg-CloudAutoBackup'),
                cloudRetention: getVal('sls-cfg-CloudRetention'),
                cloudProvider: getVal('sls-cfg-cs-provider'),
                cloudNotificationsEnabled: getCheckbox('sls-cfg-CloudSaves-NotificationsEnabled'),
                prefixSyncEnabled: getCheckbox('sls-cfg-PrefixSync-Enabled'),
                prefixSyncStartup: getCheckbox('sls-cfg-PrefixSync-Startup'),
                prefixSyncLaunch: getCheckbox('sls-cfg-PrefixSync-Launch'),
                prefixSyncExit: getCheckbox('sls-cfg-PrefixSync-Exit'),
                prefixSyncMaxMb: getVal('sls-cfg-PrefixSync-MaxMb'),
                statsSyncEnabled: getCheckbox('sls-cfg-StatsSync-Enabled'),
                statsSyncAchievements: getCheckbox('sls-cfg-StatsSync-Achievements'),
                statsSyncPlaytime: getCheckbox('sls-cfg-StatsSync-Playtime'),
                logLevel: currentLogLevel,
                ryuuKey: getVal('sls-cfg-RyuuKey'),
                depotBoxKey: getVal('sls-cfg-DepotBoxKey'),
                hubcapKey: getVal('sls-cfg-HubcapKey'),
                disableFamShare: getCheckbox('sls-cfg-DisableFamilyShareLock'),
                useWhitelist: getCheckbox('sls-cfg-UseWhitelist'),
                disableCloud: getCheckbox('sls-cfg-DisableCloud'),
                disableUpdates: getCheckbox('sls-cfg-DisableUpdates'),
                safeMode: getCheckbox('sls-cfg-SafeMode'),
                warnHash: getCheckbox('sls-cfg-WarnHashMissmatch'),
                notifyInit: getCheckbox('sls-cfg-NotifyInit'),
                api: getCheckbox('sls-cfg-API'),
                maxSchema: getVal('sls-cfg-MaxSchemaTries'),
                fakeName: getVal('sls-cfg-FakeName'),
                fakeEmail: getVal('sls-cfg-FakeEmail'),
                fakeWallet: getVal('sls-cfg-FakeWalletBalance'),
                idleAppId: getVal('sls-cfg-IdleStatus-AppId'),
                idleTitle: getVal('sls-cfg-IdleStatus-Title'),
                unownedAppId: getVal('sls-cfg-UnownedStatus-AppId'),
                unownedTitle: getVal('sls-cfg-UnownedStatus-Title'),
                appIds: getVal('sls-cfg-AppIds'),
                additionalApps: getVal('sls-cfg-AdditionalApps'),
                fakeOffline: getVal('sls-cfg-FakeOffline'),
                depotBlacklist: getVal('sls-cfg-DepotBlacklist'),
                fakeAppIds: getVal('sls-cfg-FakeAppIds'),
                manifestIds: getVal('sls-cfg-ManifestIds'),
                appTokens: getVal('sls-cfg-AppTokens'),
                cdKeys: getVal('sls-cfg-CDKeys'),
                gameTitles: getVal('sls-cfg-GameTitles'),
                subTimestamps: getVal('sls-cfg-SubscriptionTimestamps'),
                steamIdOverride: getVal('sls-cfg-SteamIdOverride'),
                dlcYaml: getVal('sls-cfg-DlcDataYaml'),
                denuvoYaml: getVal('sls-cfg-DenuvoGamesYaml'),
                dumpInterfaces: getCheckbox('sls-cfg-DumpClientInterfaces'),
                extendedLogging: getCheckbox('sls-cfg-ExtendedLogging')
            };
            return JSON.stringify(state);
        }

        var isDirty = false;
        var initialFormState = '';

        function countChangedKeys() {
            if (!initialFormState) return 0;
            var current;
            var initial;
            try {
                current = JSON.parse(serializeCurrentForm());
                initial = JSON.parse(initialFormState);
            } catch (e) {
                return 0;
            }
            var keys = {};
            for (var k in current) { if (Object.prototype.hasOwnProperty.call(current, k)) keys[k] = true; }
            for (var k2 in initial) { if (Object.prototype.hasOwnProperty.call(initial, k2)) keys[k2] = true; }
            var changed = 0;
            for (var key in keys) {
                if (JSON.stringify(current[key]) !== JSON.stringify(initial[key])) changed++;
            }
            return changed;
        }

        function setDirtyChrome(isDirtyNow, changed) {
            var pill = document.getElementById('sls-cfg-dirty-pill');
            if (pill) {
                if (isDirtyNow) {
                    pill.innerHTML = '<span style="color: #f1f5f9; font-size: 12px; font-weight: 500;">' + changed + (changed === 1 ? ' unsaved change' : ' unsaved changes') + '</span><span style="background: #f59e0b; color: #1e1e1e; font-weight: 800; font-size: 10px; width: 16px; height: 16px; border-radius: 50%; display: inline-flex; align-items: center; justify-content: center; margin-left: 6px;">!</span>';
                    pill.style.display = 'flex';
                    pill.style.justifyContent = 'space-between';
                    pill.style.alignItems = 'center';
                } else {
                    pill.innerHTML = '<span>All changes saved</span>';
                    pill.style.display = 'block';
                    pill.style.textAlign = 'center';
                    pill.style.color = '#2ecc71';
                    pill.style.fontSize = '11px';
                }
            }
            if (discardBtn) discardBtn.style.display = isDirtyNow ? 'inline-block' : 'none';
            if (saveBtn) {
                saveBtn.disabled = !isDirtyNow;
                saveBtn.style.background = isDirtyNow ? '#1a9fff' : 'rgba(255, 255, 255, 0.08)';
                saveBtn.style.color = isDirtyNow ? '#ffffff' : '#64748b';
            }
            window.__naviConfigDirty = isDirtyNow;
        }

        function checkDirty() {
            var changed = countChangedKeys();
            isDirty = changed > 0;
            setDirtyChrome(isDirty, changed);
        }

        function markClean(preserveState) {
            isDirty = false;
            if (!preserveState) {
                initialFormState = serializeCurrentForm();
            }
            setDirtyChrome(false, 0);
        }

        container.addEventListener('input', checkDirty);
        container.addEventListener('change', checkDirty);

        // ---------------------------------------------------------------------
        // Save pipeline
        // ---------------------------------------------------------------------
        function collectAndSaveConfig(callback, silent) {
            if (!saveBtn) return;
            saveBtn.disabled = true;
            saveBtn.textContent = 'Saving…';
            var savingPill = document.getElementById('sls-cfg-dirty-pill');
            if (savingPill) {
                savingPill.textContent = 'Saving…';
                savingPill.style.color = '#66c0f4';
            }
            hideStatusBanner();

            function getVal(id) { var el = document.getElementById(id); return el ? el.value : ''; }
            function getInt(id, def) { var v = parseInt(getVal(id), 10); return isNaN(v) ? (def || 0) : v; }
            function getCheckbox(id) { var el = document.getElementById(id); return el ? el.checked : false; }
            function parseUintArray(id) {
                var raw = getVal(id);
                if (!raw) return [];
                var tokens = raw.split(/[\s,]+/);
                var res = [];
                tokens.forEach(function (tok) {
                    var t = tok.trim();
                    if (!t) return;
                    var n = parseInt(t, 10);
                    if (!isNaN(n) && n >= 0) res.push(n);
                });
                return res;
            }
            function parseKvMap(id) {
                var raw = getVal(id);
                if (!raw) return {};
                var lines = raw.split('\n');
                var map = {};
                lines.forEach(function (line) {
                    var l = line.trim();
                    if (!l || l.indexOf('#') === 0) return;
                    var colonIdx = l.indexOf(':');
                    if (colonIdx === -1) colonIdx = l.indexOf('=');
                    if (colonIdx !== -1) {
                        var k = l.substring(0, colonIdx).trim();
                        var v = l.substring(colonIdx + 1).trim();
                        if ((v.startsWith('"') && v.endsWith('"')) || (v.startsWith("'") && v.endsWith("'"))) {
                            v = v.substring(1, v.length - 1);
                        }
                        if (k) map[k] = v;
                    }
                });
                return map;
            }

            var payload = {
                DisableFamilyShareLock: getCheckbox('sls-cfg-DisableFamilyShareLock'),
                UseWhitelist: getCheckbox('sls-cfg-UseWhitelist'),
                SafeMode: getCheckbox('sls-cfg-SafeMode'),
                WarnHashMissmatch: getCheckbox('sls-cfg-WarnHashMissmatch'),
                NotifyInit: getCheckbox('sls-cfg-NotifyInit'),
                API: getCheckbox('sls-cfg-API'),
                DisableCloud: getCheckbox('sls-cfg-DisableCloud'),
                DisableUpdates: getCheckbox('sls-cfg-DisableUpdates'),
                DumpClientInterfaces: getCheckbox('sls-cfg-DumpClientInterfaces'),
                ExtendedLogging: getCheckbox('sls-cfg-ExtendedLogging'),
                MaxSchemaTries: getInt('sls-cfg-MaxSchemaTries', 10),
                FakeWalletBalance: getInt('sls-cfg-FakeWalletBalance', 0),
                LogLevels: currentLogLevel,
                FakeName: getVal('sls-cfg-FakeName'),
                FakeEmail: getVal('sls-cfg-FakeEmail'),
                RyuuKey: getVal('sls-cfg-RyuuKey'),
                DepotBoxKey: getVal('sls-cfg-DepotBoxKey'),
                HubcapKey: getVal('sls-cfg-HubcapKey'),
                IdleStatus: {
                    AppId: getInt('sls-cfg-IdleStatus-AppId', 0),
                    Title: getVal('sls-cfg-IdleStatus-Title')
                },
                UnownedStatus: {
                    AppId: getInt('sls-cfg-UnownedStatus-AppId', 0),
                    Title: getVal('sls-cfg-UnownedStatus-Title')
                },
                AppIds: parseUintArray('sls-cfg-AppIds'),
                AdditionalApps: parseUintArray('sls-cfg-AdditionalApps'),
                FakeOffline: parseUintArray('sls-cfg-FakeOffline'),
                DepotBlacklist: parseUintArray('sls-cfg-DepotBlacklist'),
                FakeAppIds: parseKvMap('sls-cfg-FakeAppIds'),
                ManifestIds: parseKvMap('sls-cfg-ManifestIds'),
                AppTokens: parseKvMap('sls-cfg-AppTokens'),
                CDKeys: parseKvMap('sls-cfg-CDKeys'),
                GameTitles: parseKvMap('sls-cfg-GameTitles'),
                SubscriptionTimestamps: parseKvMap('sls-cfg-SubscriptionTimestamps'),
                SteamIdOverride: parseKvMap('sls-cfg-SteamIdOverride'),
                DlcDataYaml: getVal('sls-cfg-DlcDataYaml'),
                DenuvoGamesYaml: getVal('sls-cfg-DenuvoGamesYaml'),
                CloudSaves: {
                    Enabled: (cfg.CloudSaves && typeof cfg.CloudSaves.Enabled === 'boolean') ? cfg.CloudSaves.Enabled : true,
                    Provider: getVal('sls-cfg-cs-provider') || (cfg.CloudSaves && cfg.CloudSaves.Provider) || 'local-only',
                    NotificationsEnabled: getCheckbox('sls-cfg-CloudSaves-NotificationsEnabled'),
                    AutoBackupBeforeReset: getCheckbox('sls-cfg-CloudAutoBackup'),
                    BackupRetentionCount: getInt('sls-cfg-CloudRetention', 5),
                    PrefixSync: {
                        Enabled: getCheckbox('sls-cfg-PrefixSync-Enabled'),
                        SyncAtStartup: getCheckbox('sls-cfg-PrefixSync-Startup'),
                        SyncOnLaunch: getCheckbox('sls-cfg-PrefixSync-Launch'),
                        SyncOnExit: getCheckbox('sls-cfg-PrefixSync-Exit'),
                        MaxFileMb: getInt('sls-cfg-PrefixSync-MaxMb', 64)
                    },
                    StatsSync: {
                        Enabled: getCheckbox('sls-cfg-StatsSync-Enabled'),
                        SyncAchievements: getCheckbox('sls-cfg-StatsSync-Achievements'),
                        SyncPlaytime: getCheckbox('sls-cfg-StatsSync-Playtime')
                    },
                    ManagedApps: (cfg.CloudSaves && cfg.CloudSaves.ManagedApps) || []
                }
            };

            fetch('http://127.0.0.1:9001/config/full', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json', 'Connection': 'close' },
                body: JSON.stringify(payload)
            }).then(function (r) {
                return r.json();
            }).then(function (res) {
                saveBtn.textContent = 'Save changes';
                if (res && res.success) {
                    cfg = JSON.parse(JSON.stringify(payload));
                    initialFormState = serializeCurrentForm();
                    markClean(true);
                    refreshCloudSavesUI();
                    if (!silent) {
                        showStatusBanner('Saved. Your new settings are active.', '#2ecc71');
                    }
                    if (typeof callback === 'function') callback(null, res);
                } else {
                    checkDirty();
                    if (!silent) {
                        showStatusBanner('We couldn\'t save your changes. Nothing was lost — they\'re still on this page. ' + ((res && res.message) || 'Unknown error'), '#ff4d4d');
                    }
                    if (typeof callback === 'function') callback(new Error((res && res.message) || 'Unknown error'));
                }
            }).catch(function (err) {
                saveBtn.textContent = 'Save changes';
                checkDirty();
                if (!silent) {
                    showStatusBanner('Connection error: ' + err.message, '#ff4d4d');
                }
                if (typeof callback === 'function') callback(err);
            });
        }

        if (saveBtn) {
            saveBtn.onclick = function (e) {
                e.preventDefault();
                collectAndSaveConfig();
            };
        }

        var restartBtn = document.getElementById('sls-cfg-restart-btn');
        if (restartBtn) {
            restartBtn.onclick = function (e) {
                e.preventDefault();
                slsConfirmDialog({
                    title: 'Restart Steam now?',
                    body: 'Games will close and anything you haven\'t saved will be lost.',
                    confirmLabel: 'Restart Steam',
                    cancelLabel: 'Not now',
                    danger: true,
                    onConfirm: function () {
                        fetch('http://127.0.0.1:9001/restart', { method: 'POST' }).catch(function () {});
                    }
                });
            };
        }

        if (discardBtn) {
            discardBtn.onclick = function (e) {
                e.preventDefault();
                slsConfirmDialog({
                    title: 'Discard your changes?',
                    body: 'Your settings will go back to how they were before you started. This can\'t be undone.',
                    confirmLabel: 'Discard changes',
                    cancelLabel: 'Keep editing',
                    danger: true,
                    onConfirm: function () {
                        fetch('http://127.0.0.1:9001/config/full', { headers: { 'Connection': 'close' } })
                            .then(function (r) { return r.json(); })
                            .then(function (fresh) {
                                renderConfigPage(container, fresh, sync);
                                showStatusBanner('All unsaved changes discarded.', '#66c0f4');
                            }).catch(function (err) {
                                showStatusBanner('Could not discard changes: ' + err.message, '#ff4d4d');
                            });
                    }
                });
            };
        }

        // ---------------------------------------------------------------------
        // Danger zone actions
        // ---------------------------------------------------------------------
        function buildDefaultsPayload() {
            return {
                DisableFamilyShareLock: true,
                UseWhitelist: false,
                SafeMode: true,
                WarnHashMissmatch: false,
                NotifyInit: true,
                API: false,
                DisableCloud: false,
                DisableUpdates: false,
                DumpClientInterfaces: false,
                ExtendedLogging: false,
                MaxSchemaTries: 10,
                FakeWalletBalance: 0,
                LogLevels: 255,
                FakeName: '',
                FakeEmail: '',
                RyuuKey: '',
                DepotBoxKey: '',
                HubcapKey: '',
                IdleStatus: { AppId: 0, Title: '' },
                UnownedStatus: { AppId: 0, Title: '' },
                AppIds: [],
                AdditionalApps: [],
                FakeOffline: [],
                DepotBlacklist: [],
                FakeAppIds: {},
                ManifestIds: {},
                AppTokens: {},
                CDKeys: {},
                GameTitles: {},
                SubscriptionTimestamps: {},
                SteamIdOverride: {},
                DlcDataYaml: '',
                DenuvoGamesYaml: '',
                CloudSaves: {
                    Enabled: true,
                    Provider: 'local-only',
                    NotificationsEnabled: true,
                    AutoBackupBeforeReset: true,
                    BackupRetentionCount: 5,
                    PrefixSync: {
                        Enabled: true,
                        SyncAtStartup: true,
                        SyncOnLaunch: true,
                        SyncOnExit: true,
                        MaxFileMb: 64
                    },
                    StatsSync: {
                        Enabled: false,
                        SyncAchievements: true,
                        SyncPlaytime: true
                    }
                }
            };
        }

        var resetBtn = document.getElementById('sls-cfg-reset-btn');
        if (resetBtn) {
            resetBtn.onclick = function (e) {
                e.preventDefault();
                slsConfirmDialog({
                    title: 'Reset all Navi settings?',
                    body: 'Every setting goes back to its default. Your Steam account and save files are not touched. This can\'t be undone.',
                    confirmLabel: 'Reset settings',
                    cancelLabel: 'Cancel',
                    danger: true,
                    input: { label: 'Type RESET to confirm', placeholder: 'RESET', expected: 'RESET' },
                    mismatchMessage: 'That doesn\'t match. Type RESET exactly.',
                    onConfirm: function () {
                        var payload = buildDefaultsPayload();
                        fetch('http://127.0.0.1:9001/config/full', {
                            method: 'POST',
                            headers: { 'Content-Type': 'application/json' },
                            body: JSON.stringify(payload)
                        }).then(function (r) {
                            return r.json().then(function (data) {
                                return { ok: r.ok && data && data.success !== false, data: data };
                            });
                        }).then(function (res) {
                            if (!res.ok) {
                                showStatusBanner('Reset didn\'t finish: ' + ((res.data && res.data.message) || 'unknown error') + '. Nothing was lost — your settings are unchanged.', '#ff4d4d');
                                return;
                            }
                            fetch('http://127.0.0.1:9001/config/full', { headers: { 'Connection': 'close' } })
                                .then(function (r) { return r.json(); })
                                .then(function (fresh) {
                                    window.__naviConfigPendingBanner = {
                                        msg: 'Settings reset to default. Restart Steam to finish.',
                                        color: '#2ecc71'
                                    };
                                    renderConfigPage(container, fresh, sync);
                                }).catch(function (err) {
                                    showStatusBanner('Reset didn\'t finish: ' + (err.message || 'unknown error') + '. Nothing was lost — your settings are unchanged.', '#ff4d4d');
                                });
                        }).catch(function (err) {
                            showStatusBanner('Reset didn\'t finish: ' + (err.message || 'unknown error') + '. Nothing was lost — your settings are unchanged.', '#ff4d4d');
                        });
                    }
                });
            };
        }

        function removeAllRedirections(ids) {
            var index = 0;
            var stillRedirected = [];
            function settle(ok) {
                if (!ok) stillRedirected.push(ids[index - 1]);
                step();
            }
            function step() {
                if (index >= ids.length) {
                    var removed = ids.length - stillRedirected.length;
                    if (cfg.CloudSaves) cfg.CloudSaves.ManagedApps = stillRedirected.slice();
                    csState.managedAppsCount = stillRedirected.length;
                    refreshCloudSavesUI();
                    checkDirty();
                    if (stillRedirected.length === 0) {
                        showStatusBanner('Redirections removed for ' + ids.length + (ids.length === 1 ? ' game.' : ' games.'), '#2ecc71');
                    } else if (removed === 0) {
                        showStatusBanner('We couldn\'t remove any redirections. Nothing was lost — try again.', '#ff4d4d');
                    } else {
                        showStatusBanner('Redirections removed for ' + removed + ' of ' + ids.length + ' games. ' + stillRedirected.length + ' could not be removed.', '#f59e0b');
                    }
                    return;
                }
                var appId = ids[index];
                index++;
                var p;
                if (window.NaviCloudManager && window.NaviCloudManager.toggleAppRedirect) {
                    p = window.NaviCloudManager.toggleAppRedirect(appId, false);
                } else {
                    p = fetch('http://127.0.0.1:9001/cloud/apps/' + encodeURIComponent(appId) + '/toggle', {
                        method: 'POST',
                        headers: { 'Content-Type': 'text/plain' },
                        body: 'X-Navi-Auth: ' + (window.__naviAuthToken || '') + '\r\n' + JSON.stringify({ redirect: false })
                    }).then(function (r) {
                        return r.json().then(function (data) {
                            return { ok: r.ok, data: data };
                        });
                    });
                }
                if (p && typeof p.then === 'function') {
                    p.then(function (res) {
                        var ok = !!(res && (res.redirected === false || (res.ok && res.data && res.data.redirected === false)));
                        settle(ok);
                    }).catch(function () {
                        settle(false);
                    });
                } else {
                    settle(false);
                }
            }
            step();
        }

        var removeRedirectsBtn = document.getElementById('sls-cfg-remove-redirects-btn');
        if (removeRedirectsBtn) {
            removeRedirectsBtn.onclick = function (e) {
                e.preventDefault();
                var ids = (cfg.CloudSaves && Array.isArray(cfg.CloudSaves.ManagedApps)) ? cfg.CloudSaves.ManagedApps.slice() : [];
                if (ids.length === 0) {
                    showStatusBanner('No save redirections to remove.', '#66c0f4');
                    return;
                }
                slsConfirmDialog({
                    title: 'Remove redirections for ' + ids.length + (ids.length === 1 ? ' game?' : ' games?'),
                    body: 'Steam Cloud manages these saves again. No files are deleted.',
                    confirmLabel: 'Remove redirections',
                    cancelLabel: 'Cancel',
                    danger: true,
                    onConfirm: function () {
                        removeAllRedirections(ids);
                    }
                });
            };
        }

        var deleteBackupsBtn = document.getElementById('sls-cfg-delete-backups-btn');
        if (deleteBackupsBtn) {
            deleteBackupsBtn.onclick = function (e) {
                e.preventDefault();
                slsConfirmDialog({
                    title: 'Delete all save backups?',
                    body: 'Deletes saved copies of your saves on this computer. Your current saves and Steam Cloud saves are not touched. This can\'t be undone.',
                    confirmLabel: 'Delete backups',
                    cancelLabel: 'Cancel',
                    danger: true,
                    onConfirm: function () {
                        fetch('http://127.0.0.1:9001/cloud/backups/delete-all', {
                            method: 'POST',
                            headers: { 'Content-Type': 'text/plain' },
                            body: 'X-Navi-Auth: ' + (window.__naviAuthToken || '') + '\r\n'
                        }).then(function (r) {
                            return r.json().then(function (data) {
                                return { ok: r.ok, data: data };
                            });
                        }).then(function (res) {
                            if (res.ok && res.data && res.data.success) {
                                showStatusBanner('All save backups were deleted.', '#2ecc71');
                            } else {
                                showStatusBanner('Could not delete backups: ' + ((res.data && res.data.error) || 'unknown error'), '#ff4d4d');
                            }
                        }).catch(function (err) {
                            showStatusBanner('Could not delete backups: ' + (err.message || 'unknown error'), '#ff4d4d');
                        });
                    }
                });
            };
        }

        var dangerToCloudLink = document.getElementById('sls-cfg-danger-to-cloudsaves');
        if (dangerToCloudLink) {
            dangerToCloudLink.onclick = function (e) {
                e.preventDefault();
                switchTab('sls-cfg-sec-cloudsaves');
            };
        }
        // 11. Global Keyboard Shortcuts
        function handleKeyDown(e) {
            if (!container || !container.isConnected || container.style.display === 'none') {
                return;
            }
            if ((e.ctrlKey || e.metaKey) && (e.key === 's' || e.key === 'S')) {
                e.preventDefault();
                if (saveBtn) saveBtn.click();
                return;
            }
            if (e.key === '/' && document.activeElement !== searchInput) {
                var tag = (document.activeElement && document.activeElement.tagName) ? document.activeElement.tagName.toUpperCase() : '';
                if (tag !== 'INPUT' && tag !== 'TEXTAREA') {
                    e.preventDefault();
                    if (searchInput) {
                        searchInput.focus();
                        searchInput.select();
                    }
                }
                return;
            }
            if (e.key === 'Escape') {
                if (document.activeElement === searchInput) {
                    searchInput.value = '';
                    searchInput.blur();
                    if (searchInput.oninput) searchInput.oninput();
                }
            }
        }
        configKeyDownHandler = handleKeyDown;
        window.addEventListener('keydown', configKeyDownHandler);

        // ---------------------------------------------------------------------
        // Initial activation
        // ---------------------------------------------------------------------
        var pendingBanner = window.__naviConfigPendingBanner;
        window.__naviConfigPendingBanner = null;

        refreshCloudSavesUI();
        initialFormState = serializeCurrentForm();
        markClean();
        if (pendingBanner) showStatusBanner(pendingBanner.msg, pendingBanner.color);

    }
