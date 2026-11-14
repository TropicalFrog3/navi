/**
 * @file  config-sections.js
 * @brief HTML section templates and layout assembler for Navi configuration UI
 */
function buildConfigSectionsHtml(cfg) {
        var versionLabel = resolveVersionLabel();
        var contextText = (versionLabel || 'Navi') + ' · Saves to config.json on this computer';
        var cs = cfg.CloudSaves || {};
        var managedCount = (cs.ManagedApps && cs.ManagedApps.length) || 0;

        // ---------------------------------------------------------------------
        // S2 — Cloud saves
        // ---------------------------------------------------------------------
        var card1Content = makeSettingRow('Redirect save files', 'Also called CloudRedirect. Turning this on or off needs a Steam restart.',
                '<div style="display: flex; align-items: center; gap: 10px;">' +
                    '<span id="sls-cfg-cs-master-status" style="font-size: 13px; color: #8a929e;">' + (cs.Enabled !== false ? 'On · ' + managedCount + ' games' : 'Off') + '</span>' +
                    '<button id="sls-cfg-cs-master-btn" class="sls-cfg-btn" type="button" style="display: none;">' + (cs.Enabled !== false ? 'Turn off' : 'Turn on') + '</button>' +
                    makeToggle('sls-cfg-cs-master-toggle', cs.Enabled !== false) +
                '</div>',
                'CloudRedirect master redirect save files') +
            makeSettingRow('Launch notifications', 'Show a desktop notification when CloudRedirect loads into Steam.',
                makeToggle('sls-cfg-CloudSaves-NotificationsEnabled', cs.NotificationsEnabled !== false),
                'launch notifications desktop notification CloudRedirect') +
            makeSettingRow('Navi service', 'Navi moves saves in and out of Steam Cloud for you.',
                '<div style="display: flex; align-items: center; gap: 8px; flex-wrap: wrap;">' +
                    '<span id="sls-cfg-cs-engine-badge" role="status" aria-live="polite" style="padding: 3px 8px; border-radius: 4px; font-size: 11.5px; font-weight: 600; background: rgba(34, 197, 94, 0.14); color: #4ade80; border: 1px solid rgba(34, 197, 94, 0.3);">Not running</span>' +
                    '<span id="sls-cfg-cs-managed-badge" style="padding: 3px 8px; border-radius: 4px; font-size: 11.5px; background: rgba(26, 159, 255, 0.15); color: #66c0f4; border: 1px solid rgba(26, 159, 255, 0.3);">' + managedCount + ' games redirected</span>' +
                    '<span id="sls-cfg-cs-pending-badge" style="padding: 3px 8px; border-radius: 4px; font-size: 11.5px; display: none;"></span>' +
                '</div>', 'service status health managed apps pending operations') +
            '<div id="sls-cfg-cs-pending-callout" style="display: none; background: linear-gradient(90deg, #78350f, #92400e); border: 1px solid #f59e0b; border-radius: 6px; padding: 10px 14px; margin-top: 10px; color: #fef3c7; font-size: 12px; align-items: center; justify-content: space-between; gap: 10px; flex-wrap: wrap;">' +
                '<span id="sls-cfg-cs-pending-text">1 change waits for a restart</span>' +
                '<button id="sls-cfg-cs-restart-btn" class="sls-cfg-btn" type="button" style="background: #f59e0b; color: #1e1e1e; font-weight: 700; padding: 4px 12px; font-size: 12px; border-radius: 4px;">Restart Steam</button>' +
            '</div>' +
            '<div style="background: rgba(245, 158, 11, 0.1); border: 1px solid rgba(245, 158, 11, 0.35); border-radius: 6px; padding: 12px 16px; font-size: 12.5px; color: #fef3c7; margin-top: 10px; display: flex; align-items: center; justify-content: space-between; gap: 10px; flex-wrap: wrap;">' +
                '<span>Steam Cloud can overwrite redirected saves. Turn off Steam Cloud under Unlocking to stop that.</span>' +
                '<a href="#" id="sls-cfg-link-to-general" style="color: #38bdf8; text-decoration: underline; white-space: nowrap; font-weight: 500;">Open Unlocking ↗</a>' +
            '</div>';

        var providerList = [
            { val: 'local-only', label: 'This computer (offline)' },
            { val: 'gdrive', label: 'Google Drive' },
            { val: 'onedrive', label: 'Microsoft OneDrive' },
            { val: 'r2', label: 'Cloudflare R2' },
            { val: 's3', label: 'Amazon S3' }
        ];
        var activeProv = cs.Provider || 'local-only';
        var providerOptions = providerList.map(function (opt) {
            return '<option value="' + opt.val + '"' + (activeProv === opt.val ? ' selected' : '') + '>' + opt.label + '</option>';
        }).join('');

        var isS3OrR2 = (activeProv === 'r2' || activeProv === 's3');
        var card2Content = makeSettingRow('Where saves are stored', 'Pick this computer for offline play, or connect a cloud account to sync between devices.',
                '<select id="sls-cfg-cs-provider" class="sls-cfg-input" style="width: 240px;">' + providerOptions + '</select>', 'provider cloud storage where saves are stored') +
            '<div id="sls-cfg-cs-auth-block" style="background-color: #171a22; border: 1px solid rgba(255, 255, 255, 0.04); padding: 12px 16px; margin-top: 8px; border-radius: 6px;">' +
                '<div style="display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 10px;">' +
                    '<div style="display: flex; align-items: center; gap: 10px; flex-wrap: wrap;">' +
                        '<span id="sls-cfg-cs-auth-badge" style="padding: 3px 8px; border-radius: 4px; font-size: 11px; font-weight: 700; text-transform: uppercase; background: rgba(34, 197, 94, 0.14); color: #4ade80; border: 1px solid rgba(34, 197, 94, 0.3);">Offline</span>' +
                        '<span id="sls-cfg-cs-auth-email" style="font-size: 12.5px; color: #e2e8f0; display: none;"></span>' +
                    '</div>' +
                    '<div id="sls-cfg-cs-auth-actions" style="display: flex; gap: 8px; align-items: center; flex-wrap: wrap;"></div>' +
                '</div>' +
                '<div id="sls-cfg-cs-local-info" style="font-size: 12px; color: #8a929e; margin-top: 8px;">' +
                    "Saves are kept on this computer, inside Steam\'s cloud_redirect folder. No account or internet needed." +
                '</div>' +
                '<div id="sls-cfg-cs-creds-form" class="sls-cfg-item" data-search="r2 s3 credentials bucket access key secret endpoint cloudflare amazon" style="display: ' + (isS3OrR2 ? 'flex' : 'none') + '; flex-direction: column; align-items: stretch; gap: 10px; margin-top: 12px; padding: 14px; background: #14171d; border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 6px;">' +
                    '<div style="font-size: 12.5px; color: #8a929e; line-height: 17px;">Credentials are stored locally with <code>0600</code> permissions in <code>~/.config/CloudRedirect/</code> and never written to logs.</div>' +
                    '<div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 10px;">' +
                        '<div id="sls-cfg-cs-cred-account-id-wrap" style="display: ' + (activeProv === 'r2' ? 'flex' : 'none') + '; flex-direction: column; gap: 4px;">' +
                            '<label for="sls-cfg-cs-cred-account-id" style="font-size: 12px; font-weight: 500; color: #cbd5e1;">Account ID</label>' +
                            '<input type="text" id="sls-cfg-cs-cred-account-id" class="sls-cfg-input" style="width: 100%;" placeholder="Cloudflare Account ID" autocomplete="off" autocorrect="off" autocapitalize="off" spellcheck="false">' +
                        '</div>' +
                        '<div style="display: flex; flex-direction: column; gap: 4px;">' +
                            '<label for="sls-cfg-cs-cred-bucket" style="font-size: 12px; font-weight: 500; color: #cbd5e1;">Bucket</label>' +
                            '<input type="text" id="sls-cfg-cs-cred-bucket" class="sls-cfg-input" style="width: 100%;" placeholder="my-cloud-saves" autocomplete="off" autocorrect="off" autocapitalize="off" spellcheck="false">' +
                        '</div>' +
                        '<div style="display: flex; flex-direction: column; gap: 4px;">' +
                            '<label for="sls-cfg-cs-cred-access-key" style="font-size: 12px; font-weight: 500; color: #cbd5e1;">Access Key ID</label>' +
                            '<input type="text" id="sls-cfg-cs-cred-access-key" class="sls-cfg-input" style="width: 100%;" placeholder="Access Key ID" autocomplete="off" autocorrect="off" autocapitalize="off" spellcheck="false">' +
                        '</div>' +
                        '<div style="display: flex; flex-direction: column; gap: 4px;">' +
                            '<label for="sls-cfg-cs-cred-secret-key" style="font-size: 12px; font-weight: 500; color: #cbd5e1;">Secret Access Key</label>' +
                            '<div style="display: flex; gap: 6px; align-items: center;">' +
                                '<input type="password" id="sls-cfg-cs-cred-secret-key" class="sls-cfg-input" style="flex: 1; min-width: 0;" placeholder="Secret Access Key" autocomplete="new-password" autocorrect="off" autocapitalize="off" spellcheck="false">' +
                                '<button type="button" id="sls-cfg-cs-cred-secret-toggle" class="sls-cfg-btn" aria-pressed="false" aria-label="Show secret access key" style="padding: 6px 10px; font-size: 12px; flex-shrink: 0;">Show</button>' +
                            '</div>' +
                        '</div>' +
                        '<div style="display: flex; flex-direction: column; gap: 4px;">' +
                            '<label for="sls-cfg-cs-cred-region" style="font-size: 12px; font-weight: 500; color: #cbd5e1;">Region</label>' +
                            '<input type="text" id="sls-cfg-cs-cred-region" class="sls-cfg-input" style="width: 100%;" placeholder="' + (activeProv === 'r2' ? 'auto' : 'us-east-1') + '" autocomplete="off" autocorrect="off" autocapitalize="off" spellcheck="false">' +
                        '</div>' +
                        '<div style="display: flex; flex-direction: column; gap: 4px;">' +
                            '<label for="sls-cfg-cs-cred-endpoint" style="font-size: 12px; font-weight: 500; color: #cbd5e1;">Custom Endpoint (optional)</label>' +
                            '<input type="text" id="sls-cfg-cs-cred-endpoint" class="sls-cfg-input" style="width: 100%;" placeholder="https://..." autocomplete="off" autocorrect="off" autocapitalize="off" spellcheck="false">' +
                        '</div>' +
                        '<div style="display: flex; flex-direction: column; gap: 4px;">' +
                            '<label for="sls-cfg-cs-cred-prefix" style="font-size: 12px; font-weight: 500; color: #cbd5e1;">Key Prefix (optional)</label>' +
                            '<input type="text" id="sls-cfg-cs-cred-prefix" class="sls-cfg-input" style="width: 100%;" placeholder="CloudRedirect/" autocomplete="off" autocorrect="off" autocapitalize="off" spellcheck="false">' +
                        '</div>' +
                    '</div>' +
                    '<div style="display: flex; justify-content: flex-end; gap: 8px; align-items: center; flex-wrap: wrap; margin-top: 4px;">' +
                        '<button type="button" id="sls-cfg-cs-cred-save-btn" class="sls-cfg-btn sls-cfg-btn-primary" style="padding: 7px 14px; font-size: 12.5px;">Save &amp; Test Connection</button>' +
                    '</div>' +
                '</div>' +
            '</div>';

        var ps = (cs && cs.PrefixSync) || {};
        var prefixSyncCard = makeCard('Wine / Emulator save sync', 'PrefixSync synchronizes Goldberg and GSE emulator wineprefix saves outside Steam Cloud.',
            makeSettingRow('Enable Wine prefix save sync', 'Watches wineprefix emulator saves for managed games.',
                makeToggle('sls-cfg-PrefixSync-Enabled', ps.Enabled !== false), 'PrefixSync wine emulator prefix') +
            makeSettingRow('Sync at Steam startup', 'Scans and pulls emulator saves when Steam client starts.',
                makeToggle('sls-cfg-PrefixSync-Startup', ps.SyncAtStartup !== false), 'PrefixSync startup') +
            makeSettingRow('Sync on game launch', 'Pulls latest cloud saves before launching an emulator game.',
                makeToggle('sls-cfg-PrefixSync-Launch', ps.SyncOnLaunch !== false), 'PrefixSync launch') +
            makeSettingRow('Sync on game exit', 'Pushes save changes to cloud immediately after the game exits.',
                makeToggle('sls-cfg-PrefixSync-Exit', ps.SyncOnExit !== false), 'PrefixSync exit') +
            makeSettingRow('Max save file size (MB)', 'Skip individual files larger than this threshold.',
                '<input type="number" id="sls-cfg-PrefixSync-MaxMb" class="sls-cfg-input" style="width: 100px;" min="1" max="1024" value="' + (typeof ps.MaxFileMb === 'number' ? ps.MaxFileMb : 64) + '">',
                'PrefixSync MaxFileMb max mb'));

        var ss = (cs && cs.StatsSync) || {};
        var ssEnabled = !!ss.Enabled;
        var ssAchievements = (typeof ss.SyncAchievements === 'boolean') ? ss.SyncAchievements : true;
        var ssPlaytime = (typeof ss.SyncPlaytime === 'boolean') ? ss.SyncPlaytime : true;
        var statsSyncCard = makeCard('Stats sync', 'Synchronize unlocked achievements and total playtime across devices.',
            '<div style="background: rgba(245, 158, 11, 0.12); border: 1px solid rgba(245, 158, 11, 0.35); border-radius: 6px; padding: 10px 14px; font-size: 12.5px; color: #fef3c7; margin-bottom: 8px; display: flex; align-items: center; gap: 8px;">' +
                '<span aria-hidden="true">⚠️</span>' +
                '<span>Experimental — synchronizes unlocked achievements and playtime across devices via your cloud provider. Changes require a Steam restart.</span>' +
            '</div>' +
            makeSettingRow('Enable Stats Sync', 'Intercepts and syncs Steam UserGameStats and playtime records.',
                makeToggle('sls-cfg-StatsSync-Enabled', ssEnabled), 'StatsSync enable stats sync achievements playtime') +
            '<div id="sls-cfg-StatsSync-subrows" style="opacity: ' + (ssEnabled ? '1' : '0.5') + '; transition: opacity 0.15s ease;">' +
                makeSettingRow('Sync Achievements', 'Merge unlocked achievements across devices.',
                    makeToggle('sls-cfg-StatsSync-Achievements', ssAchievements), 'StatsSync sync achievements') +
                makeSettingRow('Sync Playtime', 'Sync total playtime counters across devices.',
                    makeToggle('sls-cfg-StatsSync-Playtime', ssPlaytime), 'StatsSync sync playtime') +
            '</div>');

        var defaultDstProv = (activeProv === 'local-only') ? 'gdrive' : 'local-only';
        var migrateSrcOptions = providerList.map(function (opt) {
            return '<option value="' + opt.val + '"' + (activeProv === opt.val ? ' selected' : '') + '>' + opt.label + '</option>';
        }).join('');
        var migrateDstOptions = providerList.map(function (opt) {
            return '<option value="' + opt.val + '"' + (defaultDstProv === opt.val ? ' selected' : '') + '>' + opt.label + '</option>';
        }).join('');

        var migrateCard = makeCard('Migrate cloud saves', 'Copy all redirected cloud save blobs and metadata from one storage provider to another.',
            '<div class="sls-cfg-item" data-search="migrate cloud saves copy provider source destination" style="flex-direction: column; align-items: stretch; gap: 12px;">' +
                '<div style="display: flex; align-items: flex-end; justify-content: space-between; gap: 12px; flex-wrap: wrap;">' +
                    '<div style="display: flex; align-items: flex-end; gap: 12px; flex-wrap: wrap; flex: 1 1 420px;">' +
                        '<div style="display: flex; flex-direction: column; gap: 4px; flex: 1 1 180px;">' +
                            '<label for="sls-cfg-cs-migrate-src" style="font-size: 12px; font-weight: 500; color: #cbd5e1;">From (source)</label>' +
                            '<select id="sls-cfg-cs-migrate-src" class="sls-cfg-input" style="width: 100%;">' + migrateSrcOptions + '</select>' +
                        '</div>' +
                        '<div style="display: flex; flex-direction: column; gap: 4px; flex: 1 1 180px;">' +
                            '<label for="sls-cfg-cs-migrate-dst" style="font-size: 12px; font-weight: 500; color: #cbd5e1;">To (destination)</label>' +
                            '<select id="sls-cfg-cs-migrate-dst" class="sls-cfg-input" style="width: 100%;">' + migrateDstOptions + '</select>' +
                        '</div>' +
                    '</div>' +
                    '<div style="display: flex; gap: 8px; align-items: center; flex-wrap: wrap;">' +
                        '<button type="button" id="sls-cfg-cs-migrate-scan-btn" class="sls-cfg-btn" style="padding: 7px 14px; font-size: 12.5px;">Scan Source</button>' +
                        '<button type="button" id="sls-cfg-cs-migrate-start-btn" class="sls-cfg-btn sls-cfg-btn-primary" style="padding: 7px 14px; font-size: 12.5px;">Start Migration</button>' +
                    '</div>' +
                '</div>' +
                '<div id="sls-cfg-cs-migrate-status" role="status" aria-live="polite" style="display: none; padding: 10px 12px; border-radius: 6px; font-size: 12.5px; background: #14171d; border: 1px solid rgba(255, 255, 255, 0.08); color: #cbd5e1;"></div>' +
            '</div>');

        var card4Content = '<div style="background-color: #171a22; border: 1px solid rgba(255, 255, 255, 0.06); padding: 16px 18px; margin-top: 8px; border-radius: 6px;">' +
            '<div style="display: flex; justify-content: space-between; align-items: flex-start; flex-wrap: wrap; gap: 12px; margin-bottom: 12px;">' +
                '<div>' +
                    '<div id="sls-cfg-cs-breadcrumb" style="font-size: 11px; color: #8a929e; margin-bottom: 4px; letter-spacing: 0.02em;">Home &gt; Account &gt; Your Steam Cloud</div>' +
                    '<div id="sls-cfg-cs-view-heading" style="font-size: 18px; font-weight: 600; color: #ffffff;">Your Steam Cloud</div>' +
                    '<div id="sls-cfg-cs-view-subtitle" style="font-size: 12px; color: #8a929e; margin-top: 2px;">Games and saves stored for your account.</div>' +
                '</div>' +
                '<div style="display: flex; gap: 8px; align-items: center; flex-wrap: wrap;">' +
                    '<button id="sls-cfg-cs-back-btn" class="sls-cfg-btn" type="button" style="display: none; padding: 6px 14px; font-size: 12px; color: #38bdf8; background: rgba(26, 159, 255, 0.12); border: 1px solid rgba(26, 159, 255, 0.35); border-radius: 4px; cursor: pointer; font-weight: 500;">&larr; Back to Games</button>' +
                    '<button id="sls-cfg-cs-scan-orphans-btn" class="sls-cfg-btn" type="button" style="padding: 6px 14px; font-size: 12px;">Scan Orphans</button>' +
                    '<button id="sls-cfg-cs-refresh-btn" class="sls-cfg-btn" type="button" style="padding: 6px 14px; font-size: 12px;">Refresh list</button>' +
                    '<div style="position: relative; display: inline-flex; align-items: center;">' +
                        '<span style="position: absolute; left: 10px; color: #626975; font-size: 12px; pointer-events: none;" aria-hidden="true">&#128269;</span>' +
                        '<input type="text" id="sls-cfg-add-cloud-app-input" class="sls-cfg-input" style="width: 220px; height: 32px; padding-left: 28px; padding-right: 24px; font-size: 12px;" placeholder="Search games or AppID..." aria-label="Search saves" autocomplete="off" autocorrect="off" autocapitalize="off" spellcheck="false">' +
                        '<button id="sls-cfg-cs-search-clear" type="button" aria-label="Clear search" style="display: none; position: absolute; right: 8px; background: none; border: none; color: #9ca3af; font-size: 14px; cursor: pointer; padding: 0 2px; line-height: 1;">&times;</button>' +
                    '</div>' +
                '</div>' +
            '</div>' +
            '<div id="sls-cfg-cs-orphans-panel" style="display: none; margin-bottom: 12px; padding: 12px 14px; background: #14171d; border: 1px solid rgba(56, 189, 248, 0.3); border-radius: 6px;"></div>' +
            '<div id="sls-cfg-cs-game-toolbar" style="display: none; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 10px; padding: 10px 14px; margin-bottom: 10px; background: #1b2838; border: 1px solid #2a475e; border-radius: 4px;"></div>' +
            '<div id="sls-cfg-managed-apps-list" class="navi-cloud-table-region" tabindex="0" role="region" aria-label="Your Steam Cloud" style="max-height: 420px; overflow-y: auto; background: #101822; border: 1px solid rgba(255, 255, 255, 0.06); border-radius: 6px;"></div>' +
        '</div>';

        var backupsCard = makeCard('Backups', 'Automatic and manual .tar.gz archives of your cloud saves.',
            makeSettingRow('Back up before resetting', 'Automatically create a .tar.gz backup archive before resetting a game\'s saves.',
                makeToggle('sls-cfg-CloudAutoBackup', cs.AutoBackupBeforeReset !== false),
                'CloudAutoBackup auto backup before resetting') +
            makeSettingRow('Backups to keep per game', 'Older backups beyond this limit are pruned automatically.',
                '<input type="number" id="sls-cfg-CloudRetention" class="sls-cfg-input" style="width: 100px;" min="1" max="100" value="' + (typeof cs.BackupRetentionCount === 'number' ? cs.BackupRetentionCount : 5) + '">',
                'CloudRetention backup retention count keep per game') +
            '<div class="sls-cfg-item" data-search="backups browser archive restore delete tar.gz" style="flex-direction: column; align-items: stretch; gap: 10px;">' +
                '<div style="display: flex; justify-content: space-between; align-items: center; gap: 10px; flex-wrap: wrap;">' +
                    '<div style="font-size: 14px; font-weight: 500; color: #ffffff;">Saved backup archives</div>' +
                    '<div style="display: flex; gap: 8px; align-items: center; flex-wrap: wrap;">' +
                        '<input type="text" id="sls-cfg-cs-backups-search" class="sls-cfg-input" style="width: 250px; height: 32px; font-size: 12px;" placeholder="Search backups by game name or AppID..." aria-label="Search backups" autocomplete="off" autocorrect="off" autocapitalize="off" spellcheck="false">' +
                        '<button type="button" id="sls-cfg-cs-backups-refresh" class="sls-cfg-btn" style="padding: 6px 12px; font-size: 12px;">Refresh backups</button>' +
                    '</div>' +
                '</div>' +
                '<div id="sls-cfg-cs-backups-list" class="navi-cloud-table-region" tabindex="0" role="region" aria-label="Global Backups Browser" style="max-height: 320px; overflow-y: auto; background: #101822; border: 1px solid rgba(255, 255, 255, 0.06); border-radius: 6px;"></div>' +
            '</div>');

        var sec1Html = makeSection('sls-cfg-sec-cloudsaves', 'Cloud saves',
            'Keeps saves for unlocked and family-shared games outside Steam Cloud. Choose this computer, or connect an account to sync between devices.',
            makeCard('Status', '', card1Content) +
            makeCard('Where saves are stored', '', card2Content) +
            prefixSyncCard +
            statsSyncCard +
            migrateCard +
            makeCard('Your Steam Cloud', '', card4Content) +
            backupsCard,
            true);

        // ---------------------------------------------------------------------
        // S3 — Provider keys
        // ---------------------------------------------------------------------
        var apiCards = makeApiKeyRow('RyuuKey', 'Ryuu key', 'Used for Ryuu manifests and depot lists.', cfg.RyuuKey, 'Paste your Ryuu key', 'sls-cfg-ryuu-reject-banner', 'ryuu') +
            makeApiKeyRow('DepotBoxKey', 'DepotBox key', 'Used for DepotBox manifests and depot lists.', cfg.DepotBoxKey, 'Paste your DepotBox key', 'sls-cfg-depotbox-reject-banner', 'depotbox') +
            makeApiKeyRow('HubcapKey', 'HubcapDB key', 'Used for HubcapDB manifests and depot lists.', cfg.HubcapKey, 'Paste your HubcapDB key', 'sls-cfg-hubcap-reject-banner', 'hubcap') +
            '<div style="font-size: 12px; color: #8a929e; margin-top: 10px;">No keys added. Navi works without them — add one only if a provider stops your downloads. Keys stay on this computer and are never shown in logs.</div>';

        var sec2Html = makeSection('sls-cfg-sec-api', 'Provider keys',
            'Some providers ask for a key before they\'ll send game files. Keys stay in config.json on this computer and are only sent to the provider that asked.',
            makeCard('Download providers', 'Optional. Navi works without keys unless a provider blocks your downloads.', apiCards),
            false);

        // ---------------------------------------------------------------------
        // S4 — Unlocking
        // ---------------------------------------------------------------------
        var sec3Html = makeSection('sls-cfg-sec-general', 'Unlocking',
            'Choose which games Navi unlocks and how it behaves with Steam.',
            makeCard('Library access', '',
                makeSettingRow('Only unlock the games I list', 'On: just the games in Library. Off: every game except the ones you list.',
                    makeToggle('sls-cfg-UseWhitelist', cfg.UseWhitelist), 'UseWhitelist whitelist unlock games') +
                makeSettingRow('Release family sharing lock', 'Bypasses client-side family sharing locks and hides unmasked shared games so another member won\'t kick you (shows Online or IdleStatus). Online multiplayer still needs FakeAppIds.',
                    makeToggle('sls-cfg-DisableFamilyShareLock', cfg.DisableFamilyShareLock), 'DisableFamilyShareLock family sharing') +
                makeSettingRow('Stop updates for extra games', 'Applies to games you don\'t own. Games you own still update.',
                    makeToggle('sls-cfg-DisableUpdates', cfg.DisableUpdates), 'DisableUpdates updates')) +
            makeCard('Steam Cloud', '',
                makeSettingRow('Turn off Steam Cloud', 'Stops Steam from syncing saves. Recommended if you use Navi cloud saves.',
                    makeToggle('sls-cfg-DisableCloud', cfg.DisableCloud), 'DisableCloud Steam Cloud Valve') +
                '<div id="sls-cfg-steam-cloud-note" style="background: rgba(245, 158, 11, 0.1); border: 1px solid rgba(245, 158, 11, 0.35); border-radius: 6px; padding: 12px 16px; font-size: 12.5px; color: #fef3c7; margin-top: 8px; display: flex; gap: 10px; align-items: center; justify-content: space-between; flex-wrap: wrap;">' +
                    '<span>Cloud saves are on. Steam Cloud can overwrite them.</span>' +
                    '<a href="#" id="sls-cfg-link-to-cloudsaves" style="color: #38bdf8; text-decoration: underline; white-space: nowrap; font-weight: 500;">Open Cloud saves ↗</a>' +
                '</div>') +
            makeCard('Compatibility', '',
                makeSettingRow('Safe mode', 'Turns Navi off when this Steam version isn\'t known to work. Recommended on Steam Deck.',
                    makeToggle('sls-cfg-SafeMode', cfg.SafeMode), 'SafeMode safe mode') +
                makeSettingRow('Warn me about untested Steam updates', 'Shows a notification when Steam updates to a version Navi hasn\'t been tested with.',
                    makeToggle('sls-cfg-WarnHashMissmatch', cfg.WarnHashMissmatch), 'WarnHashMissmatch hash warning')) +
            makeCard('Automation', '',
                makeSettingRow('Tell me when Navi is ready', 'Shows a notification when Navi finishes loading.',
                    makeToggle('sls-cfg-NotifyInit', cfg.NotifyInit !== false), 'NotifyInit notification ready') +
                makeSettingRow('Allow commands from other apps', 'Lets tools send commands to Navi through /tmp/navi.API. Only turn this on if you use such a tool.',
                    makeToggle('sls-cfg-API', !!cfg.API), 'API command local api') +
                makeSettingRow('Achievement detail retries', 'How many times Navi asks Steam for achievement details. 0 uses only the saved copy.',
                    '<input type="number" id="sls-cfg-MaxSchemaTries" class="sls-cfg-input" style="width: 120px;" min="0" max="100" value="' + (typeof cfg.MaxSchemaTries === 'number' ? cfg.MaxSchemaTries : 10) + '">',
                    'MaxSchemaTries achievement schema retries', { errorFor: 'sls-cfg-MaxSchemaTries' })),
            false);

        // ---------------------------------------------------------------------
        // S5 — Account
        // ---------------------------------------------------------------------
        var idleStatus = cfg.IdleStatus || {};
        var unownedStatus = cfg.UnownedStatus || {};

        var sec4Html = makeSection('sls-cfg-sec-profile', 'Account',
            'These changes are only visible in your Steam client. They are not sent to Steam.',
            makeCard('Profile', '',
                makeSettingRow('Display name', 'Replaces your Steam name in this client. Leave empty to use your real name.',
                    '<div style="display: flex; gap: 8px; align-items: center; flex-wrap: wrap;">' +
                        '<input type="text" id="sls-cfg-FakeName" class="sls-cfg-input" style="width: 260px;" value="' + escapeHtml(cfg.FakeName || '') + '" placeholder="TropicalFrog3">' +
                        '<button id="sls-cfg-clear-FakeName" type="button" class="sls-cfg-btn" style="padding: 6px 12px; font-size: 12px;">Clear</button>' +
                    '</div>', 'FakeName display name persona') +
                makeSettingRow('Email', 'Shown in this client only. Leave empty to hide it.',
                    '<input type="text" id="sls-cfg-FakeEmail" class="sls-cfg-input" style="width: 260px;" value="' + escapeHtml(cfg.FakeEmail || '') + '" placeholder="user@example.com">', 'FakeEmail email') +
                makeSettingRow('Wallet balance', 'Shown in this client only. Set 0 to hide it.',
                    '<input type="number" id="sls-cfg-FakeWalletBalance" class="sls-cfg-input" style="width: 160px;" value="' + (typeof cfg.FakeWalletBalance === 'number' ? cfg.FakeWalletBalance : 0) + '">', 'FakeWalletBalance wallet balance')) +
            makeCard('Spoofing', 'Spoofing makes a game or Steam see different account details. Most people don\'t need this.',
                makeSettingRow('Steam ID override', 'Only needed when a game reads your Steam ID before Navi can stand in. Leave empty to do this automatically.',
                    '<input type="text" id="sls-cfg-SteamIdOverride" class="sls-cfg-input" style="width: 280px;" placeholder="Leave empty for automatic" value="' + escapeHtml(typeof cfg.SteamIdOverride === 'object' ? formatKvForEditor(cfg.SteamIdOverride) : (cfg.SteamIdOverride || '')) + '">', 'SteamIdOverride steam id override') +
                makeSettingRow('Idle status', 'Shows a game as running while you\'re away. Set the game to 0 to turn this off.',
                    '<div style="display: flex; gap: 8px; flex-wrap: wrap;">' +
                        '<input type="number" id="sls-cfg-IdleStatus-AppId" class="sls-cfg-input" style="width: 120px;" placeholder="Game" value="' + (idleStatus.AppId || 0) + '">' +
                        '<input type="text" id="sls-cfg-IdleStatus-Title" class="sls-cfg-input" style="width: 220px;" placeholder="Title shown" value="' + escapeHtml(idleStatus.Title || '') + '">' +
                    '</div>', 'IdleStatus idle status presence') +
                makeSettingRow('Unowned status', 'Shown while an unlocked game runs. Set the game to 0 to turn this off.',
                    '<div style="display: flex; gap: 8px; flex-wrap: wrap;">' +
                        '<input type="number" id="sls-cfg-UnownedStatus-AppId" class="sls-cfg-input" style="width: 120px;" placeholder="Game" value="' + (unownedStatus.AppId || 0) + '">' +
                        '<input type="text" id="sls-cfg-UnownedStatus-Title" class="sls-cfg-input" style="width: 220px;" placeholder="Title shown" value="' + escapeHtml(unownedStatus.Title || '') + '">' +
                    '</div>', 'UnownedStatus unowned status presence') +
                makeSettingRow('Purchase dates', 'Changes the date a game shows as bought. Games you own only.',
                    '<input type="text" id="sls-cfg-SubscriptionTimestamps" class="sls-cfg-input" style="width: 280px;" placeholder="AppID: 1709424000" value="' + escapeHtml(typeof cfg.SubscriptionTimestamps === 'object' ? formatKvForEditor(cfg.SubscriptionTimestamps) : (cfg.SubscriptionTimestamps || '')) + '">', 'SubscriptionTimestamps purchase dates')),
            false);

        // ---------------------------------------------------------------------
        // S6 — Library
        // ---------------------------------------------------------------------
        var appListsContent = makeTextareaItem('sls-cfg-AppIds', 'Unlocked games', 'One AppID per line. Adding a game also includes its DLC.', formatArrayForEditor(cfg.AppIds), '440\n730\n480...', 4, { addBtnId: 'sls-cfg-add-app-btn', addBtnLabel: '+ Add game', searchTerms: 'unlocked games appids' }) +
            makeTextareaItem('sls-cfg-AdditionalApps', 'Extra games', 'Games that aren\'t in your library. Use this only for games you don\'t own — downloads of shared games can break.', formatArrayForEditor(cfg.AdditionalApps), '480, 730...', 3, { addBtnId: 'sls-cfg-add-extra-app-btn', addBtnLabel: '+ Add game' }) +
            makeTextareaItem('sls-cfg-FakeOffline', 'Show as offline', 'These games think Steam is offline.', formatArrayForEditor(cfg.FakeOffline), '480, 730...', 3, { addBtnId: 'sls-cfg-add-offline-btn', addBtnLabel: '+ Add game' }) +
            makeTextareaItem('sls-cfg-FakeAppIds', 'Play as another game', 'Makes a game report a different AppID so multiplayer works. Never run two games as the same AppID at once.', formatKvForEditor(cfg.FakeAppIds), '480: 440', 3, { addBtnId: 'sls-cfg-add-fakeappid-btn', addBtnLabel: '+ Add mapping' }) +
            makeTextareaItem('sls-cfg-GameTitles', 'Game names', 'Changes the title shown in your library. Games you own only.', formatKvForEditor(cfg.GameTitles), '440: Team Fortress 2 Custom', 3, { addBtnId: 'sls-cfg-add-gametitle-btn', addBtnLabel: '+ Add title' });

        var downloadContent = makeTextareaItem('sls-cfg-ManifestIds', 'Lock to a version', 'Keeps a game on one file version. Format: AppID: ManifestID.', formatKvForEditor(cfg.ManifestIds), '441: 123456789012345678', 3, { addBtnId: 'sls-cfg-add-manifest-btn', addBtnLabel: '+ Add version' }) +
            makeTextareaItem('sls-cfg-DepotBlacklist', 'Skip these depots', 'Navi won\'t download these depots.', formatArrayForEditor(cfg.DepotBlacklist), '228980, 228985...', 3, { addBtnId: 'sls-cfg-add-depot-blacklist-btn', addBtnLabel: '+ Add depot' });

        var advancedContent = makeTextareaItem('sls-cfg-AppTokens', 'App tokens', 'Product info tokens from Steam. Rarely needed.', formatKvForEditor(cfg.AppTokens), '730: 987654321098765432', 3, { addBtnId: 'sls-cfg-add-apptoken-btn', addBtnLabel: '+ Add token' }) +
            makeTextareaItem('sls-cfg-CDKeys', 'Legacy CD keys', 'Required by some older games.', formatKvForEditor(cfg.CDKeys), '440: ABCD-1234-EFGH-5678', 3, { addBtnId: 'sls-cfg-add-cdkey-btn', addBtnLabel: '+ Add key' });

        var yamlContent = makeTextareaItem('sls-cfg-DlcDataYaml', 'DLC details', 'Extra DLC mapping, only needed when a game reaches Steam\'s 64-DLC limit.', cfg.DlcDataYaml || '', '440:\n  441: "DLC 1 Name"', 4) +
            makeTextareaItem('sls-cfg-DenuvoGamesYaml', 'Denuvo account locks', 'Blocks these games from unlocking on other accounts.', cfg.DenuvoGamesYaml || '', '76561198000000001:\n  - 440\n  - 730', 4);

        var sec5Html = makeSection('sls-cfg-sec-overrides', 'Library',
            'Games, DLC and download files Navi manages. Lists take one entry per line.',
            makeCard('Games', '', appListsContent) +
            makeCard('Downloads', '', downloadContent) +
            makeCard('Advanced maps', '', advancedContent) +
            makeCard('Raw YAML', '', yamlContent),
            false);

        // ---------------------------------------------------------------------
        // S7 — Diagnostics
        // ---------------------------------------------------------------------
        var LOG_LEVEL_FLAGS = [
            { flag: 0x01, name: 'Trace', hex: '0x01', desc: 'Detailed tracing statements' },
            { flag: 0x02, name: 'Once', hex: '0x02', desc: 'Only log statement once' },
            { flag: 0x04, name: 'Debug', hex: '0x04', desc: 'Internal debugging output' },
            { flag: 0x08, name: 'Warn', hex: '0x08', desc: 'Non-fatal recoverable warnings' },
            { flag: 0x10, name: 'Error', hex: '0x10', desc: 'Fatal unrecoverable errors' },
            { flag: 0x20, name: 'Info', hex: '0x20', desc: 'General informational logs' },
            { flag: 0x40, name: 'Short notifications', hex: '0x40', desc: 'Short toast notifications' },
            { flag: 0x80, name: 'Long notifications', hex: '0x80', desc: 'Long toast notifications' }
        ];

        var currentLogLevel = typeof cfg.LogLevels === 'number' ? cfg.LogLevels : 255;
        var bitmaskCheckboxes = LOG_LEVEL_FLAGS.map(function (lf) {
            var checked = (currentLogLevel & lf.flag) !== 0;
            return '<label class="sls-cfg-item" data-search="' + lf.name.toLowerCase() + ' ' + lf.hex + ' ' + lf.desc.toLowerCase() + '" style="background-color: #171a22; border: 1px solid rgba(255, 255, 255, 0.04); padding: 10px 16px; margin-top: 6px; border-radius: 6px; display: flex; align-items: center; justify-content: space-between; cursor: pointer;">' +
                '<div>' +
                    '<div style="font-size: 13.5px; font-weight: 500; color: #ffffff;">' + lf.name + ' <span style="color: #38bdf8; font-family: monospace; font-size: 12px;">(' + lf.hex + ')</span></div>' +
                    '<div style="font-size: 12px; color: #8a929e;">' + lf.desc + '</div>' +
                '</div>' +
                '<input type="checkbox" id="sls-cfg-log-0x' + lf.flag.toString(16) + '" class="sls-cfg-log-flag" data-flag="' + lf.flag + '" ' + (checked ? 'checked' : '') + ' style="width: 18px; height: 18px; cursor: pointer;">' +
            '</label>';
        }).join('');

        var presetsHtml = '<div style="display: flex; gap: 8px; flex-wrap: wrap; margin-bottom: 12px;">' +
            '<button type="button" class="sls-preset-chip ' + (currentLogLevel === 255 ? 'active' : '') + '" data-preset="255">Normal</button>' +
            '<button type="button" class="sls-preset-chip ' + (currentLogLevel === 248 ? 'active' : '') + '" data-preset="248">Fewer messages</button>' +
            '<button type="button" class="sls-preset-chip ' + (currentLogLevel === 24 ? 'active' : '') + '" data-preset="24">Problems only</button>' +
            '<button type="button" class="sls-preset-chip ' + (currentLogLevel === 0 ? 'active' : '') + '" data-preset="0">Silent</button>' +
        '</div>';

        var logLevelsCard = presetsHtml +
            makeSettingRow('Log levels', 'Bitmask value. Only change this if you know the flags.',
                '<input type="text" id="sls-cfg-loglevel-hex" class="sls-cfg-input" style="width: 180px; font-family: monospace; font-weight: 700; text-align: center;" value="0x' + currentLogLevel.toString(16).toUpperCase() + ' (' + currentLogLevel + ')">',
                'LogLevels log levels bitmask hex') +
            '<div style="display: flex; flex-direction: column; margin-top: 6px;">' + bitmaskCheckboxes + '</div>';

        var diagCard = makeSettingRow('Log every Steam call', 'Makes the log file very large. Only for debugging.',
                makeToggle('sls-cfg-ExtendedLogging', cfg.ExtendedLogging), 'ExtendedLogging extended logging') +
            makeSettingRow('Write Steam\'s interface list to the log', 'Used when you report a problem.',
                makeToggle('sls-cfg-DumpClientInterfaces', cfg.DumpClientInterfaces), 'DumpClientInterfaces dump interfaces diagnostics');

        var sec6Html = makeSection('sls-cfg-sec-logging', 'Diagnostics',
            'Logs help you and the Navi team find problems. Leave these on their defaults unless you\'re troubleshooting.',
            makeCard('Log detail', '', logLevelsCard) +
            makeCard('Debug tools', '', diagCard),
            false);

        // ---------------------------------------------------------------------
        // S8 — Danger zone
        // ---------------------------------------------------------------------
        var redirectDescText = (managedCount > 0)
            ? 'Stops redirecting saves for ' + managedCount + (managedCount === 1 ? ' game.' : ' games.') + ' Files already redirected stay where they are.'
            : 'No games have redirected saves right now.';

        var sec7Html = makeSection('sls-cfg-sec-danger', 'Danger zone',
            'These actions change or delete your Navi settings. You can\'t undo them.',
            makeCard('Reset all settings', 'Puts every setting back to its default: unlocked games, cloud provider, keys and logs. Your Steam account and save files are not touched.',
                '<div class="sls-cfg-item sls-cfg-danger" data-search="reset all settings danger defaults">' +
                    '<button id="sls-cfg-reset-btn" class="sls-cfg-btn sls-cfg-btn-danger" type="button" style="padding: 8px 14px;">Reset settings</button>' +
                '</div>') +
            makeCard('Remove all save redirections', '',
                '<div class="sls-cfg-item sls-cfg-danger" data-search="remove all save redirections danger" style="display: flex; align-items: center; justify-content: space-between; gap: 12px; flex-wrap: wrap;">' +
                    '<div id="sls-cfg-danger-redirect-desc" style="font-size: 13px; color: #8a929e; line-height: 18px; flex: 1 1 320px;">' + redirectDescText + '</div>' +
                    '<button id="sls-cfg-remove-redirects-btn" class="sls-cfg-btn sls-cfg-btn-danger" type="button"' + (managedCount > 0 ? '' : ' disabled') + ' style="padding: 8px 14px;">Remove redirections</button>' +
                '</div>') +
            makeCard('Delete all backups', '',
                '<div class="sls-cfg-item sls-cfg-danger" data-search="delete all backups danger" style="display: flex; align-items: center; justify-content: space-between; gap: 12px; flex-wrap: wrap;">' +
                    '<div style="font-size: 13px; color: #8a929e; line-height: 18px; flex: 1 1 320px;">Deletes saved copies of your saves on this computer. Your current saves and Steam Cloud saves are not touched.</div>' +
                    '<button id="sls-cfg-delete-backups-btn" class="sls-cfg-btn sls-cfg-btn-danger" type="button" style="padding: 8px 14px;">Delete backups</button>' +
                '</div>') +
            '<div style="background: #171a22; border: 1px solid rgba(255, 255, 255, 0.06); border-radius: 6px; padding: 12px 16px; margin-top: 14px; display: flex; align-items: center; justify-content: space-between; gap: 10px; flex-wrap: wrap;">' +
                '<div style="display: flex; align-items: center; gap: 8px; font-size: 13px; color: #8a929e;">' +
                    '<span style="font-size: 14px;">☁️</span>' +
                    '<span id="sls-cfg-danger-cloud-status">Signed in to ' + providerLabel(cs.Provider) + '. Manage this in Cloud saves.</span>' +
                '</div>' +
                '<a href="#" id="sls-cfg-danger-to-cloudsaves" style="color: #38bdf8; text-decoration: underline; font-size: 13px;">Open Cloud saves ↗</a>' +
            '</div>',
            false);

        var headerHtml = '<div style="max-width: 980px; width: 100%; box-sizing: border-box; margin: 0 0 16px 0;">' +
            '<div id="sls-cfg-context" style="margin-bottom: 6px; font-size: 12px; color: #8a929e;">' + escapeHtml(contextText) + '</div>' +
            '<h1 id="sls-cfg-main-title" style="margin: 0; color: #ffffff; font-size: 26px; font-weight: 700; line-height: 32px;">Cloud saves</h1>' +
            '<p id="sls-cfg-main-desc" style="margin: 6px 0 0 0; font-size: 13.5px; line-height: 19px; color: #8a929e; max-width: 760px;">Keeps saves for unlocked and family-shared games outside Steam Cloud. Choose this computer, or connect an account to sync between devices.</p>' +
        '</div>' +
        '<div id="sls-cfg-no-results" role="status" aria-live="polite" style="display: none; max-width: 980px; width: 100%; box-sizing: border-box; margin-top: 16px; padding: 16px; background-color: #1c202a; border-radius: 6px; color: #e2e8f0; font-size: 13px;"></div>';

        var sidebarHtml = '<div class="PageListColumn sls-cfg-sidebar" style="background-color: rgb(42, 45, 52); width: 230px; min-width: 220px; max-width: 240px; height: 100%; padding: 24px 0px 16px 0px; display: flex; flex-direction: column; box-sizing: border-box; flex-shrink: 0; user-select: none;">' +
            '<div style="color: #ffffff; font-size: 17px; font-weight: 700; padding: 0px 16px 0px 24px; margin: 0px 0px 14px 0px; line-height: 24px;">Navi</div>' +
            '<div style="margin: 0 16px 12px 16px;">' +
                '<label for="sls-cfg-search" style="display: block; font-size: 11px; font-weight: 600; color: rgb(139, 146, 154); margin: 0 0 4px 2px;">Search settings</label>' +
                '<div style="position: relative;">' +
                    '<input type="text" id="sls-cfg-search" placeholder="Try &quot;backup&quot; or &quot;depot&quot;" style="width: 100%; background: rgba(23, 29, 37, 0.7); border: none; border-radius: 2px; color: rgb(223, 227, 230); padding: 6px 26px 6px 28px; font-size: 12px; height: 30px; min-height: 30px; outline: none; box-sizing: border-box;">' +
                    '<svg style="position: absolute; left: 8px; top: 8px; width: 14px; height: 14px; color: rgb(139, 146, 154);" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z"></path></svg>' +
                    '<button id="sls-cfg-search-clear" type="button" aria-label="Clear search" style="display: none; position: absolute; right: 6px; top: 5px; background: none; border: none; color: rgb(139, 146, 154); font-size: 16px; line-height: 1; cursor: pointer; padding: 0 2px;">&times;</button>' +
                '</div>' +
            '</div>' +
            '<div class="sls-cfg-tablist" role="tablist" aria-orientation="vertical" style="display: flex; flex-direction: column; flex: 1; overflow-y: auto; padding: 0; margin: 0;">' +
                '<div class="sls-cfg-pill active" role="tab" tabindex="0" aria-selected="true" data-target="sls-cfg-sec-cloudsaves">' +
                    '<div style="width: 20px; height: 20px; margin-right: 12px; display: flex; align-items: center; justify-content: center; flex-shrink: 0;">' +
                        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" width="20" height="20"><path d="M17.5 19H9a7 7 0 1 1 6.71-9h1.79a4.5 4.5 0 1 1 0 9Z"/><path d="M12 13v6"/><path d="m9 16 3 3 3-3"/></svg>' +
                    '</div>' +
                    '<span>Cloud saves</span>' +
                '</div>' +
                '<div class="sls-cfg-pill" role="tab" tabindex="0" aria-selected="false" data-target="sls-cfg-sec-api">' +
                    '<div style="width: 20px; height: 20px; margin-right: 12px; display: flex; align-items: center; justify-content: center; flex-shrink: 0;">' +
                        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 36 36" fill="none" width="20" height="20"><path fill="currentColor" d="M6.05882 7.05042C8.17647 6.59664 10.1429 5.9916 12.1092 5.08403C14.0756 4.02521 16.042 2.81513 17.7059 1.30252C18.0084 1.15126 18.3109 1 18.6134 1C18.916 1 19.2185 1.15126 19.3697 1.30252C21.0336 2.81513 23 4.02521 24.9664 5.08403C26.7815 5.9916 28.8992 6.7479 31.0168 7.05042C31.6218 7.05042 32.0756 7.65546 32.0756 8.2605C32.0756 14.6134 31.1681 19.4538 29.2017 23.3866C27.2353 27.4706 23.9076 30.6471 19.2185 33.6723C19.0672 33.8235 18.7647 33.8235 18.4622 33.8235C18.1597 33.8235 18.0084 33.8235 17.7059 33.6723C13.0168 30.7983 9.68908 27.4706 7.72269 23.3866C5.90756 19.4538 5 14.6134 5 8.2605C5 7.65546 5.45378 7.20168 6.05882 7.05042Z"></path></svg>' +
                    '</div>' +
                    '<span>Provider keys</span>' +
                '</div>' +
                '<div class="sls-cfg-pill" role="tab" tabindex="0" aria-selected="false" data-target="sls-cfg-sec-general">' +
                    '<div style="width: 20px; height: 20px; margin-right: 12px; display: flex; align-items: center; justify-content: center; flex-shrink: 0;">' +
                        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 36 36" fill="none" width="20" height="20"><path d="M34 6H2V28H10.6L9 34H27L25.4 28H34V6ZM30 24H6V10H30V24Z" fill="currentColor"></path></svg>' +
                    '</div>' +
                    '<span>Unlocking</span>' +
                '</div>' +
                '<div class="sls-cfg-pill" role="tab" tabindex="0" aria-selected="false" data-target="sls-cfg-sec-profile">' +
                    '<div style="width: 20px; height: 20px; margin-right: 12px; display: flex; align-items: center; justify-content: center; flex-shrink: 0;">' +
                        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 36 36" fill="none" width="20" height="20"><path d="M2 6.04999V30.05H34V6.04999H2ZM12 9.04999C13.933 9.04999 15.5 10.617 15.5 12.55C15.5 14.483 13.933 16.05 12 16.05C10.067 16.05 8.5 14.483 8.5 12.55C8.5 10.617 10.067 9.04999 12 9.04999ZM5 27.05C5 23.184 8.13401 20.05 12 20.05C15.866 20.05 19 23.184 19 27.05H5ZM30.05 21.05H22.05V17.05H30.05V21.05ZM29.99 14.05H20V10.05H30L29.99 14.05Z" fill="currentColor"></path></svg>' +
                    '</div>' +
                    '<span>Account</span>' +
                '</div>' +
                '<div class="sls-cfg-pill" role="tab" tabindex="0" aria-selected="false" data-target="sls-cfg-sec-overrides">' +
                    '<div style="width: 20px; height: 20px; margin-right: 12px; display: flex; align-items: center; justify-content: center; flex-shrink: 0;">' +
                        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 36 36" fill="none" width="20" height="20"><path fill-rule="evenodd" clip-rule="evenodd" d="M33 19.9286H3L7.35714 6H28.6429L33 19.9286ZM3 31.7143V24.2143H33V31.7143H3Z" fill="currentColor"></path></svg>' +
                    '</div>' +
                    '<span>Library</span>' +
                '</div>' +
                '<div class="sls-cfg-pill" role="tab" tabindex="0" aria-selected="false" data-target="sls-cfg-sec-logging">' +
                    '<div style="width: 20px; height: 20px; margin-right: 12px; display: flex; align-items: center; justify-content: center; flex-shrink: 0;">' +
                        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" fill="none" width="20" height="20"><path fill="currentColor" fill-rule="evenodd" clip-rule="evenodd" d="M10 1.66666C14.6024 1.66666 18.3334 5.39762 18.3334 10C18.3334 14.6024 14.6024 18.3333 10 18.3333C5.39765 18.3333 1.66669 14.6024 1.66669 10C1.66669 5.39762 5.39765 1.66666 10 1.66666ZM12.2223 5L11.389 10H8.61119L7.77786 5H12.2223ZM10.0001 15C8.92619 15 8.05564 14.1294 8.05564 13.0556C8.05564 11.9817 8.92619 11.1111 10.0001 11.1111C11.074 11.1111 11.9445 11.9817 11.9445 13.0556C11.9445 14.1294 11.074 15 10.0001 15Z"></path></svg>' +
                    '</div>' +
                    '<span>Diagnostics</span>' +
                '</div>' +
                '<div class="sls-cfg-pill" role="tab" tabindex="0" aria-selected="false" data-target="sls-cfg-sec-danger">' +
                    '<div style="width: 20px; height: 20px; margin-right: 12px; display: flex; align-items: center; justify-content: center; flex-shrink: 0;">' +
                        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" width="20" height="20"><path d="M10.29 3.86 1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0Z"></path><path d="M12 9v4"></path><path d="M12 17h.01"></path></svg>' +
                    '</div>' +
                    '<span>Danger zone</span>' +
                '</div>' +
            '</div>' +
            '<div style="height: 1px; background: rgba(255,255,255,0.08); margin: 12px 16px;"></div>' +
            '<div class="sls-cfg-saveblock" style="padding: 0 16px; display: flex; flex-direction: column; gap: 8px;">' +
                '<div id="sls-cfg-dirty-pill" role="status" aria-live="polite" style="font-size: 11px; font-weight: 500; color: #2ecc71; text-align: center; padding: 2px 0;">All changes saved</div>' +
                '<button id="sls-cfg-save-btn" class="sls-cfg-btn" type="button" disabled style="background: rgba(255, 255, 255, 0.08); color: #64748b; border: none; border-radius: 6px; padding: 10px 14px; font-size: 13px; font-weight: 600; cursor: pointer; width: 100%; text-align: center;">Save changes</button>' +
                '<div style="display: flex; justify-content: space-between; align-items: center; gap: 8px;">' +
                    '<button id="sls-cfg-discard-btn" class="sls-cfg-btn" type="button" style="display: none; background-color: rgba(255, 255, 255, 0.06); border: 1px solid rgba(255, 255, 255, 0.14); border-radius: 6px; color: #e2e8f0; font-size: 12px; font-weight: 500; padding: 6px 12px; line-height: 16px; cursor: pointer; flex: 1 1 auto; text-align: center;">Discard changes</button>' +
                    '<span style="color: #64748b; font-size: 11px; margin-left: auto; white-space: nowrap; flex-shrink: 0;">Ctrl+S</span>' +
                '</div>' +
                '<button id="sls-cfg-restart-btn" class="sls-cfg-btn" type="button" style="background: rgba(255, 77, 77, 0.12); border: 1px solid rgba(255, 77, 77, 0.3); color: #ff6b6b; width: 100%; text-align: center; border-radius: 6px; padding: 8px 12px; font-size: 12px; margin-top: 4px;">Restart Steam</button>' +
            '</div>' +
        '</div>';

        var dialogContentHtml = '<div class="DialogContent _DialogLayout" style="background-color: #14171d; flex: 1; height: 100%; padding: 0; display: flex; flex-direction: column; box-sizing: border-box; overflow: hidden; min-width: 0;">' +
            '<div id="sls-cfg-status-banner" role="status" aria-live="polite" style="display: none; margin: 16px 32px 0 32px; max-width: 980px; border-radius: 2px; padding: 10px 14px; font-size: 13px; font-weight: 500; border: 1px solid transparent; box-sizing: border-box;"></div>' +
            '<div class="sls-cfg-dialog-body" style="overflow-y: auto; padding: 24px 32px 48px 32px; width: 100%; max-width: 100%; flex: 1; display: flex; flex-direction: column; box-sizing: border-box;">' +
                headerHtml +
                sec1Html +
                sec2Html +
                sec3Html +
                sec4Html +
                sec5Html +
                sec6Html +
                sec7Html +
            '</div>' +
        '</div>';

    return {
        sidebarHtml: sidebarHtml,
        dialogContentHtml: dialogContentHtml
    };
}
