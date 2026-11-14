/**
 * @file  config-helpers.js
 * @brief HTML generation helper components for Navi configuration sections and cards
 */
function makeCard(title, subtitle, contentHtml, opts) {
            opts = opts || {};
            return '<div class="sls-cfg-card DialogControlsSection' + (opts.className ? ' ' + opts.className : '') + '" style="background: #1c202a; border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 8px; padding: 18px 20px; margin-top: 18px; box-sizing: border-box;">' +
                (title ? '<div style="color: #ffffff; font-size: 16px; font-weight: 600; line-height: 22px; margin-bottom: 4px;">' + title + '</div>' : '') +
                (subtitle ? '<div style="color: #8a929e; font-size: 13px; font-weight: 400; line-height: 18px; margin-bottom: 12px;">' + subtitle + '</div>' : '') +
                contentHtml +
            '</div>';
        }

        function makeSettingRow(label, desc, controlHtml, searchTerms, opts) {
            opts = opts || {};
            var errorHtml = opts.errorFor ? '<div class="sls-cfg-inline-error" id="sls-cfg-err-' + opts.errorFor + '" role="alert"></div>' : '';
            return '<div class="sls-cfg-item" data-search="' + escapeHtml((label + ' ' + (desc || '') + ' ' + (searchTerms || '')).toLowerCase()) + '" style="background-color: #171a22; border: 1px solid rgba(255, 255, 255, 0.04); padding: 14px 18px; margin-top: 8px; border-radius: 6px; display: flex; flex-direction: row; align-items: center; justify-content: space-between; gap: 14px; min-height: 44px; box-sizing: border-box; flex-wrap: wrap;">' +
                '<div style="flex: 1 1 260px; min-width: 220px; padding-right: 16px;">' +
                    '<div style="font-size: 14px; font-weight: 500; color: #ffffff; line-height: 19px;">' + label + '</div>' +
                    (desc ? '<div style="font-size: 12.5px; font-weight: 400; color: #8a929e; line-height: 17px; margin-top: 3px;">' + desc + '</div>' : '') +
                '</div>' +
                '<div style="flex: 0 1 auto; display: flex; align-items: center; justify-content: flex-end; gap: 8px; flex-wrap: wrap; max-width: 100%;">' +
                    controlHtml + errorHtml +
                '</div>' +
            '</div>';
        }

        function makeToggle(id, checked) {
            return '<label class="sls-toggle-switch">' +
                '<input type="checkbox" id="' + id + '" ' + (checked ? 'checked' : '') + '>' +
                '<span class="sls-toggle-slider"></span>' +
            '</label>';
        }

        function formatArrayForEditor(arr) {
            return (arr || []).join(', ');
        }

        function formatKvForEditor(m) {
            var lines = [];
            for (var k in (m || {})) {
                if (Object.prototype.hasOwnProperty.call(m, k)) {
                    lines.push(k + ': ' + m[k]);
                }
            }
            return lines.join('\n');
        }

        function makeTextareaItem(id, label, desc, content, placeholder, rows, opts) {
            opts = opts || {};
            var searchRaw = label + ' ' + (desc || '') + ' ' + id + (opts.searchTerms ? ' ' + opts.searchTerms : '');
            var btnHtml = (opts.addBtnId && opts.addBtnLabel)
                ? '<button id="' + opts.addBtnId + '" type="button" class="sls-cfg-btn" style="padding: 5px 12px; font-size: 12px; flex-shrink: 0; margin-left: 8px; white-space: nowrap;">' + escapeHtml(opts.addBtnLabel) + '</button>'
                : '';
            return '<div class="sls-cfg-item" data-search="' + escapeHtml(searchRaw.toLowerCase()) + '" style="background-color: #171a22; border: 1px solid rgba(255, 255, 255, 0.04); padding: 14px 18px; margin-top: 8px; border-radius: 6px; display: flex; flex-direction: column; align-items: stretch; gap: 6px;">' +
                '<div style="display: flex; justify-content: space-between; align-items: flex-start; gap: 16px; width: 100%; margin-bottom: 6px;">' +
                    '<div style="flex: 1 1 auto; min-width: 0;">' +
                        '<div style="font-size: 14px; font-weight: 500; color: #ffffff; line-height: 19px;">' + label + '</div>' +
                        (desc ? '<div style="font-size: 12.5px; color: #8a929e; line-height: 17px; margin-top: 3px;">' + desc + '</div>' : '') +
                    '</div>' +
                    btnHtml +
                '</div>' +
                '<textarea id="' + id + '" class="sls-cfg-textarea" rows="' + (rows || 3) + '" placeholder="' + escapeHtml(placeholder || '') + '">' + escapeHtml(content) + '</textarea>' +
                (id === 'sls-cfg-AppIds' || id === 'sls-cfg-AdditionalApps' || id === 'sls-cfg-FakeOffline' || id === 'sls-cfg-DepotBlacklist'
                    ? '<div class="sls-cfg-inline-error" id="sls-cfg-err-' + id + '" role="alert"></div>'
                    : '') +
            '</div>';
        }

        function makeApiKeyRow(keyName, label, desc, currentVal, placeholder, bannerId, providerType) {
            var bannerHtml = bannerId ? '<div id="' + bannerId + '" style="display: none; width: 100%; background: rgba(239, 68, 68, 0.12); border: 1px solid rgba(239, 68, 68, 0.4); border-radius: 6px; padding: 10px 14px; margin-top: 8px; color: #fca5a5; font-size: 12px; align-items: center; justify-content: space-between; gap: 10px; flex-wrap: wrap;">' +
                '<div style="display: flex; align-items: center; gap: 8px;">' +
                    '<span style="font-size: 14px;">⚠️</span>' +
                    '<span>' + label + ' was not accepted by the provider. Copy the full key and paste it again.</span>' +
                '</div>' +
                '<button type="button" class="sls-cfg-clear-banner-btn" data-input="sls-cfg-' + keyName + '" data-banner="' + bannerId + '" style="background: none; border: none; color: #f87171; text-decoration: underline; cursor: pointer; padding: 0; font-size: 12px;">Remove key</button>' +
            '</div>' : '';

            return '<div class="sls-cfg-item" data-search="' + escapeHtml(keyName.toLowerCase() + ' ' + label.toLowerCase() + ' ' + desc.toLowerCase()) + '" style="background-color: #171a22; border: 1px solid rgba(255, 255, 255, 0.04); padding: 14px 18px; margin-top: 8px; border-radius: 6px; display: flex; flex-direction: column; gap: 8px;">' +
                '<div style="display: flex; justify-content: space-between; align-items: center; width: 100%; flex-wrap: wrap; gap: 12px;">' +
                    '<div style="flex: 1 1 240px;">' +
                        '<div style="display: flex; align-items: center; gap: 8px;">' +
                            '<span style="font-size: 14px; font-weight: 500; color: #ffffff;">' + label + '</span>' +
                            '<span style="background: rgba(255, 255, 255, 0.08); color: #8a929e; font-size: 10.5px; padding: 2px 6px; border-radius: 3px;">Optional</span>' +
                        '</div>' +
                        (desc ? '<div style="font-size: 12.5px; color: #8a929e; margin-top: 3px;">' + desc + '</div>' : '') +
                    '</div>' +
                    '<div style="display: gap; gap: 8px; align-items: center; flex-wrap: wrap; display: flex;">' +
                        '<input type="password" id="sls-cfg-' + keyName + '" class="sls-cfg-input sls-cfg-provider-key" data-provider="' + (providerType || '') + '" data-banner="' + (bannerId || '') + '" style="width: 260px;" value="' + escapeHtml(currentVal || '') + '" placeholder="' + escapeHtml(placeholder || '') + '">' +
                        '<button id="sls-cfg-toggle-' + keyName + '" type="button" class="sls-cfg-btn" style="padding: 6px 12px; font-size: 12px;">Show</button>' +
                    '</div>' +
                '</div>' +
                bannerHtml +
            '</div>';
        }

        function sectionHeader(title, desc) {
            return '';
        }

        function makeSection(id, title, desc, bodyHtml, visible) {
            return '<div id="' + id + '" class="sls-cfg-section" style="display: ' + (visible ? 'block' : 'none') + '; max-width: 980px; width: 100%; box-sizing: border-box;">' +
                bodyHtml +
            '</div>';
        }

        function providerLabel(prov) {
            if (prov === 'gdrive') return 'Google Drive';
            if (prov === 'onedrive') return 'Microsoft OneDrive';
            if (prov === 'r2') return 'Cloudflare R2';
            if (prov === 's3') return 'Amazon S3';
            return 'This computer (offline)';
        }

        function resolveVersionLabel() {
            var el = document.querySelector('[data-navi-version]');
            var raw = '';
            if (el) {
                raw = (el.getAttribute('data-navi-version') || '').trim();
                if (!raw) raw = (el.textContent || '').trim();
            }
            raw = raw.replace(/·.*$/, '').trim();
            if (!raw) return '';
            if (raw.toLowerCase().indexOf('navi') === 0) return raw;
            return 'Navi ' + raw;
        }
