(function () {
    'use strict';
    if (window.__slsImportLuaInjected) return;
    window.__slsImportLuaInjected = true;

    function log(m) {
        console.log('[SLS] ' + m);
        fetch('http://127.0.0.1:9001/log?msg=' + encodeURIComponent('[RemoveLua] ' + m)).catch(function () { });
    }

    // Dynamically load JSZip library for client-side zip extraction
    var _jszipPromise = null;
    function loadJSZip() {
        if (typeof JSZip !== 'undefined') return Promise.resolve(JSZip);
        if (_jszipPromise) return _jszipPromise;
        _jszipPromise = new Promise(function (resolve, reject) {
            var script = document.createElement('script');
            script.src = 'https://cdnjs.cloudflare.com/ajax/libs/jszip/3.10.1/jszip.min.js';
            script.onload = function () {
                if (typeof JSZip !== 'undefined') {
                    log('JSZip loaded successfully');
                    resolve(JSZip);
                } else {
                    reject(new Error('JSZip not available after script load'));
                }
            };
            script.onerror = function () {
                _jszipPromise = null;
                reject(new Error('Failed to load JSZip from CDN'));
            };
            document.head.appendChild(script);
        });
        return _jszipPromise;
    }

    // Known Steam CSS class names (hashed, from CEF inspection)
    var STEAM_NAV_CONTAINER_CLASS = '_2D64jIEK7wpUR_NlObDW76';
    var STEAM_TAB_BASE_CLASS = '_2Lu3d-5qLmW4i19ysTt2jT';
    var STEAM_TAB_ACTIVE_CLASS = '_1gqEjB5QsKT_NftD1dEsdZ';
    var STEAM_TAB_TEXT_CLASS = '_19axKcqYRuaJ8vdYKYmtTQ';

    function findSuperNavContainer() {
        // Primary: use the known hashed class
        var container = document.querySelector('.' + STEAM_NAV_CONTAINER_CLASS);
        if (container) return container;

        // Fallback: search for an element whose direct DIV children contain Store/Library/Community text
        var divs = document.querySelectorAll('div');
        for (var i = 0; i < divs.length; i++) {
            var el = divs[i];
            if (el.children.length >= 4 && el.children.length <= 12) {
                var text = (el.textContent || '').toUpperCase();
                if (text.indexOf('STORE') !== -1 && text.indexOf('LIBRARY') !== -1 && text.indexOf('COMMUNITY') !== -1) {
                    var rect = el.getBoundingClientRect();
                    if (rect.top >= 0 && rect.top < 150 && rect.height > 15 && rect.height < 100) {
                        return el;
                    }
                }
            }
        }
        return null;
    }

    function getTopBarHeight() {
        // Use the outer wrapper of the nav bar (_3Z3ohQ8-1NKnCZkbS6fvy) 
        var navContainer = findSuperNavContainer();
        if (navContainer) {
            // Walk up to find the full top bar area
            var el = navContainer;
            while (el && el !== document.body) {
                var rect = el.getBoundingClientRect();
                if (rect.top === 0 && rect.height > 30 && rect.height < 150) {
                    return rect.height;
                }
                el = el.parentElement;
            }
            return navContainer.getBoundingClientRect().bottom;
        }
        return 66;
    }

    function setActiveTabStyle(activeTab) {
        var container = findSuperNavContainer();
        if (!container) return;

        // Remove active class from all sibling tabs
        var tabs = container.querySelectorAll('.' + STEAM_TAB_BASE_CLASS);
        for (var i = 0; i < tabs.length; i++) {
            tabs[i].classList.remove(STEAM_TAB_ACTIVE_CLASS);
        }

        // Add active class to the target tab
        if (activeTab) {
            activeTab.classList.add(STEAM_TAB_ACTIVE_CLASS);
        }
    }

    function getSteamHistory() {
        // Extract React Router history from the fiber tree of a tab element
        var container = findSuperNavContainer();
        if (!container) return null;
        var tabEl = container.querySelector('.' + STEAM_TAB_BASE_CLASS);
        if (!tabEl) return null;
        var reactFiberKey = null;
        for (var key in tabEl) {
            if (key.indexOf('__reactFiber') === 0 || key.indexOf('__reactInternalInstance') === 0) {
                reactFiberKey = key;
                break;
            }
        }
        if (!reactFiberKey) return null;
        var fiber = tabEl[reactFiberKey];
        while (fiber) {
            if (fiber.memoizedProps && fiber.memoizedProps.history) {
                return fiber.memoizedProps.history;
            }
            fiber = fiber.return;
        }
        return null;
    }

    var historyUnlisten = null;
    var configKeyDownHandler = null;

    var activeSlsAppId = null;

    function escapeHtml(str) {
        if (str === null || typeof str === 'undefined') return '';
        return String(str)
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;')
            .replace(/'/g, '&#39;');
    }

    function ensureCustomTabStyles() {
        if (document.getElementById('sls-custom-tab-styles')) return;
        var styleEl = document.createElement('style');
        styleEl.id = 'sls-custom-tab-styles';
        styleEl.innerHTML =
            '@keyframes sls-spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }' +
            '@keyframes sls-pulse { 0%, 100% { opacity: 1; transform: scale(1); } 50% { opacity: 0.5; transform: scale(1.15); } }' +
            'body:has(#sls-top-tab.' + STEAM_TAB_ACTIVE_CLASS + ', #sls-config-top-tab.' + STEAM_TAB_ACTIVE_CLASS + ') .' + STEAM_TAB_BASE_CLASS + ':not(#sls-top-tab):not(#sls-config-top-tab) .' + STEAM_TAB_TEXT_CLASS + ' {' +
            '    color: rgb(150, 150, 150) !important;' +
            '    text-shadow: none !important;' +
            '    font-weight: normal !important;' +
            '}' +
            'body:has(#sls-top-tab.' + STEAM_TAB_ACTIVE_CLASS + ', #sls-config-top-tab.' + STEAM_TAB_ACTIVE_CLASS + ') .' + STEAM_TAB_BASE_CLASS + ':not(#sls-top-tab):not(#sls-config-top-tab) .' + STEAM_TAB_TEXT_CLASS + '::after {' +
            '    display: none !important;' +
            '}' +
            'details summary::-webkit-details-marker { display: none !important; }' +
            'details summary { list-style: none !important; }' +
            'details .sls-lua-chevron { transition: transform 0.2s ease; color: rgb(139, 146, 154); }' +
            'details[open] .sls-lua-chevron { transform: rotate(180deg); color: rgb(26, 159, 255); }' +
            '.sls-cfg-pill { background: transparent; border: none; border-radius: 0; color: rgb(184, 188, 191); padding: 10px 12px 10px 24px; height: 40px; box-sizing: border-box; font-family: "Motiva Sans", Arial, Helvetica, sans-serif; font-size: 14px; font-weight: 400; line-height: 16px; cursor: pointer; display: flex; flex-direction: row; align-items: center; width: 100%; text-align: left; transition: background-color 0.1s ease, color 0.1s ease; user-select: none; outline: none; }' +
            '.sls-cfg-pill:hover { background-color: rgba(61, 68, 80, 0.5); color: rgb(255, 255, 255); }' +
            '.sls-cfg-pill.active { background-color: rgb(61, 68, 80); color: rgb(255, 255, 255); }' +
            '.sls-toggle-switch { position: relative; display: inline-block; width: 38px; height: 22px; flex-shrink: 0; }' +
            '.sls-toggle-switch input { opacity: 0; width: 0; height: 0; }' +
            '.sls-toggle-slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: rgba(255, 255, 255, 0.15); transition: .2s; border-radius: 9001px; }' +
            '.sls-toggle-slider:before { position: absolute; content: ""; height: 22px; width: 22px; left: 0px; bottom: 0px; background-color: rgb(255, 255, 255); transition: .2s; border-radius: 9001px; box-shadow: rgba(0, 0, 0, 0.35) 0px 0px 5px 0px; }' +
            '.sls-toggle-switch input:checked + .sls-toggle-slider { background-color: rgb(26, 159, 255); }' +
            '.sls-toggle-switch input:checked + .sls-toggle-slider:before { transform: translateX(16px); }' +
            ':focus-visible { outline: 2px solid rgb(26, 159, 255) !important; outline-offset: 2px !important; }' +
            '.sls-card-selectable { border: 1px solid rgba(255,255,255,0.1); background: rgba(255,255,255,0.03); border-radius: 4px; padding: 14px 16px; cursor: pointer; transition: all 0.2s ease; user-select: none; min-height: 48px; display: flex; flex-direction: column; justify-content: center; }' +
            '.sls-card-selectable:hover { border-color: rgba(26, 159, 255, 0.4); background: rgba(26, 159, 255, 0.06); }' +
            '.sls-card-selectable.active { border-color: rgb(26, 159, 255) !important; background: rgba(26, 159, 255, 0.14) !important; }' +
            '.sls-preset-chip { background: rgba(59, 63, 72, 0.5); border: 1px solid rgba(255, 255, 255, 0.08); border-radius: 2px; color: rgb(184, 188, 191); padding: 6px 12px; font-size: 12px; font-weight: 500; cursor: pointer; transition: all 0.15s; min-height: 32px; display: inline-flex; align-items: center; }' +
            '.sls-preset-chip:hover { background: rgba(103, 112, 123, 0.5); color: #fff; }' +
            '.sls-preset-chip.active { background: rgba(26, 159, 255, 0.2); border-color: rgb(26, 159, 255); color: rgb(26, 159, 255); font-weight: 700; }' +
            '.sls-step-node { display: flex; align-items: center; gap: 8px; flex: 1; min-width: 120px; }' +
            '.sls-step-circle { width: 24px; height: 24px; border-radius: 50%; display: flex; align-items: center; justify-content: center; font-size: 11px; font-weight: 700; background: rgba(255, 255, 255, 0.08); color: rgb(150, 150, 150); flex-shrink: 0; }' +
            '.sls-step-node.active .sls-step-circle { background: rgb(26, 159, 255); color: #fff; }' +
            '.sls-step-node.completed .sls-step-circle, .sls-step-node.done .sls-step-circle { background: #2ecc71; color: #fff; }' +
            '.sls-cfg-input { background-color: rgba(59, 63, 72, 0.5); border: none; color: rgb(223, 227, 230); border-radius: 3px; padding: 7px 12px; font-family: "Motiva Sans", Arial, Helvetica, sans-serif; font-size: 13px; font-weight: 400; line-height: 18px; height: 34px; min-height: 34px; outline: none; box-sizing: border-box; transition: background-color 0.2s; }' +
            '.sls-cfg-input:focus { background-color: rgba(59, 63, 72, 0.8); box-shadow: 0 0 0 1px rgb(26, 159, 255); }' +
            '.sls-cfg-textarea { background-color: rgba(59, 63, 72, 0.5); border: none; color: rgb(223, 227, 230); border-radius: 3px; padding: 8px 12px; font-family: monospace; font-size: 12px; line-height: 1.5; resize: vertical; outline: none; transition: background-color 0.2s; box-sizing: border-box; width: 100%; }' +
            '.sls-cfg-textarea:focus { background-color: rgba(59, 63, 72, 0.8); box-shadow: 0 0 0 1px rgb(26, 159, 255); }' +
            '.sls-cfg-btn { background-color: rgba(59, 63, 72, 0.5); border: none; border-radius: 2px; color: rgb(223, 227, 230); padding: 8px 14px; font-family: "Motiva Sans", Arial, Helvetica, sans-serif; font-size: 13px; font-weight: 400; line-height: 18px; cursor: pointer; transition: background-color 0.15s ease, color 0.15s ease; outline: none; }' +
            '.sls-cfg-btn:hover { background-color: rgba(103, 112, 123, 0.5); color: #fff; }' +
            '.sls-cfg-btn:active { background-color: rgba(103, 112, 123, 0.8); }' +
            '.sls-cfg-section { max-width: 980px; width: 100%; box-sizing: border-box; }' +
            '.sls-cfg-dialog-body::-webkit-scrollbar { width: 12px; }' +
            '.sls-cfg-dialog-body::-webkit-scrollbar-track { background: rgba(0, 0, 0, 0.1); }' +
            '.sls-cfg-dialog-body::-webkit-scrollbar-thumb { background: rgba(255, 255, 255, 0.15); border-radius: 6px; border: 3px solid transparent; background-clip: content-box; }' +
            '.sls-cfg-dialog-body::-webkit-scrollbar-thumb:hover { background: rgba(255, 255, 255, 0.25); border: 3px solid transparent; background-clip: content-box; }';
        document.head.appendChild(styleEl);
    }

    function showSlsPage() {
        hideConfigPage();
        ensureCustomTabStyles();

        // Use Steam's React Router to navigate to /library/home which hides the browser overlay
        var history = getSteamHistory();
        if (history) {
            window.__slsNavigating = true;
            history.push('/library/home');
        }

        var container = document.getElementById('sls-page-container');
        if (!container) {
            container = document.createElement('div');
            container.id = 'sls-page-container';
            container.style.cssText = 'position: fixed; left: 0; right: 0; bottom: 0; z-index: 999999; background: radial-gradient(circle at top, #1b2838 0%, #0d121a 100%); display: none; overflow-y: auto; font-family: "Motiva Sans", Arial, Helvetica, sans-serif; color: #d6d7d9; padding: 40px 60px;';
            document.body.appendChild(container);
        }

        container.innerHTML = '<div style="max-width: 1000px; margin: 0 auto;">' +
            '<div style="margin-bottom: 40px; display: flex; align-items: center; justify-content: space-between; border-bottom: 1px solid rgba(255,255,255,0.06); padding-bottom: 24px;">' +
            '<div>' +
            '<h1 style="margin: 0; font-size: 32px; font-weight: 800; background: linear-gradient(90deg, #66c0f4 0%, #1a9fff 100%); -webkit-background-clip: text; -webkit-text-fill-color: transparent; letter-spacing: -0.5px;">Manual Installation</h1>' +
            '<p style="margin: 8px 0 0; font-size: 14px; color: #8f98a0;">Manually install Lua plugins and Manifest depotcaches for any Steam game.</p>' +
            '</div>' +
            '</div>' +
            '<div style="display: flex; flex-direction: column; gap: 0;">' +
            // Single Column: Upload Zone, File List, AppID, and Install
            '<div style="width: 100%; box-sizing: border-box; background: rgba(255, 255, 255, 0.02); border: 1px solid rgba(255, 255, 255, 0.05); border-radius: 16px; padding: 32px; display: flex; flex-direction: column; overflow: hidden;">' +
            '<div id="sls-manual-dropzone" style="min-height: 180px; border: 2px dashed rgba(255, 255, 255, 0.15); border-radius: 12px; background: rgba(0,0,0,0.15); display: flex; flex-direction: column; align-items: center; justify-content: center; padding: 30px; text-align: center; cursor: pointer; transition: all 0.2s;">' +
            '<svg style="width: 48px; height: 48px; color: #66c0f4; margin-bottom: 16px;" fill="none" stroke="currentColor" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">' +
            '<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M7 16a4 4 0 01-.88-7.903A5 5 0 1115.9 6L16 6a5 5 0 011 9.9M15 13l-3-3m0 0l-3 3m3-3v12"></path>' +
            '</svg>' +
            '<p style="margin: 0; font-size: 15px; font-weight: 600; color: #fff;">Supports .zip, .lua, .manifest, or entire folders</p>' +
            '<div style="margin-top: 20px; display: flex; gap: 10px;">' +
            '<button id="sls-manual-select-files" style="background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.1); border-radius: 6px; color: #fff; padding: 8px 16px; font-size: 13px; font-weight: 600; cursor: pointer; transition: background 0.2s; outline: none;">' +
            'Select Files' +
            '</button>' +
            '<button id="sls-manual-select-folder" style="background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.1); border-radius: 6px; color: #fff; padding: 8px 16px; font-size: 13px; font-weight: 600; cursor: pointer; transition: background 0.2s; outline: none;">' +
            'Select Folder' +
            '</button>' +
            '</div>' +
            '</div>' +
            '<div id="sls-manual-file-list" style="max-height: 480px; overflow-y: auto; display: none; flex-direction: column; gap: 8px; border-bottom: 1px solid rgba(255,255,255,0.06); padding-bottom: 16px; margin-top: 24px;">' +
            '</div>' +
            '<div style="margin-top: 24px;">' +
            '<div id="sls-manual-status" role="status" aria-live="polite" style="margin-bottom: 12px; font-size: 14px; text-align: center; color: #8f98a0; font-weight: 600;">' +
            '</div>' +
            '<button id="sls-manual-submit" disabled style="width: 100%; background: rgba(255, 255, 255, 0.05); border: none; border-radius: 8px; color: rgba(255,255,255,0.3); padding: 16px; font-size: 15px; font-weight: 700; cursor: not-allowed; outline: none;">' +
            'Import' +
            '</button>' +
            '</div>' +
            '</div>' +
            '</div>' +
            '</div>';

        initManualInstallPage();

        container.style.top = getTopBarHeight() + 'px';
        container.style.display = 'block';

        // Register history listener to auto-hide when user navigates away
        if (!historyUnlisten) {
            var history = getSteamHistory();
            if (history) {
                historyUnlisten = history.listen(function (location) {
                    if (window.__slsNavigating) {
                        window.__slsNavigating = false;
                        return;
                    }
                    hideSlsPage();
                    hideConfigPage();
                });
            }
        }

        var slsTab = document.getElementById('sls-top-tab');
        if (slsTab) {
            setActiveTabStyle(slsTab);
        }
    }

    function hideSlsPage() {
        var container = document.getElementById('sls-page-container');
        if (container) {
            container.style.display = 'none';
        }
        var slsTab = document.getElementById('sls-top-tab');
        if (slsTab) {
            slsTab.classList.remove(STEAM_TAB_ACTIVE_CLASS);
        }
    }

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

        var history = getSteamHistory();
        if (history) {
            window.__slsNavigating = true;
            history.push('/library/home');
        }

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

        if (!historyUnlisten) {
            var hist = getSteamHistory();
            if (hist) {
                historyUnlisten = hist.listen(function (location) {
                    if (window.__slsNavigating) {
                        window.__slsNavigating = false;
                        return;
                    }
                    hideSlsPage();
                    hideConfigPage();
                });
            }
        }

        var configTab = document.getElementById('sls-config-top-tab');
        if (configTab) {
            setActiveTabStyle(configTab);
        }

        container.innerHTML = '<div style="display: flex; width: 100%; height: 100%; align-items: center; justify-content: center; flex-direction: column;">' +
            '<div style="font-size: 18px; font-weight: 700; color: rgb(26, 159, 255); margin-bottom: 8px;">Loading Navi Configuration...</div>' +
            '<div style="font-size: 13px; color: rgb(139, 146, 154);">Connecting to Navi backend (http://127.0.0.1:9001)...</div>' +
            '</div>';

        fetch('http://127.0.0.1:9001/config/full')
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

        function makeCard(title, subtitle, contentHtml) {
            return '<div class="DialogControlsSection" style="margin-top: 20px;">' +
                (title ? '<div style="color: rgb(255, 255, 255); font-size: 14px; font-weight: 600; text-transform: uppercase; letter-spacing: 0.5px; margin-bottom: 4px;">' + title + '</div>' : '') +
                (subtitle ? '<div style="color: rgb(139, 146, 154); font-size: 13px; font-weight: 400; line-height: 18px; margin-bottom: 8px;">' + subtitle + '</div>' : '') +
                contentHtml +
            '</div>';
        }

        function makeSettingRow(label, desc, controlHtml, searchTerms) {
            return '<div class="sls-cfg-item" data-search="' + escapeHtml((label + ' ' + (desc || '') + ' ' + (searchTerms || '')).toLowerCase()) + '" style="background-color: rgb(35, 38, 46); padding: 12px 16px; margin-top: 6px; border-radius: 2px; display: flex; flex-direction: row; align-items: center; justify-content: space-between; gap: 12px; min-height: 40px; box-sizing: border-box; flex-wrap: wrap;">' +
                '<div style="flex: 1 1 260px; min-width: 220px; padding-right: 16px;">' +
                    '<div style="font-size: 14px; font-weight: 300; color: rgb(220, 222, 223); line-height: 18px;">' + label + '</div>' +
                    (desc ? '<div style="font-size: 13px; font-weight: 400; color: rgb(139, 146, 154); line-height: 18px; margin-top: 4px;">' + desc + '</div>' : '') +
                '</div>' +
                '<div style="flex: 0 1 auto; display: flex; align-items: center; justify-content: flex-end; gap: 8px; flex-wrap: wrap;">' +
                    controlHtml +
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

        var cs = cfg.CloudSaves || {};

        var card1Content = makeSettingRow('Master Cloud Saves', 'Enable or disable save redirection globally (requires Steam restart to mount/unmount virtual save layer)', '<button id="sls-cfg-cs-master-btn" class="Focusable" tabindex="-1" role="button" style="height:34px;padding:0 18px;border-radius:2px;border:none;font-weight:600;font-size:13px;cursor:pointer;">Checking...</button>', 'Master Cloud Saves CloudRedirect') +
            makeSettingRow('Subsystem Health', 'Real-time daemon and operational queue metrics', '<div style="display:flex;align-items:center;gap:10px;"><span id="sls-cfg-cs-engine-badge" role="status" aria-live="polite" style="padding:3px 8px;border-radius:2px;font-size:12px;font-weight:600;background:rgba(172,178,201,0.1);color:#9ca3af;">Checking...</span><span id="sls-cfg-cs-managed-badge" style="padding:3px 8px;border-radius:2px;font-size:12px;background:rgba(26,159,255,0.15);color:#66c0f4;border:1px solid rgba(26,159,255,0.3);">0 Games Managed</span><span id="sls-cfg-cs-pending-badge" style="padding:3px 8px;border-radius:2px;font-size:12px;display:none;"></span></div>', 'status health metrics') +
            '<div id="sls-cfg-cs-pending-callout" style="display:none;background:linear-gradient(90deg, #78350f, #92400e);border:1px solid #f59e0b;border-radius:3px;padding:10px 14px;margin-top:10px;color:#fef3c7;font-size:12px;align-items:center;justify-content:space-between;gap:10px;"><span>⚠️ Pending cloud operations waiting for Steam restart.</span><button id="sls-cfg-cs-restart-btn" class="sls-cfg-btn" style="background:#f59e0b;color:#1e1e1e;font-weight:700;padding:4px 12px;font-size:12px;">Restart Steam</button></div>' +
            '<div style="background: rgba(26, 159, 255, 0.08); border: 1px solid rgba(26, 159, 255, 0.2); border-radius: 2px; padding: 10px 14px; font-size: 12px; color: rgb(220, 222, 223); margin-top: 10px;">💡 <b>Pro-Tip:</b> Enable <b>Disable Steam Cloud (Valve)</b> under General &amp; Unlock to suppress Valve remote conflict popups. <a href="#" id="sls-cfg-link-to-general" style="color: rgb(26, 159, 255); text-decoration: underline; margin-left: 4px;">Go to General &amp; Unlock &rarr;</a></div>';

        var providerOptions = [
            { val: 'local-only', label: 'Local Diverted Storage (Offline)' },
            { val: 'gdrive', label: 'Google Drive' },
            { val: 'onedrive', label: 'Microsoft OneDrive' },
            { val: 'r2', label: 'Cloudflare R2' },
            { val: 's3', label: 'Amazon S3' }
        ].map(function (opt) {
            return '<option value="' + opt.val + '"' + ((cs.Provider || 'local-only') === opt.val ? ' selected' : '') + '>' + opt.label + '</option>';
        }).join('');

        var card2Content = makeSettingRow('Cloud Storage Provider', 'Select offline diverted storage or connect remote cloud providers',
            '<select id="sls-cfg-cs-provider" class="sls-cfg-input" style="width: 260px;">' + providerOptions + '</select>', 'provider cloud storage') +
            '<div id="sls-cfg-cs-auth-block" style="background-color: rgb(35, 38, 46); padding: 12px 16px; margin-top: 6px; border-radius: 2px;">' +
                '<div style="display:flex; align-items:center; justify-content:space-between; flex-wrap:wrap; gap:10px;">' +
                    '<div style="display:flex; align-items:center; gap:10px;">' +
                        '<span id="sls-cfg-cs-auth-badge" style="padding: 3px 8px; border-radius: 2px; font-size: 11px; font-weight: 700; text-transform: uppercase; background: rgba(255,255,255,0.06); color: rgb(139, 146, 154); border: 1px solid rgba(255,255,255,0.12);">Offline</span>' +
                        '<span id="sls-cfg-cs-auth-email" style="font-size: 12px; color: rgb(220, 222, 223); display: none;"></span>' +
                    '</div>' +
                    '<div id="sls-cfg-cs-auth-actions" style="display:flex; gap:8px;"></div>' +
                    '<div id="sls-cfg-cs-auth-actions" style="display:flex; gap:8px; align-items:center;">' +
                        '<button id="sls-cfg-cs-connect-btn" class="sls-cfg-btn" style="background: #2563eb; border: 1px solid #3b82f6; color: #ffffff; padding: 6px 14px; font-size: 12px; font-weight: 600; cursor: pointer; border-radius: 2px;">Login</button>' +
                        '<button id="sls-cfg-cs-disconnect-btn" class="sls-cfg-btn" style="background: rgba(239, 68, 68, 0.15); border: 1px solid rgba(239, 68, 68, 0.35); color: #f87171; padding: 6px 14px; font-size: 12px; font-weight: 600; cursor: pointer; border-radius: 2px;">Sign Out</button>' +
                    '</div>' +
                '</div>' +
                '<div id="sls-cfg-cs-local-info" style="font-size: 12px; color: rgb(139, 146, 154); margin-top: 8px;">' +
                    'Saves are stored locally in diverted paths (&lt;SteamRoot&gt;/cloud_redirect/storage/). No cloud accounts or network sync needed.' +
                '</div>' +
            '</div>';

        var card3Content = makeSettingRow('Auto-Backup Before Reset', 'Create an automatic tar.gz backup before any game save reset operation',
            makeToggle('sls-cfg-CloudAutoBackup', cs.AutoBackupBeforeReset !== false), 'AutoBackupBeforeReset CloudAutoBackup') +
            makeSettingRow('Backup Retention Count', 'Maximum number of automatic save snapshots to retain per game (1 to 100)',
                '<input type="number" id="sls-cfg-CloudRetention" class="sls-cfg-input" style="width: 120px;" min="1" max="100" value="' + (cs.BackupRetentionCount || 5) + '">', 'BackupRetentionCount CloudRetention');

        var card4Content = '<div style="background-color: rgb(35, 38, 46); padding: 12px 16px; margin-top: 6px; border-radius: 2px;">' +
            '<div style="display:flex; gap:8px; margin-bottom:12px; align-items:center;">' +
                '<input type="number" id="sls-cfg-add-cloud-app-input" class="sls-cfg-input" style="width: 180px;" placeholder="AppID (e.g. 480)">' +
                '<button id="sls-cfg-add-cloud-app-btn" class="sls-cfg-btn" style="padding: 7px 16px; font-size: 13px; color: #fff; background: #2563eb; cursor: pointer;">Add Game</button>' +
            '</div>' +
            '<div id="sls-cfg-managed-apps-list" style="max-height: 280px; overflow-y: auto; background: rgba(0,0,0,0.2); border: 1px solid rgba(172,178,201,0.1); border-radius: 3px; padding: 8px;"></div>' +
        '</div>';

        var sec1Html = '<div id="sls-cfg-sec-cloudsaves" class="sls-cfg-section" style="display: block; max-width: 980px; width: 100%; box-sizing: border-box;">' +
            '<div class="DialogHeader" style="color: rgb(255, 255, 255); font-size: 22px; font-weight: 700; line-height: 28px; margin: 8px 0px 12px 0px;">Cloud Saves (CloudRedirect)</div>' +
            '<div style="font-size: 13px; color: rgb(139, 146, 154); margin-bottom: 14px;">Intercepts Steam Cloud file I/O to redirect game save files into structured local storage or remote cloud providers for unowned and family-shared games.</div>' +
            makeCard('CloudRedirect Engine Status', 'Master toggle and runtime execution state', card1Content) +
            makeCard('Storage Provider & Credentials', 'Select offline diverted storage or connect remote cloud providers', card2Content) +
            makeCard('Backup & Safety Settings', 'Automatic safety snapshots before destructive operations', card3Content) +
            makeCard('Redirected Game Titles', 'Individual AppIDs with cloud redirection enabled', card4Content) +
        '</div>';

        // SECTION 2: Provider API Keys
        function makeApiKeyRow(keyName, label, desc, currentVal) {
            return makeSettingRow(label, desc,
                '<div style="display: flex; gap: 8px;">' +
                    '<input type="password" id="sls-cfg-' + keyName + '" class="sls-cfg-input" style="width: 280px;" value="' + escapeHtml(currentVal || '') + '" placeholder="API token or key...">' +
                    '<button id="sls-cfg-toggle-' + keyName + '" class="sls-cfg-btn" style="padding: 6px 12px; font-size: 12px;">Show</button>' +
                '</div>', keyName);
        }

        var apiCards = makeApiKeyRow('RyuuKey', 'Ryuu API Key', 'Access token for Ryuu manifest and depot index provider', cfg.RyuuKey) +
            makeApiKeyRow('DepotBoxKey', 'DepotBox API Key', 'Access key for DepotBox depot decryption key database', cfg.DepotBoxKey) +
            makeApiKeyRow('HubcapKey', 'Hubcap API Key', 'Authentication token for Hubcap community manifest mirror', cfg.HubcapKey);

        var sec2Html = '<div id="sls-cfg-sec-api" class="sls-cfg-section" style="display: none; max-width: 980px; width: 100%; box-sizing: border-box;">' +
            '<div class="DialogHeader" style="color: rgb(255, 255, 255); font-size: 22px; font-weight: 700; line-height: 28px; margin: 8px 0px 12px 0px;">Provider Keys</div>' +
            makeCard('External Service Credentials', 'Tokens used by Navi to query manifests and depot AES decryption keys', apiCards) +
        '</div>';

        // SECTION 3: General & Unlock Behavior
        var genCards = makeSettingRow('Disable Family Share Lock', 'Bypasses simultaneous library play locking for yourself and family share members',
            makeToggle('sls-cfg-DisableFamilyShareLock', cfg.DisableFamilyShareLock), 'DisableFamilyShareLock') +
            makeSettingRow('Use Whitelist Mode', 'Switches AppID filtering to whitelist mode instead of default blacklist mode',
                makeToggle('sls-cfg-UseWhitelist', cfg.UseWhitelist), 'UseWhitelist') +
            makeSettingRow('Disable Steam Cloud (Valve)', 'Suppresses Valve\'s native Steam Cloud sync for unlocked titles. Enable this alongside CloudRedirect to prevent cloud overwrites. <a href="#" id="sls-cfg-link-to-cloudsaves" style="color: rgb(26, 159, 255); text-decoration: underline; margin-left: 4px;">Configure Cloud Saves &rarr;</a>',
                makeToggle('sls-cfg-DisableCloud', cfg.DisableCloud), 'DisableCloud Steam Valve') +
            makeSettingRow('Disable Updates', 'Prevents automatic updates for titles specified in AdditionalApps list',
                makeToggle('sls-cfg-DisableUpdates', cfg.DisableUpdates), 'DisableUpdates') +
            makeSettingRow('Safe Mode', 'Automatically disables Navi injection when steamclient.so hash differs from known hashes',
                makeToggle('sls-cfg-SafeMode', cfg.SafeMode), 'SafeMode') +
            makeSettingRow('Warn Hash Mismatch', 'Display notification warning when steamclient.so binary hash differs from safe list',
                makeToggle('sls-cfg-WarnHashMissmatch', cfg.WarnHashMissmatch), 'WarnHashMissmatch') +
            makeSettingRow('Notify Initialization', 'Show a desktop/Steam notification when Navi has successfully initialized hooks',
                makeToggle('sls-cfg-NotifyInit', cfg.NotifyInit), 'NotifyInit') +
            makeSettingRow('Enable Local API', 'Enable receiving local command IPC via /tmp/navi.API FIFO pipe',
                makeToggle('sls-cfg-API', cfg.API), 'API') +
            makeSettingRow('Max Schema Tries', 'Maximum retry attempts when fetching Achievement schemas directly from Steam CDN (0 to disable)',
                '<input type="number" id="sls-cfg-MaxSchemaTries" class="sls-cfg-input" style="width: 120px;" min="0" max="100" value="' + (typeof cfg.MaxSchemaTries === 'number' ? cfg.MaxSchemaTries : 10) + '">', 'MaxSchemaTries');

        var sec3Html = '<div id="sls-cfg-sec-general" class="sls-cfg-section" style="display: none; max-width: 980px; width: 100%; box-sizing: border-box;">' +
            '<div class="DialogHeader" style="color: rgb(255, 255, 255); font-size: 22px; font-weight: 700; line-height: 28px; margin: 8px 0px 12px 0px;">General &amp; Unlock</div>' +
            makeCard('Unlock Policies & Security Options', 'Configure family sharing, safe mode, and schema retrieval rules', genCards) +
        '</div>';

        // SECTION 4: Profile, Status & Spoofing
        var idleStatus = cfg.IdleStatus || {};
        var unownedStatus = cfg.UnownedStatus || {};

        var profCards = makeSettingRow('Fake Persona Name', 'Clientside persona display name override (leave blank for native)',
            '<input type="text" id="sls-cfg-FakeName" class="sls-cfg-input" style="width: 280px;" value="' + escapeHtml(cfg.FakeName || '') + '" placeholder="e.g. Gordon Freeman">', 'FakeName') +
            makeSettingRow('Fake Account Email', 'Clientside account email override (leave blank to disable)',
                '<input type="text" id="sls-cfg-FakeEmail" class="sls-cfg-input" style="width: 280px;" value="' + escapeHtml(cfg.FakeEmail || '') + '" placeholder="e.g. user@example.com">', 'FakeEmail') +
            makeSettingRow('Fake Wallet Balance', 'Clientside wallet balance in currency units or cents (0 to disable)',
                '<input type="number" id="sls-cfg-FakeWalletBalance" class="sls-cfg-input" style="width: 180px;" value="' + (typeof cfg.FakeWalletBalance === 'number' ? cfg.FakeWalletBalance : 0) + '">', 'FakeWalletBalance') +
            makeSettingRow('Idle Status Override', 'Custom in-game presence displayed to friends when idle (AppID 0 to disable)',
                '<div style="display: flex; gap: 8px;">' +
                    '<input type="number" id="sls-cfg-IdleStatus-AppId" class="sls-cfg-input" style="width: 120px;" placeholder="AppID" value="' + (idleStatus.AppId || 0) + '">' +
                    '<input type="text" id="sls-cfg-IdleStatus-Title" class="sls-cfg-input" style="width: 220px;" placeholder="Title e.g. In Space" value="' + escapeHtml(idleStatus.Title || '') + '">' +
                '</div>', 'IdleStatus') +
            makeSettingRow('Unowned Status Override', 'Custom presence displayed when running unowned games (AppID 0 to disable)',
                '<div style="display: flex; gap: 8px;">' +
                    '<input type="number" id="sls-cfg-UnownedStatus-AppId" class="sls-cfg-input" style="width: 120px;" placeholder="AppID" value="' + (unownedStatus.AppId || 0) + '">' +
                    '<input type="text" id="sls-cfg-UnownedStatus-Title" class="sls-cfg-input" style="width: 220px;" placeholder="Title e.g. Testing" value="' + escapeHtml(unownedStatus.Title || '') + '">' +
                '</div>', 'UnownedStatus');

        var sec4Html = '<div id="sls-cfg-sec-profile" class="sls-cfg-section" style="display: none; max-width: 980px; width: 100%; box-sizing: border-box;">' +
            '<div class="DialogHeader" style="color: rgb(255, 255, 255); font-size: 22px; font-weight: 700; line-height: 28px; margin: 8px 0px 12px 0px;">Account &amp; Spoofing</div>' +
            makeCard('Persona & Presence Overrides', 'Clientside spoofing of account info, wallet balance, and rich presence', profCards) +
        '</div>';

        // SECTION 5: App Lists, DLC & Depot Overrides
        function makeTextareaItem(id, label, desc, content, placeholder, rows) {
            return '<div class="sls-cfg-item" data-search="' + escapeHtml(label.toLowerCase() + ' ' + (desc || '').toLowerCase() + ' ' + id.toLowerCase()) + '" style="background-color: rgb(35, 38, 46); padding: 12px 16px; margin-top: 6px; border-radius: 2px;">' +
                '<div style="font-size: 14px; font-weight: 300; color: rgb(220, 222, 223);">' + label + '</div>' +
                (desc ? '<div style="font-size: 13px; color: rgb(139, 146, 154); margin: 2px 0 8px;">' + desc + '</div>' : '<div style="margin-bottom: 6px;"></div>') +
                '<textarea id="' + id + '" class="sls-cfg-textarea" rows="' + (rows || 3) + '" placeholder="' + escapeHtml(placeholder || '') + '">' + escapeHtml(content) + '</textarea>' +
            '</div>';
        }

        var appListsContent = makeTextareaItem('sls-cfg-AppIds', 'AppIds', 'List of AppIDs to ex-/include individually (comma-separated or one per line)', formatArrayForEditor(cfg.AppIds), '440, 730, 480...') +
            makeTextareaItem('sls-cfg-AdditionalApps', 'AdditionalApps', 'AppIDs to inject directly into your library licenses', formatArrayForEditor(cfg.AdditionalApps), '3934270, 480...') +
            makeTextareaItem('sls-cfg-FakeOffline', 'FakeOffline', 'Simulate Steam being offline for specified AppIDs', formatArrayForEditor(cfg.FakeOffline), '480, 730...') +
            makeTextareaItem('sls-cfg-DepotBlacklist', 'DepotBlacklist', 'Depot IDs to never download or update', formatArrayForEditor(cfg.DepotBlacklist), '228980, 228985...');

        var kvContent = makeTextareaItem('sls-cfg-FakeAppIds', 'FakeAppIds (AppID -> FakeAppID)', 'Change AppIDs of games to enable networking features (one Key: Value per line)', formatKvForEditor(cfg.FakeAppIds), '480: 440\n123: 456') +
            makeTextareaItem('sls-cfg-ManifestIds', 'ManifestIds (DepotID -> ManifestID)', 'Lock specific game depots to older manifest versions', formatKvForEditor(cfg.ManifestIds), '441: 123456789012345678') +
            makeTextareaItem('sls-cfg-AppTokens', 'AppTokens (AppID -> Token)', 'ProductInfo access tokens from Steam servers', formatKvForEditor(cfg.AppTokens), '730: 987654321098765432') +
            makeTextareaItem('sls-cfg-CDKeys', 'CDKeys (AppID -> CDKey)', 'Legacy CD keys required by older titles', formatKvForEditor(cfg.CDKeys), '440: ABCD-1234-EFGH-5678') +
            makeTextareaItem('sls-cfg-GameTitles', 'GameTitles (AppID -> Title)', 'Override display titles for owned games', formatKvForEditor(cfg.GameTitles), '440: Team Fortress 2 Custom') +
            makeTextareaItem('sls-cfg-SubscriptionTimestamps', 'SubscriptionTimestamps (AppID -> Timestamp)', 'Override purchase timestamps (Unix epoch seconds)', formatKvForEditor(cfg.SubscriptionTimestamps), '440: 1600000000') +
            makeTextareaItem('sls-cfg-SteamIdOverride', 'SteamIdOverride (AppID -> SteamID64)', 'Override SteamID64 an app receives (workaround for locked saves)', formatKvForEditor(cfg.SteamIdOverride), '440: 76561198000000001');

        var yamlContent = makeTextareaItem('sls-cfg-DlcDataYaml', 'DlcData (YAML Block)', 'Extra DLC data mapping (ParentAppID -> DLC ID: Name) for games with 64+ DLCs', cfg.DlcDataYaml || '', '440:\n  441: "DLC 1 Name"', 5) +
            makeTextareaItem('sls-cfg-DenuvoGamesYaml', 'DenuvoGames (YAML Block)', 'Blocks games from unlocking on unauthorized accounts (SteamID64 -> [AppIDs])', cfg.DenuvoGamesYaml || '', '76561198000000001:\n  - 440\n  - 730', 5);

        var sec5Html = '<div id="sls-cfg-sec-overrides" class="sls-cfg-section" style="display: none; max-width: 980px; width: 100%; box-sizing: border-box;">' +
            '<div class="DialogHeader" style="color: rgb(255, 255, 255); font-size: 22px; font-weight: 700; line-height: 28px; margin: 8px 0px 12px 0px;">Library &amp; Depots</div>' +
            makeCard('Application Lists', 'Configure AppIDs for injection, offline spoofing, and depot blacklisting', appListsContent) +
            makeCard('Key-Value Overrides', 'Fine-grained key-value maps for manifests, CD keys, and spoofed parameters', kvContent) +
            makeCard('Complex YAML Blocks', 'Raw YAML definitions for DLC dictionaries and Denuvo account locks', yamlContent) +
        '</div>';

        // SECTION 6: Logging & Diagnostics
        var LOG_LEVEL_FLAGS = [
            { flag: 0x01, name: 'Trace', hex: '0x01', desc: 'Detailed tracing statements' },
            { flag: 0x02, name: 'Once', hex: '0x02', desc: 'Only log statement once' },
            { flag: 0x04, name: 'Debug', hex: '0x04', desc: 'Internal debugging output' },
            { flag: 0x08, name: 'Warn', hex: '0x08', desc: 'Non-fatal recoverable warnings' },
            { flag: 0x10, name: 'Error', hex: '0x10', desc: 'Fatal unrecoverable errors' },
            { flag: 0x20, name: 'Info', hex: '0x20', desc: 'General informational logs' },
            { flag: 0x40, name: 'NotifyShort', hex: '0x40', desc: 'Short toast notifications' },
            { flag: 0x80, name: 'NotifyLong', hex: '0x80', desc: 'Long toast notifications' }
        ];

        var currentLogLevel = typeof cfg.LogLevels === 'number' ? cfg.LogLevels : 255;
        var bitmaskCheckboxes = LOG_LEVEL_FLAGS.map(function (lf) {
            var checked = (currentLogLevel & lf.flag) !== 0;
            return '<label class="sls-cfg-item" data-search="' + lf.name.toLowerCase() + ' ' + lf.hex + '" style="background-color: rgb(35, 38, 46); padding: 10px 16px; margin-top: 4px; border-radius: 2px; display: flex; align-items: center; justify-content: space-between; cursor: pointer;">' +
                '<div>' +
                    '<div style="font-size: 13px; font-weight: 400; color: rgb(220, 222, 223);">' + lf.name + ' <span style="color: rgb(26, 159, 255); font-family: monospace; font-size: 12px;">(' + lf.hex + ')</span></div>' +
                    '<div style="font-size: 12px; color: rgb(139, 146, 154);">' + lf.desc + '</div>' +
                '</div>' +
                '<input type="checkbox" id="sls-cfg-log-0x' + lf.flag.toString(16) + '" class="sls-cfg-log-flag" data-flag="' + lf.flag + '" ' + (checked ? 'checked' : '') + ' style="width: 18px; height: 18px; cursor: pointer;">' +
            '</label>';
        }).join('');

        var presetsHtml = '<div style="display: flex; gap: 8px; flex-wrap: wrap; margin-bottom: 12px;">' +
            '<span style="font-size: 12px; font-weight: 500; color: rgb(139, 146, 154); align-self: center; margin-right: 4px;">Presets:</span>' +
            '<button type="button" class="sls-preset-chip ' + (currentLogLevel === 255 ? 'active' : '') + '" data-preset="255">Default (0xFF)</button>' +
            '<button type="button" class="sls-preset-chip ' + (currentLogLevel === 248 ? 'active' : '') + '" data-preset="248">Standard (0xF8)</button>' +
            '<button type="button" class="sls-preset-chip ' + (currentLogLevel === 24 ? 'active' : '') + '" data-preset="24">Quiet (0x18)</button>' +
            '<button type="button" class="sls-preset-chip ' + (currentLogLevel === 0 ? 'active' : '') + '" data-preset="0">Silent (0x00)</button>' +
        '</div>';

        var logLevelsCard = presetsHtml +
            '<div class="sls-cfg-item" data-search="loglevel bitmask hex" style="background-color: rgb(35, 38, 46); padding: 12px 16px; margin-top: 6px; border-radius: 2px; display: flex; align-items: center; justify-content: space-between;">' +
            '<div>' +
                '<div style="font-size: 14px; font-weight: 300; color: rgb(220, 222, 223);">Combined Bitmask Value</div>' +
                '<div style="font-size: 13px; color: rgb(139, 146, 154); margin-top: 2px;">Calculated integer bitmask for active log levels</div>' +
            '</div>' +
            '<input type="text" id="sls-cfg-loglevel-hex" class="sls-cfg-input" style="width: 180px; font-family: monospace; font-weight: 700; text-align: center;" value="0x' + currentLogLevel.toString(16).toUpperCase() + ' (' + currentLogLevel + ')">' +
        '</div>' +
        '<div style="display: flex; flex-direction: column; margin-top: 6px;">' + bitmaskCheckboxes + '</div>';

        var diagCard = makeSettingRow('Dump Client Interfaces', 'Dumps all used IClientInterfaceMaps to disk for reverse engineering',
            makeToggle('sls-cfg-DumpClientInterfaces', cfg.DumpClientInterfaces), 'DumpClientInterfaces') +
            makeSettingRow('Extended Logging', 'Logs all calls to Steamworks (creates extremely large logfile, only for debugging)',
                makeToggle('sls-cfg-ExtendedLogging', cfg.ExtendedLogging), 'ExtendedLogging');

        var sec6Html = '<div id="sls-cfg-sec-logging" class="sls-cfg-section" style="display: none; max-width: 980px; width: 100%; box-sizing: border-box;">' +
            '<div class="DialogHeader" style="color: rgb(255, 255, 255); font-size: 22px; font-weight: 700; line-height: 28px; margin: 8px 0px 12px 0px;">Notifications &amp; Logs</div>' +
            makeCard('LogLevels Bitmask Configuration', 'Bitwise flags controlling terminal and in-client notification verbosity', logLevelsCard) +
            makeCard('Engine Diagnostics & Interface Dumps', 'Low-level introspection flags for Navi and Steamworks interfaces', diagCard) +
        '</div>';

        var sidebarHtml = '<div class="PageListColumn" style="background-color: rgb(42, 45, 52); width: 220px; min-width: 200px; max-width: 240px; height: 100%; padding: 36px 0px 16px 0px; display: flex; flex-direction: column; box-sizing: border-box; flex-shrink: 0; user-select: none;">' +
            '<div style="color: rgb(26, 159, 255); font-size: 17px; font-weight: 700; text-transform: uppercase; padding: 0px 14px 0px 24px; margin: 0px 0px 14px 0px; line-height: 24px;">NAVI SETTINGS</div>' +
            '<div style="margin: 0 16px 12px 16px; position: relative;">' +
                '<input type="text" id="sls-cfg-search" placeholder="Filter settings..." style="width: 100%; background: rgba(23, 29, 37, 0.7); border: none; border-radius: 2px; color: rgb(223, 227, 230); padding: 6px 10px 6px 28px; font-size: 12px; height: 30px; min-height: 30px; outline: none; box-sizing: border-box;">' +
                '<svg style="position: absolute; left: 8px; top: 7px; width: 14px; height: 14px; color: rgb(139, 146, 154);" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z"></path></svg>' +
            '</div>' +
            '<div role="tablist" aria-orientation="vertical" style="display: flex; flex-direction: column; flex: 1; overflow-y: auto; padding: 0; margin: 0;">' +
                '<div class="sls-cfg-pill active" role="tab" tabindex="0" aria-selected="true" data-target="sls-cfg-sec-cloudsaves">' +
                    '<div style="width: 20px; height: 20px; margin-right: 12px; display: flex; align-items: center; justify-content: center; flex-shrink: 0;">' +
                        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" width="20" height="20"><path d="M17.5 19H9a7 7 0 1 1 6.71-9h1.79a4.5 4.5 0 1 1 0 9Z"/><path d="M12 13v6"/><path d="m9 16 3 3 3-3"/></svg>' +
                    '</div>' +
                    '<span>Cloud Saves</span>' +
                '</div>' +
                '<div class="sls-cfg-pill" role="tab" tabindex="0" aria-selected="false" data-target="sls-cfg-sec-api">' +
                    '<div style="width: 20px; height: 20px; margin-right: 12px; display: flex; align-items: center; justify-content: center; flex-shrink: 0;">' +
                        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 36 36" fill="none" width="20" height="20"><path fill="currentColor" d="M6.05882 7.05042C8.17647 6.59664 10.1429 5.9916 12.1092 5.08403C14.0756 4.02521 16.042 2.81513 17.7059 1.30252C18.0084 1.15126 18.3109 1 18.6134 1C18.916 1 19.2185 1.15126 19.3697 1.30252C21.0336 2.81513 23 4.02521 24.9664 5.08403C26.7815 5.9916 28.8992 6.7479 31.0168 7.05042C31.6218 7.05042 32.0756 7.65546 32.0756 8.2605C32.0756 14.6134 31.1681 19.4538 29.2017 23.3866C27.2353 27.4706 23.9076 30.6471 19.2185 33.6723C19.0672 33.8235 18.7647 33.8235 18.4622 33.8235C18.1597 33.8235 18.0084 33.8235 17.7059 33.6723C13.0168 30.7983 9.68908 27.4706 7.72269 23.3866C5.90756 19.4538 5 14.6134 5 8.2605C5 7.65546 5.45378 7.20168 6.05882 7.05042Z"></path></svg>' +
                    '</div>' +
                    '<span>Provider Keys</span>' +
                '</div>' +
                '<div class="sls-cfg-pill" role="tab" tabindex="0" aria-selected="false" data-target="sls-cfg-sec-general">' +
                    '<div style="width: 20px; height: 20px; margin-right: 12px; display: flex; align-items: center; justify-content: center; flex-shrink: 0;">' +
                        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 36 36" fill="none" width="20" height="20"><path d="M34 6H2V28H10.6L9 34H27L25.4 28H34V6ZM30 24H6V10H30V24Z" fill="currentColor"></path></svg>' +
                    '</div>' +
                    '<span>General &amp; Unlock</span>' +
                '</div>' +
                '<div class="sls-cfg-pill" role="tab" tabindex="0" aria-selected="false" data-target="sls-cfg-sec-profile">' +
                    '<div style="width: 20px; height: 20px; margin-right: 12px; display: flex; align-items: center; justify-content: center; flex-shrink: 0;">' +
                        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 36 36" fill="none" width="20" height="20"><path d="M2 6.04999V30.05H34V6.04999H2ZM12 9.04999C13.933 9.04999 15.5 10.617 15.5 12.55C15.5 14.483 13.933 16.05 12 16.05C10.067 16.05 8.5 14.483 8.5 12.55C8.5 10.617 10.067 9.04999 12 9.04999ZM5 27.05C5 23.184 8.13401 20.05 12 20.05C15.866 20.05 19 23.184 19 27.05H5ZM30.05 21.05H22.05V17.05H30.05V21.05ZM29.99 14.05H20V10.05H30L29.99 14.05Z" fill="currentColor"></path></svg>' +
                    '</div>' +
                    '<span>Account &amp; Spoofing</span>' +
                '</div>' +
                '<div class="sls-cfg-pill" role="tab" tabindex="0" aria-selected="false" data-target="sls-cfg-sec-overrides">' +
                    '<div style="width: 20px; height: 20px; margin-right: 12px; display: flex; align-items: center; justify-content: center; flex-shrink: 0;">' +
                        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 36 36" fill="none" width="20" height="20"><path fill-rule="evenodd" clip-rule="evenodd" d="M33 19.9286H3L7.35714 6H28.6429L33 19.9286ZM3 31.7143V24.2143H33V31.7143H3Z" fill="currentColor"></path></svg>' +
                    '</div>' +
                    '<span>Library &amp; Depots</span>' +
                '</div>' +
                '<div class="sls-cfg-pill" role="tab" tabindex="0" aria-selected="false" data-target="sls-cfg-sec-logging">' +
                    '<div style="width: 20px; height: 20px; margin-right: 12px; display: flex; align-items: center; justify-content: center; flex-shrink: 0;">' +
                        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" fill="none" width="20" height="20"><path fill="currentColor" fill-rule="evenodd" clip-rule="evenodd" d="M10 1.66666C14.6024 1.66666 18.3334 5.39762 18.3334 10C18.3334 14.6024 14.6024 18.3333 10 18.3333C5.39765 18.3333 1.66669 14.6024 1.66669 10C1.66669 5.39762 5.39765 1.66666 10 1.66666ZM12.2223 5L11.389 10H8.61119L7.77786 5H12.2223ZM10.0001 15C8.92619 15 8.05564 14.1294 8.05564 13.0556C8.05564 11.9817 8.92619 11.1111 10.0001 11.1111C11.074 11.1111 11.9445 11.9817 11.9445 13.0556C11.9445 14.1294 11.074 15 10.0001 15Z"></path></svg>' +
                    '</div>' +
                    '<span>Notifications &amp; Logs</span>' +
                '</div>' +
            '</div>' +
            '<div style="height: 1px; background: rgba(255,255,255,0.08); margin: 12px 16px;"></div>' +
            '<div style="padding: 0 16px; display: flex; flex-direction: column; gap: 8px;">' +
                '<div id="sls-cfg-dirty-pill" style="font-size: 11px; font-weight: 500; color: #2ecc71; text-align: center; padding: 2px 0;">✓ All changes saved</div>' +
                '<button id="sls-cfg-save-btn" class="sls-cfg-btn" style="background: linear-gradient(to right, #47bfff 0%, #1a44c2 60%); color: #fff; border: none; border-radius: 2px; padding: 8px 14px; font-size: 13px; font-weight: 500; cursor: pointer; width: 100%; text-align: center;">Save Changes (Ctrl+S)</button>' +
                '<button id="sls-cfg-discard-btn" class="sls-cfg-btn" style="display: none; width: 100%; text-align: center;">Discard Changes</button>' +
                '<button id="sls-cfg-restart-btn" class="sls-cfg-btn" style="background: rgba(255, 77, 77, 0.15); border: 1px solid rgba(255, 77, 77, 0.3); color: #ff6b6b; width: 100%; text-align: center;">Restart Steam</button>' +
            '</div>' +
        '</div>';

        var dialogContentHtml = '<div class="DialogContent _DialogLayout" style="background-color: rgb(23, 29, 37); flex: 1; height: 100%; padding: 0; display: flex; flex-direction: column; box-sizing: border-box; overflow: hidden;">' +
            '<div id="sls-cfg-status-banner" role="status" aria-live="polite" style="display: none; margin: 16px 32px 0 32px; max-width: 980px; border-radius: 2px; padding: 10px 14px; font-size: 13px; font-weight: 500; border: 1px solid transparent; box-sizing: border-box;"></div>' +
            '<div class="sls-cfg-dialog-body" style="overflow-y: auto; padding: 24px 32px 48px 32px; width: 100%; max-width: 100%; flex: 1; display: flex; flex-direction: column; box-sizing: border-box;">' +
                sec1Html +
                sec2Html +
                sec3Html +
                sec4Html +
                sec5Html +
                sec6Html +
            '</div>' +
        '</div>';

        container.innerHTML = '<div style="display: flex; flex-direction: row; width: 100%; height: 100%; overflow: hidden;">' +
            sidebarHtml +
            dialogContentHtml +
        '</div>';

        // BIND EVENT LISTENERS
        function showStatusBanner(msg, color) {
            var banner = document.getElementById('sls-cfg-status-banner');
            if (!banner) return;
            banner.textContent = msg;
            banner.style.display = 'block';
            banner.style.backgroundColor = color === '#2ecc71' ? 'rgba(46, 204, 113, 0.15)' : (color === '#66c0f4' || color === 'rgb(26, 159, 255)' ? 'rgba(26, 159, 255, 0.15)' : 'rgba(255, 77, 77, 0.15)');
            banner.style.borderColor = color === '#2ecc71' ? '#2ecc71' : (color === '#66c0f4' || color === 'rgb(26, 159, 255)' ? 'rgb(26, 159, 255)' : '#ff4d4d');
            banner.style.color = color === '#2ecc71' ? '#2ecc71' : (color === '#66c0f4' || color === 'rgb(26, 159, 255)' ? 'rgb(26, 159, 255)' : '#ff4d4d');
            setTimeout(function () {
                if (banner) banner.style.display = 'none';
            }, 6000);
        }

        // 1. Category pill switching
        var pills = container.querySelectorAll('.sls-cfg-pill');
        var dialogBody = container.querySelector('.sls-cfg-dialog-body');
        function switchTab(targetId) {
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
            var sections = container.querySelectorAll('.sls-cfg-section');
            sections.forEach(function (sec) {
                sec.style.display = (sec.id === targetId) ? 'block' : 'none';
            });
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

        // 2. Live search filter
        var searchInput = document.getElementById('sls-cfg-search');
        if (searchInput) {
            searchInput.oninput = function () {
                var q = searchInput.value.trim().toLowerCase();
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
                if (!q) {
                    var activePill = container.querySelector('.sls-cfg-pill.active');
                    var targetId = activePill ? activePill.getAttribute('data-target') : 'sls-cfg-sec-cloudsaves';
                    sections.forEach(function (sec) {
                        sec.style.display = (sec.id === targetId) ? 'block' : 'none';
                    });
                } else {
                    sections.forEach(function (sec) {
                        var visible = sec.querySelectorAll('.sls-cfg-item:not([style*="display: none"])');
                        sec.style.display = visible.length > 0 ? 'block' : 'none';
                    });
                }
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
                    if (csState.enabled) {
                        masterBtn.textContent = 'Enabled';
                        masterBtn.style.background = '#059669';
                        masterBtn.style.color = '#ffffff';
                    } else {
                        masterBtn.textContent = 'Disabled';
                        masterBtn.style.background = '#4b5563';
                        masterBtn.style.color = '#ffffff';
                    }
                }
                if (engineBadge) {
                    if (csState.enabled) {
                        engineBadge.textContent = 'Running ✓';
                        engineBadge.style.background = 'rgba(16, 185, 129, 0.15)';
                        engineBadge.style.color = '#10b981';
                        engineBadge.style.border = '1px solid rgba(16, 185, 129, 0.3)';
                    } else {
                        engineBadge.textContent = 'Stopped';
                        engineBadge.style.background = 'rgba(172, 178, 201, 0.1)';
                        engineBadge.style.color = '#9ca3af';
                        engineBadge.style.border = '1px solid rgba(172, 178, 201, 0.2)';
                    }
                }
                if (managedBadge) {
                    managedBadge.textContent = csState.managedAppsCount + ' Games Managed';
                }
                if (csState.pendingOpsCount > 0) {
                    if (pendingCallout) pendingCallout.style.display = 'flex';
                    if (pendingBadge) {
                        pendingBadge.style.display = 'inline-block';
                        pendingBadge.textContent = csState.pendingOpsCount + ' Pending Op' + (csState.pendingOpsCount === 1 ? '' : 's');
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
            function renderProviderAuthButtons(prov, isAuthed) {
                if (!authActions) return;
                var isLocal = (prov === 'local-only');
                var loginStyle = isLocal
                    ? 'background: rgba(37, 99, 235, 0.35); border: 1px solid rgba(59, 130, 246, 0.35); color: rgba(255, 255, 255, 0.6); padding: 6px 14px; font-size: 12px; font-weight: 600; cursor: pointer; border-radius: 2px;'
                    : 'background: #2563eb; border: 1px solid #3b82f6; color: #ffffff; padding: 6px 14px; font-size: 12px; font-weight: 600; cursor: pointer; border-radius: 2px;';
                var signOutStyle = isLocal
                    ? 'background: rgba(239, 68, 68, 0.08); border: 1px solid rgba(239, 68, 68, 0.2); color: rgba(248, 113, 113, 0.55); padding: 6px 14px; font-size: 12px; font-weight: 600; cursor: pointer; border-radius: 2px;'
                    : 'background: rgba(239, 68, 68, 0.15); border: 1px solid rgba(239, 68, 68, 0.35); color: #f87171; padding: 6px 14px; font-size: 12px; font-weight: 600; cursor: pointer; border-radius: 2px;';
                authActions.innerHTML =
                    '<button id="sls-cfg-cs-connect-btn" class="sls-cfg-btn" style="' + loginStyle + '">' + (isAuthed ? 'Login / Reconnect' : 'Login') + '</button>' +
                    '<button id="sls-cfg-cs-disconnect-btn" class="sls-cfg-btn" style="' + signOutStyle + '">Sign Out</button>';

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
                        var doSignIn = (window.NaviCloudManager && window.NaviCloudManager.signInProvider)
                            ? window.NaviCloudManager.signInProvider(curProv)
                            : fetch('http://127.0.0.1:9001/cloud/provider/signin', {
                                method: 'POST',
                                headers: { 'Content-Type': 'text/plain' },
                                body: 'X-Navi-Auth: ' + (window.__naviAuthToken || '') + '\r\n' + JSON.stringify({ provider: curProv })
                            }).then(function (r) { return r.json(); }).then(function (d) {
                                if (d && d.authUrl) window.open(d.authUrl, '_blank');
                                return d;
                            });
                        doSignIn.then(function (resData) {
                            if (resData && resData.error) {
                                showStatusBanner('Provider login failed: ' + resData.error, '#ff4d4d');
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

                var dBtn = document.getElementById('sls-cfg-cs-disconnect-btn');
                if (dBtn) {
                    dBtn.onclick = function (e) {
                        e.preventDefault();
                        var curProv = providerSel ? providerSel.value : prov;
                        if (curProv === 'local-only') {
                            showStatusBanner('Local offline storage does not use an authenticated cloud session.', '#f59e0b');
                            return;
                        }
                        dBtn.textContent = 'Signing out...';
                        var doSignOut = (window.NaviCloudManager && window.NaviCloudManager.signOutProvider)
                            ? window.NaviCloudManager.signOutProvider(curProv)
                            : fetch('http://127.0.0.1:9001/cloud/provider/signout', {
                                method: 'POST',
                                headers: { 'Content-Type': 'text/plain' },
                                body: 'X-Navi-Auth: ' + (window.__naviAuthToken || '') + '\r\n' + JSON.stringify({ provider: curProv })
                            }).then(function (r) { return r.json(); });
                        doSignOut.then(function (resData) {
                            if (resData && resData.error) {
                                showStatusBanner('Sign out failed: ' + resData.error, '#ff4d4d');
                            } else {
                                showStatusBanner('Signed out from ' + curProv + '.', '#66c0f4');
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
                if (window.NaviCloudManager && window.NaviCloudManager.fetchProviderStatus) {
                    window.NaviCloudManager.fetchProviderStatus(provider).then(function (data) {
                        var isAuthed = data && !!data.authenticated;
                        if (authBadge) {
                            authBadge.textContent = isAuthed ? 'Connected ✓' : 'Not Connected';
                            authBadge.style.background = isAuthed ? 'rgba(16, 185, 129, 0.15)' : 'rgba(245, 158, 11, 0.15)';
                            authBadge.style.color = isAuthed ? '#10b981' : '#f59e0b';
                            authBadge.style.border = isAuthed ? '1px solid rgba(16, 185, 129, 0.3)' : '1px solid rgba(245, 158, 11, 0.3)';
                renderProviderAuthButtons(provider, false);
                var statusPromise = (window.NaviCloudManager && window.NaviCloudManager.fetchProviderStatus)
                    ? window.NaviCloudManager.fetchProviderStatus(provider)
                    : fetch('http://127.0.0.1:9001/cloud/provider/status?provider=' + encodeURIComponent(provider)).then(function (r) { return r.json(); });
                statusPromise.then(function (data) {
                    var isAuthed = data && !!data.authenticated;
                    if (authBadge) {
                        authBadge.textContent = isAuthed ? 'Connected ✓' : 'Not Connected';
                        authBadge.style.background = isAuthed ? 'rgba(16, 185, 129, 0.15)' : 'rgba(245, 158, 11, 0.15)';
                        authBadge.style.color = isAuthed ? '#10b981' : '#f59e0b';
                        authBadge.style.border = isAuthed ? '1px solid rgba(16, 185, 129, 0.3)' : '1px solid rgba(245, 158, 11, 0.3)';
                    }
                    if (authEmail) {
                        if (isAuthed && data.accountEmail) {
                            authEmail.textContent = data.accountEmail;
                            authEmail.style.display = 'inline';
                        } else {
                            authEmail.textContent = '';
                            authEmail.style.display = 'none';
                        }
                        if (authEmail) {
                            if (isAuthed && data.accountEmail) {
                                authEmail.textContent = data.accountEmail;
                                authEmail.style.display = 'inline';
                            } else {
                                authEmail.textContent = '';
                                authEmail.style.display = 'none';
                            }
                        }
                        if (authActions) {
                            if (isAuthed) {
                                authActions.innerHTML = '<button id="sls-cfg-cs-disconnect-btn" class="sls-cfg-btn" style="background: rgba(239, 68, 68, 0.15); border: 1px solid rgba(239, 68, 68, 0.35); color: #f87171; padding: 6px 14px; font-size: 12px; cursor: pointer; border-radius: 2px;">Disconnect</button>';
                                var dBtn = document.getElementById('sls-cfg-cs-disconnect-btn');
                                if (dBtn) {
                                    dBtn.onclick = function (e) {
                                        e.preventDefault();
                                        window.NaviCloudManager.signOutProvider(provider).then(function () {
                                            refreshCloudSavesUI();
                                        }).catch(function (err) {
                                            console.warn('[CloudSaves] Sign out error:', err);
                                        });
                                    };
                                }
                            } else {
                                authActions.innerHTML = '<button id="sls-cfg-cs-connect-btn" class="sls-cfg-btn" style="background: #2563eb; border: none; color: #ffffff; padding: 6px 14px; font-size: 12px; cursor: pointer; border-radius: 2px;">Connect</button>';
                                var cBtn = document.getElementById('sls-cfg-cs-connect-btn');
                                if (cBtn) {
                                    cBtn.onclick = function (e) {
                                        e.preventDefault();
                                        window.NaviCloudManager.signInProvider(provider).then(function () {
                                            refreshCloudSavesUI();
                                        }).catch(function (err) {
                                            console.warn('[CloudSaves] Sign in error:', err);
                                        });
                                    };
                                }
                            }
                        }
                    }).catch(function (err) {
                        console.warn('[CloudSaves] Provider status error:', err);
                        if (authBadge) {
                            authBadge.textContent = 'Not Connected';
                            authBadge.style.background = 'rgba(245, 158, 11, 0.15)';
                            authBadge.style.color = '#f59e0b';
                            authBadge.style.border = '1px solid rgba(245, 158, 11, 0.3)';
                        }
                    });
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

            if (appsListContainer) {
                appsListContainer.innerHTML = '';
                var managedApps = (cfg.CloudSaves && cfg.CloudSaves.ManagedApps) || [];
                if (managedApps.length === 0) {
                    var emptyDiv = document.createElement('div');
                    emptyDiv.style.cssText = 'color: rgb(139, 146, 154); font-size: 12px; padding: 12px; text-align: center;';
                    emptyDiv.textContent = 'No redirected games configured. Add an AppID above or toggle CloudRedirect from the game details page.';
                    appsListContainer.appendChild(emptyDiv);
                } else {
                    managedApps.forEach(function (appId) {
                        var title = (cfg.GameTitles && (cfg.GameTitles[appId] || cfg.GameTitles[String(appId)])) || '';
                        var appRow = document.createElement('div');
                        appRow.style.cssText = 'display: flex; align-items: center; justify-content: space-between; padding: 8px 10px; border-bottom: 1px solid rgba(255,255,255,0.06); gap: 10px; flex-wrap: wrap;';

                        var leftDiv = document.createElement('div');
                        leftDiv.style.cssText = 'font-size: 13px; font-weight: 500; color: #fff; display: flex; align-items: center; gap: 8px;';
                        if (title) {
                            leftDiv.innerHTML = escapeHtml(title) + ' <span style="font-size: 11px; color: #8b929a;">(' + escapeHtml(String(appId)) + ')</span>';
                        } else {
                            leftDiv.textContent = 'AppID ' + appId;
                        }

                        var actionsDiv = document.createElement('div');
                        actionsDiv.style.cssText = 'display: flex; gap: 6px; align-items: center;';

                        var bBtn = document.createElement('button');
                        bBtn.className = 'sls-cfg-btn';
                        bBtn.style.cssText = 'background: rgba(26, 159, 255, 0.15); border: 1px solid rgba(26, 159, 255, 0.3); color: #66c0f4; padding: 4px 10px; font-size: 11px; cursor: pointer; border-radius: 2px;';
                        bBtn.textContent = 'Backup';
                        bBtn.onclick = function (e) {
                            e.preventDefault();
                            if (window.NaviCloudManager && window.NaviCloudManager.backupAppSave) {
                                window.NaviCloudManager.backupAppSave(appId).then(function () {
                                    showStatusBanner('Save backup created for App ' + appId, '#2ecc71');
                                }).catch(function (err) {
                                    showStatusBanner('Backup failed: ' + (err.message || 'Unknown error'), '#ff4d4d');
                                });
                            }
                        };

                        var rBtn = document.createElement('button');
                        rBtn.className = 'sls-cfg-btn';
                        rBtn.style.cssText = 'background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.15); color: #dcdedf; padding: 4px 10px; font-size: 11px; cursor: pointer; border-radius: 2px;';
                        rBtn.textContent = 'Restore';
                        rBtn.onclick = function (e) {
                            e.preventDefault();
                            if (window.NaviCloudManager && window.NaviCloudManager.openRestoreModal) {
                                window.NaviCloudManager.openRestoreModal(appId);
                            }
                        };

                        var resetBtn = document.createElement('button');
                        resetBtn.className = 'sls-cfg-btn';
                        resetBtn.style.cssText = 'background: rgba(245, 158, 11, 0.15); border: 1px solid rgba(245, 158, 11, 0.3); color: #f59e0b; padding: 4px 10px; font-size: 11px; cursor: pointer; border-radius: 2px;';
                        resetBtn.textContent = 'Reset';
                        resetBtn.onclick = function (e) {
                            e.preventDefault();
                            if (window.NaviCloudManager && window.NaviCloudManager.openResetModal) {
                                window.NaviCloudManager.openResetModal(appId);
                            }
                        };

                        var rmBtn = document.createElement('button');
                        rmBtn.className = 'sls-cfg-btn';
                        rmBtn.style.cssText = 'background: rgba(239, 68, 68, 0.15); border: 1px solid rgba(239, 68, 68, 0.3); color: #f87171; padding: 4px 10px; font-size: 11px; cursor: pointer; border-radius: 2px;';
                        rmBtn.textContent = 'Remove';
                        rmBtn.onclick = function (e) {
                            e.preventDefault();
                            function doRemove() {
                                if (!cfg.CloudSaves) cfg.CloudSaves = {};
                                if (!Array.isArray(cfg.CloudSaves.ManagedApps)) cfg.CloudSaves.ManagedApps = [];
                                var idx = cfg.CloudSaves.ManagedApps.indexOf(appId);
                                if (idx !== -1) {
                                    cfg.CloudSaves.ManagedApps.splice(idx, 1);
                                } else {
                                    var sidx = cfg.CloudSaves.ManagedApps.indexOf(String(appId));
                                    if (sidx !== -1) cfg.CloudSaves.ManagedApps.splice(sidx, 1);
                                }
                                refreshCloudSavesUI();
                                checkDirty();
                                showStatusBanner('Removed App ' + appId + ' from CloudRedirect.', '#66c0f4');
                            }
                            if (window.NaviCloudManager && window.NaviCloudManager.toggleAppRedirect) {
                                window.NaviCloudManager.toggleAppRedirect(appId, false).then(function () {
                                    doRemove();
                                }).catch(function (err) {
                                    console.warn('[CloudSaves] Remove redirect error:', err);
                                    doRemove();
                                });
                            } else {
                                doRemove();
                            }
                        };

                        actionsDiv.appendChild(bBtn);
                        actionsDiv.appendChild(rBtn);
                        actionsDiv.appendChild(resetBtn);
                        actionsDiv.appendChild(rmBtn);

                        appRow.appendChild(leftDiv);
                        appRow.appendChild(actionsDiv);
                        appsListContainer.appendChild(appRow);
                    });
                }
            }
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

        var addAppInput = document.getElementById('sls-cfg-add-cloud-app-input');
        var addAppBtn = document.getElementById('sls-cfg-add-cloud-app-btn');
        if (addAppBtn && addAppInput) {
            addAppBtn.onclick = function (e) {
                e.preventDefault();
                var appIdVal = parseInt(addAppInput.value.trim(), 10);
                if (!appIdVal || isNaN(appIdVal) || appIdVal <= 0) {
                    showStatusBanner('Please enter a valid AppID number.', '#ff4d4d');
                    return;
                }
                if (!cfg.CloudSaves) cfg.CloudSaves = {};
                if (!Array.isArray(cfg.CloudSaves.ManagedApps)) cfg.CloudSaves.ManagedApps = [];
                if (cfg.CloudSaves.ManagedApps.indexOf(appIdVal) !== -1 || cfg.CloudSaves.ManagedApps.indexOf(String(appIdVal)) !== -1) {
                    showStatusBanner('AppID ' + appIdVal + ' is already managed.', '#f59e0b');
                    return;
                }
                function appAdded() {
                    cfg.CloudSaves.ManagedApps.push(appIdVal);
                    addAppInput.value = '';
                    refreshCloudSavesUI();
                    checkDirty();
                    showStatusBanner('AppID ' + appIdVal + ' added to CloudRedirect.', '#2ecc71');
                }
                if (window.NaviCloudManager && window.NaviCloudManager.toggleAppRedirect) {
                    window.NaviCloudManager.toggleAppRedirect(appIdVal, true).then(function () {
                        appAdded();
                    }).catch(function (err) {
                        showStatusBanner('Error adding AppID: ' + (err.message || 'Unknown error'), '#ff4d4d');
                    });
                } else {
                    appAdded();
                }
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

        // 7. API keys toggle visibility
        ['RyuuKey', 'DepotBoxKey', 'HubcapKey'].forEach(function (k) {
            var btn = document.getElementById('sls-cfg-toggle-' + k);
            var inp = document.getElementById('sls-cfg-' + k);
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
        });

        // 8. Legacy sync handlers stripped (Phase 2b)





        // 9. Header Action Buttons
        var restartBtn = document.getElementById('sls-cfg-restart-btn');
        if (restartBtn) {
            restartBtn.onclick = function (e) {
                e.preventDefault();
                if (!confirm('Are you sure you want to restart Steam? Navi will cleanly terminate and restart Steam.')) return;
                fetch('http://127.0.0.1:9001/restart', { method: 'POST' }).then(function () {
                    showStatusBanner('Restart signal sent to Steam...', '#66c0f4');
                }).catch(function () {
                    showStatusBanner('Restart signal sent to Steam...', '#66c0f4');
                });
            };
        }

        var discardBtn = document.getElementById('sls-cfg-discard-btn');
        if (discardBtn) {
            discardBtn.onclick = function (e) {
                e.preventDefault();
                renderConfigPage(container, cfg, sync);
                showStatusBanner('All unsaved changes discarded.', '#66c0f4');
            };
        }

        // 10. Save All Changes & State Tracking
        var saveBtn = document.getElementById('sls-cfg-save-btn');

        function serializeCurrentForm() {
            function getVal(id) { var el = document.getElementById(id); return el ? el.value : ''; }
            function getCheckbox(id) { var el = document.getElementById(id); return el ? el.checked : false; }
            var state = {
                cloudAutoBackup: getCheckbox('sls-cfg-CloudAutoBackup'),
                cloudRetention: getVal('sls-cfg-CloudRetention'),
                cloudProvider: getVal('sls-cfg-cs-provider'),
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

        function checkDirty() {
            var curr = serializeCurrentForm();
            if (curr !== initialFormState) {
                isDirty = true;
                var pill = document.getElementById('sls-cfg-dirty-pill');
                if (pill) {
                    pill.textContent = '● Unsaved changes';
                    pill.style.color = '#f39c12';
                }
                var dBtn = document.getElementById('sls-cfg-discard-btn');
                if (dBtn) dBtn.style.display = 'block';
                if (saveBtn) {
                    saveBtn.style.boxShadow = '0 0 10px rgba(26, 159, 255, 0.6)';
                }
            } else {
                markClean();
            }
        }

        function markClean() {
            isDirty = false;
            var pill = document.getElementById('sls-cfg-dirty-pill');
            if (pill) {
                pill.textContent = '✓ All changes saved';
                pill.style.color = '#2ecc71';
            }
            var dBtn = document.getElementById('sls-cfg-discard-btn');
            if (dBtn) dBtn.style.display = 'none';
            if (saveBtn) {
                saveBtn.style.boxShadow = 'none';
            }
        }

        container.addEventListener('input', checkDirty);
        container.addEventListener('change', checkDirty);

        function collectAndSaveConfig(callback, silent) {
            if (!saveBtn) return;
            saveBtn.disabled = true;
            saveBtn.textContent = 'Saving...';

            function getVal(id) {
                var el = document.getElementById(id);
                return el ? el.value : '';
            }
            function getInt(id, def) {
                var v = parseInt(getVal(id), 10);
                return isNaN(v) ? (def || 0) : v;
            }
            function getCheckbox(id) {
                var el = document.getElementById(id);
                return el ? el.checked : false;
            }
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
            function parseStringArray(id) {
                var raw = getVal(id);
                if (!raw) return [];
                var tokens = raw.split(/[\s,]+/);
                var res = [];
                tokens.forEach(function (tok) {
                    var t = tok.trim();
                    if (t) res.push(t);
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
                    AutoBackupBeforeReset: getCheckbox('sls-cfg-CloudAutoBackup'),
                    BackupRetentionCount: getInt('sls-cfg-CloudRetention', 5),
                    ManagedApps: (cfg.CloudSaves && cfg.CloudSaves.ManagedApps) || []
                }
            };

            fetch('http://127.0.0.1:9001/config/full', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            }).then(function (r) {
                return r.json();
            }).then(function (res) {
                saveBtn.disabled = false;
                saveBtn.textContent = 'Save Changes (Ctrl+S)';
                if (res.success) {
                    cfg = JSON.parse(JSON.stringify(payload));
                    initialFormState = serializeCurrentForm();
                    markClean();
                    refreshCloudSavesUI();
                    if (!silent) {
                        showStatusBanner('Configuration saved and reloaded successfully!', '#2ecc71');
                    }
                    if (typeof callback === 'function') callback(null, res);
                } else {
                    if (!silent) {
                        showStatusBanner('Error saving configuration: ' + (res.message || 'Unknown error'), '#ff4d4d');
                    }
                    if (typeof callback === 'function') callback(new Error(res.message || 'Unknown error'));
                }
            }).catch(function (err) {
                saveBtn.disabled = false;
                saveBtn.textContent = 'Save Changes (Ctrl+S)';
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

        // 12. Initial activation
        refreshCloudSavesUI();
        initialFormState = serializeCurrentForm();
        markClean();
    }

    function initManualInstallPage() {
        var dropzone = document.getElementById('sls-manual-dropzone');
        var fileList = document.getElementById('sls-manual-file-list');
        var status = document.getElementById('sls-manual-status');
        var submitBtn = document.getElementById('sls-manual-submit');
        var selectFilesBtn = document.getElementById('sls-manual-select-files');
        var selectFolderBtn = document.getElementById('sls-manual-select-folder');

        var selectedFiles = [];
        var activeAppId = null;
        var activeAppMetadata = null;
        var fetchedMetadataCache = {};
        var checkedFiles = {};
        var openDetails = {};
        var luaContentsCache = {};

        // 1. More robust attribute setting for Steam CEF
        var fileInput = document.createElement('input');
        fileInput.type = 'file';
        
        // Brute-force the multiple flag for CEF
        fileInput.multiple = true; 
        fileInput.setAttribute('multiple', 'true');
        fileInput.setAttribute('multiple', 'multiple');
        
        // This is the magic bullet for some CEF versions. 
        // Forcing an 'accept' type often triggers the newer, multi-select capable Windows dialog.
        fileInput.setAttribute('accept', '*/*'); 
        
        fileInput.style.display = 'none';
        document.body.appendChild(fileInput);

        var folderInput = document.createElement('input');
        folderInput.type = 'file';
        folderInput.setAttribute('webkitdirectory', '');
        folderInput.setAttribute('directory', '');
        folderInput.style.display = 'none';
        document.body.appendChild(folderInput);

        selectFilesBtn.onclick = function (e) {
            e.preventDefault(); e.stopPropagation();
            fileInput.click();
        };

        selectFolderBtn.onclick = function (e) {
            e.preventDefault(); e.stopPropagation();
            folderInput.click();
        };

        function loadLuaFilesCache(files, callback) {
            var luaFilesToRead = files.filter(function (file) {
                return file.name.toLowerCase().endsWith('.lua') && typeof luaContentsCache[file.name] === 'undefined';
            });

            if (luaFilesToRead.length === 0) {
                callback();
                return;
            }

            var remaining = luaFilesToRead.length;
            luaFilesToRead.forEach(function (file) {
                var reader = new FileReader();
                reader.onload = function (e) {
                    luaContentsCache[file.name] = e.target.result;
                    remaining--;
                    if (remaining === 0) {
                        callback();
                    }
                };
                reader.onerror = function () {
                    luaContentsCache[file.name] = '';
                    remaining--;
                    if (remaining === 0) {
                        callback();
                    }
                };
                reader.readAsText(file);
            });
        }

        // Extract zip files into individual File objects
        function extractZipFiles(zipFile) {
            return loadJSZip().then(function (JSZipLib) {
                return JSZipLib.loadAsync(zipFile).then(function (zip) {
                    var promises = [];
                    zip.forEach(function (relativePath, zipEntry) {
                        if (zipEntry.dir) return;
                        // Only extract relevant file types
                        var lowerName = zipEntry.name.toLowerCase();
                        if (!lowerName.endsWith('.lua') && !lowerName.endsWith('.manifest')) return;
                        promises.push(
                            zipEntry.async('blob').then(function (blob) {
                                // Use just the filename (strip directory paths inside zip)
                                var fileName = zipEntry.name.split('/').pop();
                                return new File([blob], fileName, { type: blob.type || 'application/octet-stream' });
                            })
                        );
                    });
                    return Promise.all(promises);
                });
            });
        }

        function handleFiles(files) {
            var newFiles = Array.prototype.slice.call(files);

            // Separate zip files from regular files, filtering regular files by extension (.lua and .manifest only)
            var regularFiles = [];
            var zipFiles = [];
            newFiles.forEach(function (file) {
                var lowerName = file.name.toLowerCase();
                if (lowerName.endsWith('.zip')) {
                    zipFiles.push(file);
                } else if (lowerName.endsWith('.lua') || lowerName.endsWith('.manifest')) {
                    regularFiles.push(file);
                }
            });

            // Process regular files immediately
            function addFiles(filesToAdd) {
                filesToAdd.forEach(function (newFile) {
                    var exists = selectedFiles.some(function (existingFile) {
                        return existingFile.name === newFile.name;
                    });
                    if (!exists) {
                        selectedFiles.push(newFile);
                    }
                });
            }

            if (zipFiles.length === 0) {
                // No zips, process normally
                addFiles(regularFiles);
                loadLuaFilesCache(regularFiles, function () {
                    initCheckedFiles();
                    updateFileList();
                });
                return;
            }

            // Extract all zip files, then merge with regular files
            var zipPromises = zipFiles.map(function (zf) {
                return extractZipFiles(zf).catch(function (err) {
                    log('Failed to extract ' + zf.name + ': ' + err.message);
                    // If extraction fails, don't show the zip file in the list since it's not a .lua/.manifest
                    return [];
                });
            });

            Promise.all(zipPromises).then(function (results) {
                var allExtracted = [];
                results.forEach(function (extractedFiles) {
                    allExtracted = allExtracted.concat(extractedFiles);
                });
                var combined = regularFiles.concat(allExtracted);
                addFiles(combined);
                loadLuaFilesCache(combined, function () {
                    initCheckedFiles();
                    updateFileList();
                });
            });
        }

        // 3. Clear the input value so onchange fires even if the same file is selected again
        fileInput.onchange = function () {
            handleFiles(fileInput.files);
            fileInput.value = '';
        };
        folderInput.onchange = function () {
            handleFiles(folderInput.files);
            folderInput.value = '';
        };

        dropzone.ondragover = function (e) {
            e.preventDefault(); e.stopPropagation();
            dropzone.style.borderColor = '#1a9fff';
            dropzone.style.background = 'rgba(26, 159, 255, 0.05)';
        };

        dropzone.ondragleave = function (e) {
            e.preventDefault(); e.stopPropagation();
            dropzone.style.borderColor = 'rgba(255, 255, 255, 0.15)';
            dropzone.style.background = 'rgba(0,0,0,0.15)';
        };

        dropzone.ondrop = function (e) {
            e.preventDefault(); e.stopPropagation();
            dropzone.style.borderColor = 'rgba(255, 255, 255, 0.15)';
            dropzone.style.background = 'rgba(0,0,0,0.15)';
            if (e.dataTransfer && e.dataTransfer.files) {
                handleFiles(e.dataTransfer.files);
            }
        };

        dropzone.onclick = function (e) {
            if (e.target !== selectFilesBtn && e.target !== selectFolderBtn) {
                fileInput.click();
            }
        };

        var pendingFetches = {};

        function fetchAppMetadata(appid) {
            if (!appid) return;
            if (fetchedMetadataCache[appid]) {
                if (activeAppId === appid) {
                    activeAppMetadata = fetchedMetadataCache[appid];
                }
                updateFileList();
                return;
            }
            if (pendingFetches[appid]) return;
            pendingFetches[appid] = true;

            fetch('https://api.steamcmd.net/v1/info/' + appid)
                .then(function (r) { return r.json(); })
                .then(function (data) {
                    var appData = data && data.data && data.data[appid];
                    if (appData) {
                        fetchedMetadataCache[appid] = appData;
                        updateFileList();
                    }
                    delete pendingFetches[appid];
                })
                .catch(function (err) {
                    console.warn('[SLS] Failed to fetch metadata for:', appid, err);
                    delete pendingFetches[appid];
                });
        }

        function getAppIdFromFileName(fileName) {
            var manifestMatch = fileName.match(/^([0-9]+)_[0-9]+\.manifest$/);
            if (manifestMatch) return manifestMatch[1];

            var dotIndex = fileName.lastIndexOf('.');
            var baseName = dotIndex !== -1 ? fileName.substring(0, dotIndex) : fileName;
            if (/^[0-9]+$/.test(baseName)) return baseName;

            var match = fileName.match(/([0-9]+)/);
            if (match) return match[1];

            return null;
        }

        function classifyFile(file, parentAppId) {
            if (file.name.toLowerCase().endsWith('.lua')) return 'lua';

            var manifestMatch = file.name.toLowerCase().match(/^([0-9]+)_[0-9]+\.manifest$/);
            if (manifestMatch) {
                var depotId = manifestMatch[1];
                var appMetadata = null;
                if (parentAppId && fetchedMetadataCache[parentAppId]) {
                    appMetadata = fetchedMetadataCache[parentAppId];
                } else if (activeAppMetadata) {
                    appMetadata = activeAppMetadata;
                } else {
                    // Try to find the depot across all cached app metadata
                    for (var appid in fetchedMetadataCache) {
                        var meta = fetchedMetadataCache[appid];
                        if (meta && meta.depots && meta.depots[depotId]) {
                            appMetadata = meta;
                            break;
                        }
                    }
                }

                if (appMetadata && appMetadata.depots && appMetadata.depots[depotId]) {
                    var depot = appMetadata.depots[depotId];
                    var osList = depot.config && depot.config.oslist || '';

                    if (!osList) {
                        var launchObj = appMetadata.config && appMetadata.config.launch;
                        if (launchObj) {
                            var firstLaunch = launchObj['0'] || (Array.isArray(launchObj) ? launchObj[0] : null);
                            if (firstLaunch && firstLaunch.config && firstLaunch.config.oslist) {
                                osList = firstLaunch.config.oslist;
                            } else {
                                var launchEntries = Array.isArray(launchObj) ? launchObj : Object.values(launchObj);
                                for (var li = 0; li < launchEntries.length; li++) {
                                    var entry = launchEntries[li];
                                    if (entry && entry.config && entry.config.oslist) {
                                        osList = entry.config.oslist;
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    if (depot.dlcappid || (depot.name && depot.name.toLowerCase().indexOf('dlc') !== -1)) {
                        return 'dlc';
                    } else if (osList.indexOf('windows') !== -1) {
                        return 'windows';
                    } else if (osList.indexOf('linux') !== -1) {
                        return 'linux';
                    } else if (osList.indexOf('macos') !== -1 || osList.indexOf('osx') !== -1) {
                        return 'macos';
                    }
                }
                return 'other';
            }

            return 'other';
        }

        function initCheckedFiles() {
            // Don't reset the object entirely. Only default new files to true.
            selectedFiles.forEach(function (file) {
                if (typeof checkedFiles[file.name] === 'undefined') {
                    checkedFiles[file.name] = true;
                }
            });
        }

        function getCheckedOSCount() {
            var count = 0;
            selectedFiles.forEach(function (file) {
                var fileAppId = getAppIdFromFileName(file.name);
                var cat = classifyFile(file, fileAppId);
                if ((cat === 'windows' || cat === 'linux' || cat === 'macos') && checkedFiles[file.name]) {
                    count++;
                }
            });
            return count;
        }

        function findRelatedLuaFile(file, luaFiles) {
            if (luaFiles.length === 0) return null;
            if (luaFiles.length === 1) return luaFiles[0];

            var isManifest = file.name.toLowerCase().endsWith('.manifest');
            if (isManifest) {
                // Extract numbers from the manifest file name (e.g. depot ID and manifest ID)
                var numbers = file.name.match(/[0-9]+/g) || [];

                for (var i = 0; i < luaFiles.length; i++) {
                    var luaName = luaFiles[i].name;
                    var luaContent = luaContentsCache[luaName];
                    if (luaContent) {
                        for (var j = 0; j < numbers.length; j++) {
                            var num = numbers[j];
                            if (num.length >= 5) { // Only match significant numbers
                                if (luaContent.indexOf(num) !== -1) {
                                    return luaFiles[i];
                                }
                            }
                        }
                    }
                }
            }

            var fileAppId = getAppIdFromFileName(file.name);
            if (fileAppId) {
                for (var i = 0; i < luaFiles.length; i++) {
                    var luaAppId = getAppIdFromFileName(luaFiles[i].name);
                    if (luaAppId === fileAppId) {
                        return luaFiles[i];
                    }
                }
            }
            return luaFiles[0];
        }

        // Extract AppID from lua file contents by parsing addappid(XXXX) calls
        function getAppIdFromLuaContent(luaContent) {
            if (!luaContent) return null;
            var match = luaContent.match(/addappid\s*\(\s*([0-9]+)\s*\)/);
            return match ? match[1] : null;
        }

        // Find the best AppID from all checked lua files
        function detectAppIdFromCheckedFiles() {
            for (var i = 0; i < selectedFiles.length; i++) {
                var file = selectedFiles[i];
                if (file.name.toLowerCase().endsWith('.lua') && checkedFiles[file.name] !== false) {
                    var content = luaContentsCache[file.name];
                    var appid = getAppIdFromLuaContent(content);
                    if (appid) return appid;
                    // Fallback: try extracting from filename
                    var fromName = getAppIdFromFileName(file.name);
                    if (fromName) return fromName;
                }
            }
            // Fallback: try manifest filenames
            for (var j = 0; j < selectedFiles.length; j++) {
                var mf = selectedFiles[j];
                if (mf.name.toLowerCase().endsWith('.manifest') && checkedFiles[mf.name] !== false) {
                    var mfAppId = getAppIdFromFileName(mf.name);
                    if (mfAppId) return mfAppId;
                }
            }
            return null;
        }

        // Update the submit button state based on current checked files
        function updateSubmitButton() {
            var hasCheckedFiles = selectedFiles.some(function (f) {
                return checkedFiles[f.name] !== false;
            });
            var detectedAppId = detectAppIdFromCheckedFiles();
            activeAppId = detectedAppId;

            if (hasCheckedFiles && detectedAppId) {
                submitBtn.disabled = false;
                submitBtn.style.background = 'linear-gradient(90deg, #1a9fff 0%, #66c0f4 100%)';
                submitBtn.style.color = '#fff';
                submitBtn.style.cursor = 'pointer';
            } else {
                submitBtn.disabled = true;
                submitBtn.style.background = 'rgba(255, 255, 255, 0.05)';
                submitBtn.style.color = 'rgba(255,255,255,0.3)';
                submitBtn.style.cursor = 'not-allowed';
            }
        }

        function updateFileList() {
            fileList.innerHTML = '';
            if (selectedFiles.length === 0) {
                fileList.style.display = 'none';
                updateSubmitButton();
                return;
            }
            fileList.style.display = 'flex';

            var badgeStyles = {
                lua: { label: 'LUA', color: 'rgba(156, 39, 176, 0.15)', textColor: '#ce93d8' },
                windows: { label: 'Windows', color: 'rgba(26, 159, 255, 0.15)', textColor: '#66c0f4' },
                linux: { label: 'Linux', color: 'rgba(76, 175, 80, 0.15)', textColor: '#4caf50' },
                macos: { label: 'macOS', color: 'rgba(200, 200, 200, 0.15)', textColor: '#e0e0e0' },
                dlc: { label: 'DLC', color: 'rgba(255, 193, 7, 0.15)', textColor: '#ffc107' },
                other: { label: 'Other', color: 'rgba(255, 255, 255, 0.08)', textColor: '#8f98a0' }
            };

            // 1. Separate files into Lua files and other files
            var luaFiles = selectedFiles.filter(function (file) {
                var fileAppId = getAppIdFromFileName(file.name);
                return classifyFile(file, fileAppId) === 'lua';
            });

            var manifestRelations = {};
            luaFiles.forEach(function (lf) {
                manifestRelations[lf.name] = [];
            });

            var standaloneFiles = [];

            selectedFiles.forEach(function (file) {
                var fileAppId = getAppIdFromFileName(file.name);
                var cat = classifyFile(file, fileAppId);
                if (cat === 'lua') return;

                var isManifest = file.name.toLowerCase().endsWith('.manifest');
                if (isManifest && luaFiles.length > 0) {
                    var relatedLua = findRelatedLuaFile(file, luaFiles);
                    if (relatedLua) {
                        manifestRelations[relatedLua.name].push(file);
                        return;
                    }
                }
                standaloneFiles.push(file);
            });

            // 2. Render all Lua files first
            luaFiles.forEach(function (file) {
                var fileAppId = getAppIdFromFileName(file.name);
                var cat = classifyFile(file, fileAppId);
                var style = badgeStyles[cat];

                var fileDisplayName = 'Unknown';
                var fileAppId = getAppIdFromFileName(file.name);

                if (fileAppId) {
                    if (fetchedMetadataCache[fileAppId]) {
                        var appData = fetchedMetadataCache[fileAppId];
                        var name = appData.common && appData.common.name || fileAppId;
                        var parentName = activeAppMetadata && activeAppMetadata.common && activeAppMetadata.common.name;
                        if (parentName && name.indexOf(parentName) === 0) {
                            var sub = name.substring(parentName.length).trim();
                            sub = sub.replace(/^[:\-\s]+/, '').replace(/^DLC\s*[:\-\s]*/i, '').trim();
                            if (sub) name = sub;
                        }
                        fileDisplayName = name;
                    } else {
                        fetchAppMetadata(fileAppId);
                        fileDisplayName = 'AppID ' + fileAppId;
                    }
                } else {
                    fileDisplayName = file.name;
                }

                var item = document.createElement('details');
                item.style.cssText = 'background:rgba(255,255,255,0.03); border:1px solid rgba(255,255,255,0.05); border-radius:6px; font-size:14px; overflow:hidden; transition:opacity 0.15s, background 0.15s; flex-shrink:0; width:100%; box-sizing:border-box;';

                if (openDetails[file.name]) {
                    item.setAttribute('open', '');
                }

                item.ontoggle = function () {
                    openDetails[file.name] = item.open;
                    
                    // Force a reflow on parent containers to fix Chromium CEF flex scroll height bugs
                    var originalOverflow = fileList.style.overflowY;
                    fileList.style.overflowY = 'hidden';
                    fileList.offsetHeight; // trigger reflow
                    fileList.style.overflowY = originalOverflow;

                    var pageContainer = document.getElementById('sls-page-container');
                    if (pageContainer) {
                        var pageOverflow = pageContainer.style.overflowY;
                        pageContainer.style.overflowY = 'hidden';
                        pageContainer.offsetHeight; // trigger reflow
                        pageContainer.style.overflowY = pageOverflow;
                    }
                };

                var summary = document.createElement('summary');
                summary.style.cssText = 'display:flex; justify-content:space-between; align-items:center; padding:20px 24px; cursor:pointer; user-select:none; outline:none; list-style:none; gap:12px;';

                var leftSide = document.createElement('div');
                leftSide.style.cssText = 'display:flex; align-items:center; gap:8px; overflow:hidden; flex:1; min-width:0;';

                var badge = document.createElement('span');
                badge.innerText = style.label;
                badge.style.cssText = 'background:' + style.color + '; color:' + style.textColor + '; border:1px solid ' + style.color.replace('0.15', '0.3').replace('0.08', '0.2') + '; font-size:10px; font-weight:700; text-transform:uppercase; padding:2px 6px; border-radius:4px; letter-spacing:0.5px; white-space:nowrap; flex-shrink:0;';
                leftSide.appendChild(badge);

                var nameSpan = document.createElement('span');
                nameSpan.innerText = fileDisplayName;
                nameSpan.style.cssText = 'color:#fff; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; flex-shrink:1; min-width:0;';
                leftSide.appendChild(nameSpan);

                summary.appendChild(leftSide);

                var rightSide = document.createElement('div');
                rightSide.style.cssText = 'display:flex; align-items:center; gap:12px; flex-shrink:0;';

                var sizeSpan = document.createElement('span');
                var sizeKB = Math.round(file.size / 1024 * 10) / 10;
                var sizeStr = sizeKB >= 1024 ? (Math.round(sizeKB / 1024 * 10) / 10) + ' MB' : sizeKB + ' KB';
                sizeSpan.innerText = sizeStr;
                sizeSpan.style.cssText = 'color:#8f98a0; white-space:nowrap; text-align:right; font-family:monospace; min-width:85px;';
                rightSide.appendChild(sizeSpan);

                var chevron = document.createElement('div');
                chevron.className = 'sls-lua-chevron';
                chevron.innerHTML = '<svg style="width: 16px; height: 16px; display: block;" fill="none" stroke="currentColor" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">' +
                    '<path stroke-linecap="round" stroke-linejoin="round" stroke-width="2.5" d="M19 9l-7 7-7-7"></path>' +
                    '</svg>';
                rightSide.appendChild(chevron);

                summary.appendChild(rightSide);
                item.appendChild(summary);

                var contentContainer = document.createElement('div');
                contentContainer.style.cssText = 'padding: 0 24px 20px 24px; border-top: 1px solid rgba(255,255,255,0.03); background: rgba(0,0,0,0.1); display: flex; flex-direction: column; gap: 20px;';

                // Render related manifest files inside details
                var relatedManifests = manifestRelations[file.name] || [];
                if (relatedManifests.length > 0) {
                    // Sort relatedManifests by category: linux, windows, macos, dlc, other
                    var categoryOrder = { linux: 1, windows: 2, macos: 3, dlc: 4, other: 5 };
                    relatedManifests.sort(function (a, b) {
                        var catA = classifyFile(a, fileAppId);
                        var catB = classifyFile(b, fileAppId);
                        var weightA = categoryOrder[catA] || 99;
                        var weightB = categoryOrder[catB] || 99;
                        if (weightA !== weightB) {
                            return weightA - weightB;
                        }
                        return a.name.localeCompare(b.name);
                    });

                    var manifestsSection = document.createElement('div');
                    manifestsSection.style.cssText = 'display: flex; flex-direction: column; gap: 8px; margin-top: 16px;';

                    var hasAnyUnchecked = relatedManifests.some(function (mFile) {
                        return checkedFiles[mFile.name] === false;
                    });

                    // Actions bar: Single toggle button
                    var actionsBar = document.createElement('div');
                    actionsBar.style.cssText = 'display: flex; gap: 12px; margin-bottom: 4px;';

                    var toggleBtn = document.createElement('button');
                    if (hasAnyUnchecked) {
                        toggleBtn.innerText = 'Check All';
                        toggleBtn.style.cssText = 'background: rgba(26, 159, 255, 0.1); border: 1px solid rgba(26, 159, 255, 0.2); border-radius: 4px; color: #66c0f4; padding: 4px 8px; font-size: 11px; font-weight: 700; cursor: pointer; text-transform: uppercase; letter-spacing: 0.5px; transition: all 0.2s; outline: none;';
                        toggleBtn.onmouseover = function () {
                            toggleBtn.style.background = 'rgba(26, 159, 255, 0.2)';
                            toggleBtn.style.borderColor = 'rgba(26, 159, 255, 0.4)';
                        };
                        toggleBtn.onmouseout = function () {
                            toggleBtn.style.background = 'rgba(26, 159, 255, 0.1)';
                            toggleBtn.style.borderColor = 'rgba(26, 159, 255, 0.2)';
                        };
                        toggleBtn.onclick = function (e) {
                            e.preventDefault(); e.stopPropagation();
                            relatedManifests.forEach(function (mFile) {
                                checkedFiles[mFile.name] = true;
                            });
                            updateFileList();
                        };
                    } else {
                        toggleBtn.innerText = 'Uncheck All';
                        toggleBtn.style.cssText = 'background: rgba(255, 255, 255, 0.05); border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 4px; color: #8f98a0; padding: 4px 8px; font-size: 11px; font-weight: 700; cursor: pointer; text-transform: uppercase; letter-spacing: 0.5px; transition: all 0.2s; outline: none;';
                        toggleBtn.onmouseover = function () {
                            toggleBtn.style.background = 'rgba(255, 255, 255, 0.1)';
                            toggleBtn.style.borderColor = 'rgba(255, 255, 255, 0.2)';
                            toggleBtn.style.color = '#fff';
                        };
                        toggleBtn.onmouseout = function () {
                            toggleBtn.style.background = 'rgba(255, 255, 255, 0.05)';
                            toggleBtn.style.borderColor = 'rgba(255, 255, 255, 0.1)';
                            toggleBtn.style.color = '#8f98a0';
                        };
                        toggleBtn.onclick = function (e) {
                            e.preventDefault(); e.stopPropagation();
                            relatedManifests.forEach(function (mFile) {
                                checkedFiles[mFile.name] = false;
                            });
                            updateFileList();
                        };
                    }

                    actionsBar.appendChild(toggleBtn);
                    manifestsSection.appendChild(actionsBar);

                    var manifestsList = document.createElement('div');
                    manifestsList.style.cssText = 'display: flex; flex-direction: column; gap: 6px;';

                    relatedManifests.forEach(function (mFile) {
                        var mCat = classifyFile(mFile, fileAppId);
                        var mStyle = badgeStyles[mCat];
                        var mIsChecked = checkedFiles[mFile.name] !== false;

                        var mDisplayName = 'Unknown';
                        var mAppId = getAppIdFromFileName(mFile.name);

                        if (mAppId) {
                            if (fetchedMetadataCache[mAppId]) {
                                var appData = fetchedMetadataCache[mAppId];
                                var name = appData.common && appData.common.name || mAppId;
                                var parentName = activeAppMetadata && activeAppMetadata.common && activeAppMetadata.common.name;
                                if (parentName && name.indexOf(parentName) === 0) {
                                    var sub = name.substring(parentName.length).trim();
                                    sub = sub.replace(/^[:\-\s]+/, '').replace(/^DLC\s*[:\-\s]*/i, '').trim();
                                    if (sub) name = sub;
                                }
                                mDisplayName = name;
                            } else {
                                fetchAppMetadata(mAppId);
                                mDisplayName = 'AppID ' + mAppId;
                            }
                        } else {
                            mDisplayName = mFile.name;
                        }

                        var mItem = document.createElement('div');
                        mItem.style.cssText = 'display:flex; justify-content:space-between; align-items:center; background:rgba(255,255,255,0.02); border:1px solid rgba(255,255,255,0.04); padding:12px 16px; border-radius:6px; font-size:13px; gap:10px; overflow:hidden; cursor:pointer; user-select:none; transition:opacity 0.15s, background 0.15s;' + (!mIsChecked ? ' opacity:0.4;' : '');

                        var mCb = document.createElement('input');
                        mCb.type = 'checkbox';
                        mCb.checked = mIsChecked;
                        mCb.style.cssText = 'width:14px; height:14px; flex-shrink:0; pointer-events:none; accent-color:#1a9fff;';
                        mItem.appendChild(mCb);

                        (function (f) {
                            mItem.onclick = function (e) {
                                e.preventDefault(); e.stopPropagation();
                                checkedFiles[f.name] = checkedFiles[f.name] === false;
                                updateFileList();
                            };
                        })(mFile);

                        var mLeftSide = document.createElement('div');
                        mLeftSide.style.cssText = 'display:flex; align-items:center; gap:8px; overflow:hidden; flex:1; min-width:0;';

                        var mBadge = document.createElement('span');
                        mBadge.innerText = mStyle.label;
                        mBadge.style.cssText = 'background:' + mStyle.color + '; color:' + mStyle.textColor + '; border:1px solid ' + mStyle.color.replace('0.15', '0.3').replace('0.08', '0.2') + '; font-size:9px; font-weight:700; text-transform:uppercase; padding:1px 4px; border-radius:3px; letter-spacing:0.5px; white-space:nowrap; flex-shrink:0;';
                        mLeftSide.appendChild(mBadge);

                        var mNameSpan = document.createElement('span');
                        mNameSpan.innerText = mDisplayName;
                        mNameSpan.style.cssText = 'color:#e0e0e0; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; flex-shrink:1; min-width:0;';
                        mLeftSide.appendChild(mNameSpan);

                        var mSizeSpan = document.createElement('span');
                        var mSizeKB = Math.round(mFile.size / 1024 * 10) / 10;
                        var mSizeStr = mSizeKB >= 1024 ? (Math.round(mSizeKB / 1024 * 10) / 10) + ' MB' : mSizeKB + ' KB';
                        mSizeSpan.innerText = mSizeStr;
                        mSizeSpan.style.cssText = 'color:#8f98a0; white-space:nowrap; flex-shrink:0; text-align:right; font-family:monospace; min-width:70px;';

                        mItem.appendChild(mLeftSide);
                        mItem.appendChild(mSizeSpan);
                        manifestsList.appendChild(mItem);
                    });
                    manifestsSection.appendChild(manifestsList);
                    contentContainer.appendChild(manifestsSection);
                }

                item.appendChild(contentContainer);
                fileList.appendChild(item);
            });

            // 3. Render all other standalone files
            standaloneFiles.forEach(function (file) {
                var fileAppId = getAppIdFromFileName(file.name);
                var cat = classifyFile(file, fileAppId);
                var style = badgeStyles[cat];

                var fileDisplayName = 'Unknown';
                var fileAppId = getAppIdFromFileName(file.name);

                if (fileAppId) {
                    if (fetchedMetadataCache[fileAppId]) {
                        var appData = fetchedMetadataCache[fileAppId];
                        var name = appData.common && appData.common.name || fileAppId;
                        var parentName = activeAppMetadata && activeAppMetadata.common && activeAppMetadata.common.name;
                        if (parentName && name.indexOf(parentName) === 0) {
                            var sub = name.substring(parentName.length).trim();
                            sub = sub.replace(/^[:\-\s]+/, '').replace(/^DLC\s*[:\-\s]*/i, '').trim();
                            if (sub) name = sub;
                        }
                        fileDisplayName = name;
                    } else {
                        fetchAppMetadata(fileAppId);
                        fileDisplayName = 'AppID ' + fileAppId;
                    }
                } else {
                    fileDisplayName = file.name;
                }

                var isChecked = checkedFiles[file.name] !== false;

                var item = document.createElement('div');
                item.style.cssText = 'display:flex; justify-content:space-between; align-items:center; background:rgba(255,255,255,0.03); border:1px solid rgba(255,255,255,0.05); padding:20px 24px; border-radius:6px; font-size:14px; gap:12px; overflow:hidden; cursor:pointer; user-select:none; transition:opacity 0.15s, background 0.15s; flex-shrink:0; width:100%; box-sizing:border-box;' + (!isChecked ? ' opacity:0.4;' : '');

                var cb = document.createElement('input');
                cb.type = 'checkbox';
                cb.checked = isChecked;
                cb.style.cssText = 'width:16px; height:16px; flex-shrink:0; pointer-events:none; accent-color:#1a9fff;';
                item.appendChild(cb);

                (function (f) {
                    item.onclick = function (e) {
                        e.preventDefault(); e.stopPropagation();
                        checkedFiles[f.name] = checkedFiles[f.name] === false;
                        updateFileList();
                    };
                })(file);

                var leftSide = document.createElement('div');
                leftSide.style.cssText = 'display:flex; align-items:center; gap:8px; overflow:hidden; flex:1; min-width:0;';

                var badge = document.createElement('span');
                badge.innerText = style.label;
                badge.style.cssText = 'background:' + style.color + '; color:' + style.textColor + '; border:1px solid ' + style.color.replace('0.15', '0.3').replace('0.08', '0.2') + '; font-size:10px; font-weight:700; text-transform:uppercase; padding:2px 6px; border-radius:4px; letter-spacing:0.5px; white-space:nowrap; flex-shrink:0;';
                leftSide.appendChild(badge);

                var nameSpan = document.createElement('span');
                nameSpan.innerText = fileDisplayName;
                nameSpan.style.cssText = 'color:#fff; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; flex-shrink:1; min-width:0;';
                leftSide.appendChild(nameSpan);

                var sizeSpan = document.createElement('span');
                var sizeKB = Math.round(file.size / 1024 * 10) / 10;
                var sizeStr = sizeKB >= 1024 ? (Math.round(sizeKB / 1024 * 10) / 10) + ' MB' : sizeKB + ' KB';
                sizeSpan.innerText = sizeStr;
                sizeSpan.style.cssText = 'color:#8f98a0; white-space:nowrap; flex-shrink:0; text-align:right; font-family:monospace; min-width:85px;';

                item.appendChild(leftSide);
                item.appendChild(sizeSpan);
                fileList.appendChild(item);
            });

            // Update the submit button state after rendering
            updateSubmitButton();
        }

        submitBtn.onclick = function (e) {
            e.preventDefault(); e.stopPropagation();

            var appid = detectAppIdFromCheckedFiles();
            if (!appid) {
                status.style.color = '#ff4d4d';
                status.innerText = 'Error: Could not detect AppID from files. Ensure a .lua file with addappid() is included.';
                return;
            }

            var filesToInstall = selectedFiles.filter(function (f) { return checkedFiles[f.name] !== false; });
            if (filesToInstall.length === 0) {
                status.style.color = '#ff4d4d';
                status.innerText = 'Error: Please select at least one file to import.';
                return;
            }

            submitBtn.innerText = 'Processing...';
            submitBtn.disabled = true;
            submitBtn.style.background = 'rgba(255, 255, 255, 0.05)';
            submitBtn.style.color = 'rgba(255,255,255,0.3)';
            submitBtn.style.cursor = 'not-allowed';
            status.style.color = '#8f98a0';
            status.innerText = 'Reading files...';

            var promises = filesToInstall.map(function (file) {
                return new Promise(function (resolve, reject) {
                    var reader = new FileReader();
                    reader.onload = function () {
                        var base64 = reader.result.split(',')[1];
                        resolve({ name: file.webkitRelativePath || file.name, content: base64 });
                    };
                    reader.onerror = function (err) { reject(err); };
                    reader.readAsDataURL(file);
                });
            });

            Promise.all(promises)
                .then(function (filesData) {
                    status.innerText = 'Installing to Steam...';
                    var payload = {
                        appid: appid,
                        files: filesData
                    };

                    return fetch('http://127.0.0.1:9001/manual-install', {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify(payload)
                    });
                })
                .then(function (r) { return r.json(); })
                .then(function (res) {
                    submitBtn.innerText = 'Import';

                    if (res.success) {
                        status.style.color = '#4caf50';
                        status.innerText = 'Success: ' + (res.message || 'Files installed successfully!');

                        // Find all distinct AppIDs from the checked lua files to trigger installations
                        var appidsToInstall = [];
                        selectedFiles.forEach(function (f) {
                            if (checkedFiles[f.name] !== false && f.name.toLowerCase().endsWith('.lua')) {
                                var content = luaContentsCache[f.name];
                                var id = getAppIdFromLuaContent(content) || getAppIdFromFileName(f.name);
                                if (id && appidsToInstall.indexOf(id) === -1) {
                                    appidsToInstall.push(id);
                                }
                            }
                        });

                        // Fallback to the main appid if no custom ones are found in selected lua files
                        if (appidsToInstall.length === 0 && appid) {
                            appidsToInstall.push(appid);
                        }

                        selectedFiles = [];
                        checkedFiles = {};
                        updateFileList();

                        // Library hot-reload is handled dynamically via Package 0 injection; no steam://install prompt needed
                    } else {
                        status.style.color = '#ff4d4d';
                        status.innerText = 'Failed: ' + (res.message || 'Unknown error');
                        updateSubmitButton();
                    }
                })
                .catch(function (err) {
                    submitBtn.innerText = 'Import';
                    status.style.color = '#ff4d4d';
                    status.innerText = 'Error connecting to backend: ' + err.message;
                    updateSubmitButton();
                });
        };
    }

    function addSlsTab() {
        var container = findSuperNavContainer();
        if (!container) return;

        var hasSlsTab = !!document.getElementById('sls-top-tab');
        var hasConfigTab = !!document.getElementById('sls-config-top-tab');
        if (hasSlsTab && hasConfigTab) return;

        // Find one of the existing tab divs (Store, Community, etc.) to clone
        var siblingTab = container.querySelector('.' + STEAM_TAB_BASE_CLASS);
        if (!siblingTab) {
            // Last resort: pick a child that is a DIV
            for (var i = 0; i < container.children.length; i++) {
                if (container.children[i].tagName === 'DIV') {
                    siblingTab = container.children[i];
                    break;
                }
            }
        }
        if (!siblingTab) return;

        if (!hasSlsTab) {
            // Clone the tab and strip inherited event listeners by round-tripping through HTML
            var rawClone = siblingTab.cloneNode(true);
            var slsTab = document.createElement('div');
            slsTab.className = rawClone.className;
            slsTab.innerHTML = rawClone.innerHTML;
            slsTab.id = 'sls-top-tab';
            // Remove active class if cloned tab had it
            slsTab.classList.remove(STEAM_TAB_ACTIVE_CLASS);

            // Set the text to 'Import Lua files' inside the known text container class
            var textEl = slsTab.querySelector('.' + STEAM_TAB_TEXT_CLASS);
            if (textEl) {
                textEl.textContent = 'Import Lua files';
            } else {
                // Fallback: find a span or the deepest text node
                textEl = slsTab.querySelector('span') || slsTab.querySelector('div') || slsTab;
                textEl.textContent = 'Import Lua files';
            }

            // Remove any href/navigation attributes from child anchors
            var anchors = slsTab.querySelectorAll('a');
            for (var ai = 0; ai < anchors.length; ai++) {
                anchors[ai].removeAttribute('href');
                anchors[ai].style.cursor = 'pointer';
            }

            container.appendChild(slsTab);
            log('Added Import Lua files tab to top bar');

            slsTab.onclick = function (e) {
                e.preventDefault();
                e.stopPropagation();
                (window.__naviShowImportLuaPage || showSlsPage)();
            };
        }

        if (!hasConfigTab) {
            var rawCloneCfg = siblingTab.cloneNode(true);
            var configTab = document.createElement('div');
            configTab.className = rawCloneCfg.className;
            configTab.innerHTML = rawCloneCfg.innerHTML;
            configTab.id = 'sls-config-top-tab';
            configTab.classList.remove(STEAM_TAB_ACTIVE_CLASS);

            var textElCfg = configTab.querySelector('.' + STEAM_TAB_TEXT_CLASS);
            if (textElCfg) {
                textElCfg.textContent = 'Navi Config';
            } else {
                textElCfg = configTab.querySelector('span') || configTab.querySelector('div') || configTab;
                textElCfg.textContent = 'Navi Config';
            }

            var anchorsCfg = configTab.querySelectorAll('a');
            for (var aiCfg = 0; aiCfg < anchorsCfg.length; aiCfg++) {
                anchorsCfg[aiCfg].removeAttribute('href');
                anchorsCfg[aiCfg].style.cursor = 'pointer';
            }

            container.appendChild(configTab);
            log('Added Navi Config tab to top bar');

            configTab.onclick = function (e) {
                e.preventDefault();
                e.stopPropagation();
                (window.__naviShowConfigPage || showConfigPage)();
            };
        }
    }

    function initSlsTabAndPage() {
        var container = findSuperNavContainer();
        if (container) {
            addSlsTab();

            // Set up a listener to hide custom pages if any other tab is clicked
            if (!container.__slsTabListenerAttached) {
                container.__slsTabListenerAttached = true;
                container.addEventListener('click', function (e) {
                    var target = e.target;
                    while (target && target !== container) {
                        if (target.classList && target.classList.contains(STEAM_TAB_BASE_CLASS)) {
                            if (target.id !== 'sls-top-tab' && target.id !== 'sls-config-top-tab') {
                                hideSlsPage();
                                hideConfigPage();
                            }
                            break;
                        }
                        target = target.parentNode;
                    }
                });
            }
        }
    }

    // Expose helpers globally on window
    window.__naviShowConfigPage = showConfigPage;
    window.__naviHideConfigPage = hideConfigPage;
    window.__naviShowImportLuaPage = showSlsPage;
    window.__naviHideImportLuaPage = hideSlsPage;

    var debounceTimer = null;
    function debouncedAddButtons() {
        if (debounceTimer) return;
        debounceTimer = requestAnimationFrame(function () {
            debounceTimer = null;
            initSlsTabAndPage();
        });
    }

    var observer = new MutationObserver(debouncedAddButtons);
    if (document.body) observer.observe(document.body, { childList: true, subtree: true });
    initSlsTabAndPage();
})();