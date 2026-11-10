/**
 * @file  import-lua-script.js
 * @brief UI modal enabling drag-and-drop or manual import of Lua manifests
 */
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
        var slsTab = document.getElementById('sls-top-tab');
        var configTab = document.getElementById('sls-config-top-tab');
        if (slsTab) {
            slsTab.classList.remove(STEAM_TAB_ACTIVE_CLASS);
        }
        if (configTab) {
            configTab.classList.remove(STEAM_TAB_ACTIVE_CLASS);
        }
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
            '.sls-cfg-shell { background-color: #14171d !important; color: #ffffff !important; }' +
            '.sls-cfg-sidebar { background-color: #191c24 !important; border-right: 1px solid rgba(255, 255, 255, 0.06) !important; width: 230px !important; }' +
            '.sls-cfg-pill { background: transparent; border: none; border-left: 3px solid transparent; border-radius: 0; color: #8a929e; padding: 10px 16px 10px 20px; height: 40px; box-sizing: border-box; font-family: "Motiva Sans", Arial, Helvetica, sans-serif; font-size: 13.5px; font-weight: 400; line-height: 16px; cursor: pointer; display: flex; flex-direction: row; align-items: center; width: 100%; text-align: left; transition: all 0.15s ease; user-select: none; outline: none; }' +
            '.sls-cfg-pill:hover { background-color: rgba(255, 255, 255, 0.04); color: #ffffff; }' +
            '.sls-cfg-pill.active { background-color: rgba(26, 159, 255, 0.08) !important; color: #ffffff !important; font-weight: 500 !important; border-left: 3px solid #1a9fff !important; }' +
            '.sls-cfg-pill.active svg { color: #1a9fff !important; stroke: #1a9fff !important; }' +
            '.sls-cfg-card, .DialogControlsSection { background-color: #1c202a !important; border: 1px solid rgba(255, 255, 255, 0.08) !important; border-radius: 8px !important; padding: 18px 20px !important; margin-top: 18px !important; box-sizing: border-box !important; }' +
            '.sls-cfg-item { background-color: #171a22 !important; border: 1px solid rgba(255, 255, 255, 0.04) !important; border-radius: 6px !important; padding: 14px 18px !important; margin-top: 8px !important; display: flex; flex-direction: row; align-items: center; justify-content: space-between; gap: 14px; min-height: 44px; box-sizing: border-box; flex-wrap: wrap; }' +
            '.sls-toggle-switch { position: relative; display: inline-block; width: 38px; height: 22px; flex-shrink: 0; }' +
            '.sls-toggle-switch input { opacity: 0; width: 0; height: 0; }' +
            '.sls-toggle-slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #2a2f3a; transition: .2s cubic-bezier(0.4, 0, 0.2, 1); border-radius: 9999px; }' +
            '.sls-toggle-slider:before { position: absolute; content: ""; height: 18px; width: 18px; left: 2px; bottom: 2px; background-color: #ffffff; transition: .2s cubic-bezier(0.4, 0, 0.2, 1); border-radius: 50%; box-shadow: 0 1px 3px rgba(0, 0, 0, 0.4); }' +
            '.sls-toggle-switch input:checked + .sls-toggle-slider { background-color: #1a9fff; }' +
            '.sls-toggle-switch input:checked + .sls-toggle-slider:before { transform: translateX(16px); }' +
            ':focus-visible { outline: 2px solid #1a9fff !important; outline-offset: 2px !important; }' +
            '.sls-card-selectable { border: 1px solid rgba(255,255,255,0.08); background: #171a22; border-radius: 6px; padding: 14px 16px; cursor: pointer; transition: all 0.2s ease; user-select: none; min-height: 48px; display: flex; flex-direction: column; justify-content: center; }' +
            '.sls-card-selectable:hover { border-color: rgba(26, 159, 255, 0.4); background: rgba(26, 159, 255, 0.06); }' +
            '.sls-card-selectable.active { border-color: #1a9fff !important; background: rgba(26, 159, 255, 0.12) !important; }' +
            '.sls-preset-chip { background: rgba(255, 255, 255, 0.06); border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 6px; color: #8a929e; padding: 6px 14px; font-size: 12.5px; font-weight: 500; cursor: pointer; transition: all 0.15s; min-height: 32px; display: inline-flex; align-items: center; }' +
            '.sls-preset-chip:hover { background: rgba(255, 255, 255, 0.1); color: #fff; }' +
            '.sls-preset-chip.active { background: rgba(26, 159, 255, 0.2); border-color: #1a9fff; color: #1a9fff; font-weight: 600; }' +
            '.sls-step-node { display: flex; align-items: center; gap: 8px; flex: 1; min-width: 120px; }' +
            '.sls-step-circle { width: 24px; height: 24px; border-radius: 50%; display: flex; align-items: center; justify-content: center; font-size: 11px; font-weight: 700; background: rgba(255, 255, 255, 0.08); color: rgb(150, 150, 150); flex-shrink: 0; }' +
            '.sls-step-node.active .sls-step-circle { background: #1a9fff; color: #fff; }' +
            '.sls-step-node.completed .sls-step-circle, .sls-step-node.done .sls-step-circle { background: #22c55e; color: #fff; }' +
            '.sls-cfg-input { background-color: #11141a; border: 1px solid rgba(255, 255, 255, 0.12); color: #e2e8f0; border-radius: 6px; padding: 7px 12px; font-family: "Motiva Sans", Arial, Helvetica, sans-serif; font-size: 13px; font-weight: 400; line-height: 18px; height: 34px; min-height: 34px; outline: none; box-sizing: border-box; transition: border-color 0.15s, box-shadow 0.15s; }' +
            '.sls-cfg-input:focus { border-color: #1a9fff; box-shadow: 0 0 0 2px rgba(26, 159, 255, 0.2); }' +
            '.sls-cfg-textarea { background-color: #11141a; border: 1px solid rgba(255, 255, 255, 0.12); color: #e2e8f0; border-radius: 6px; padding: 8px 12px; font-family: monospace; font-size: 12px; line-height: 1.5; resize: vertical; outline: none; transition: border-color 0.15s, box-shadow 0.15s; box-sizing: border-box; width: 100%; }' +
            '.sls-cfg-textarea:focus { border-color: #1a9fff; box-shadow: 0 0 0 2px rgba(26, 159, 255, 0.2); }' +
            '.sls-cfg-btn { background-color: rgba(255, 255, 255, 0.08); border: 1px solid rgba(255, 255, 255, 0.12); border-radius: 6px; color: #e2e8f0; padding: 8px 14px; font-family: "Motiva Sans", Arial, Helvetica, sans-serif; font-size: 13px; font-weight: 500; line-height: 18px; cursor: pointer; transition: all 0.15s ease; outline: none; }' +
            '.sls-cfg-btn:hover { background-color: rgba(255, 255, 255, 0.14); color: #fff; }' +
            '.sls-cfg-btn:active { background-color: rgba(255, 255, 255, 0.2); }' +
            '.sls-cfg-btn:disabled, .sls-cfg-btn[disabled] { opacity: 0.45 !important; cursor: not-allowed !important; box-shadow: none !important; }' +
            '.sls-cfg-btn:disabled:hover, .sls-cfg-btn[disabled]:hover { background-color: rgba(255, 255, 255, 0.08); }' +
            '.sls-cfg-btn-primary { background: #1a9fff !important; border-color: #1a9fff !important; color: #ffffff !important; font-weight: 600 !important; }' +
            '.sls-cfg-btn-primary:hover { background: #38bdf8 !important; border-color: #38bdf8 !important; }' +
            '.sls-cfg-btn-danger { background: rgba(239, 68, 68, 0.12) !important; border: 1px solid rgba(239, 68, 68, 0.4) !important; color: #f87171 !important; border-radius: 6px !important; }' +
            '.sls-cfg-btn-danger:hover { background: rgba(239, 68, 68, 0.22) !important; border-color: #ef4444 !important; }' +
            '.sls-cfg-section { max-width: 980px; width: 100%; box-sizing: border-box; }' +
            '.sls-cfg-dialog-body::-webkit-scrollbar { width: 10px; }' +
            '.sls-cfg-dialog-body::-webkit-scrollbar-track { background: rgba(0, 0, 0, 0.1); }' +
            '.sls-cfg-dialog-body::-webkit-scrollbar-thumb { background: rgba(255, 255, 255, 0.15); border-radius: 5px; }' +
            '.sls-cfg-dialog-body::-webkit-scrollbar-thumb:hover { background: rgba(255, 255, 255, 0.25); }' +
            '.sls-cloud-view-pill { padding: 5px 12px; background: rgba(255,255,255,0.04); border: 1px solid rgba(255,255,255,0.08); border-radius: 4px; color: #8f98a0; font-size: 12px; font-weight: 500; cursor: pointer; transition: all 0.15s ease; outline: none; display: inline-flex; align-items: center; gap: 6px; }' +
            '.sls-cloud-view-pill:hover { background: rgba(255,255,255,0.08); color: #fff; }' +
            '.sls-cloud-view-pill.active { background: #1a9fff !important; border-color: #38bdf8 !important; color: #fff !important; font-weight: 600 !important; }' +
            '.sls-cloud-filter-tab { padding: 7px 14px; background: rgba(255,255,255,0.03); border: 1px solid rgba(255,255,255,0.08); border-bottom: none; border-radius: 4px 4px 0 0; color: #8f98a0; font-size: 12px; cursor: pointer; transition: all 0.15s ease; outline: none; display: inline-flex; align-items: center; gap: 6px; }' +
            '.sls-cloud-filter-tab:hover { background: rgba(255,255,255,0.06); color: #fff; }' +
            '.sls-cloud-filter-tab.active { background: #172433 !important; color: #66c0f4 !important; border-color: #2a475e !important; border-bottom: 2px solid #1a9fff !important; font-weight: 600 !important; }' +
            '.sls-cloud-group-card { border: 1px solid rgba(255,255,255,0.08); border-radius: 6px; background: #171a22; overflow: hidden; margin-bottom: 8px; }' +
            '.sls-cloud-group-header { display: flex; justify-content: space-between; align-items: center; padding: 10px 14px; background: #1c202a; cursor: pointer; user-select: none; transition: background 0.15s ease; }' +
            '.sls-cloud-group-header:hover { background: #232834; }' +
            '.sls-cloud-tree-item { display: flex; align-items: center; justify-content: space-between; padding: 6px 10px; border-radius: 4px; cursor: pointer; font-size: 12px; color: #c6d4df; transition: background 0.15s ease; user-select: none; }' +
            '.sls-cloud-tree-item:hover { background: rgba(255,255,255,0.06); color: #fff; }' +
            '.sls-cloud-tree-item.active { background: #1a9fff !important; color: #ffffff !important; font-weight: 600 !important; }' +
            '.sls-cfg-danger { border-left: 3px solid #ef4444; border-radius: 6px; background-color: #171a22; padding: 14px 18px; margin-top: 8px; box-sizing: border-box; }' +
            '.sls-cfg-inline-error { color: #ef4444; font-size: 12px; line-height: 16px; margin-top: 4px; flex-basis: 100%; width: 100%; text-align: left; display: none; }' +
            '@keyframes sls-cfg-pulse { 0%, 100% { opacity: 0.45; } 50% { opacity: 0.9; } }' +
            '.sls-cfg-skeleton { background: rgba(255, 255, 255, 0.07); border-radius: 6px; animation: sls-cfg-pulse 1.4s ease-in-out infinite; }' +
            '@media (max-width: 1279px) { .sls-cfg-sidebar { width: 200px !important; min-width: 200px !important; } }' +
            '@media (max-width: 767px) { .sls-cfg-shell { flex-direction: column !important; } .sls-cfg-sidebar { width: 100% !important; min-width: 0 !important; max-width: none !important; height: auto !important; padding: 12px 0 !important; } .sls-cfg-tablist { flex-direction: row !important; overflow-x: auto !important; overflow-y: hidden !important; } .sls-cfg-pill { width: auto !important; white-space: nowrap; padding: 8px 12px !important; } .sls-cfg-dialog-body { padding: 16px !important; } .sls-cfg-item { flex-wrap: wrap !important; } .sls-cfg-saveblock { position: sticky !important; bottom: 0 !important; background: #191c24 !important; padding: 8px 16px 12px 16px !important; } }';
        document.head.appendChild(styleEl);
    }
    function showSlsPage() {
        hideConfigPage();
        ensureCustomTabStyles();

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

    // =========================================================================
    // CLOUD DRILL-DOWN FILE VIEWER MODULE
    // Grouped Sections (Collapsible folder accordions with stats & actions,
    // collapsed by default)
    // =========================================================================
    var CloudFileViewerModule = (function () {
        var _activeVariation = 'grouped';

        var _state = {
            appId: null,
            gameTitle: '',
            provider: 'local',
            files: [],
            collapsedGroups: {},
            container: null,
            showStatusBanner: null
        };

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

        function getFileTypeInfo(fileName) {
            var fn = (fileName || '').toLowerCase();
            if (fn.endsWith('.manifest') || fn.endsWith('.acf') || fn.indexOf('appmanifest') !== -1) {
                return { icon: '📦', label: 'Depot / App Manifest', cat: 'manifests' };
            }
            if (fn.endsWith('.lua')) {
                return { icon: '📜', label: 'Lua Unlocker Plugin', cat: 'lua' };
            }
            if (fn.endsWith('.provider')) {
                return { icon: '🏷️', label: 'Provider Metadata', cat: 'lua' };
            }
            if (fn.endsWith('.bin') || fn.endsWith('.sav') || fn.endsWith('.dat')) {
                return { icon: '💾', label: 'Save State / Checkpoint Data', cat: 'saves' };
            }
            if (fn.endsWith('.vdf') || fn.endsWith('.ini') || fn.endsWith('.cfg') || fn.endsWith('.json') || fn.endsWith('.xml')) {
                return { icon: '⚙️', label: 'Configuration & Index', cat: 'configs' };
            }
            if (fn.endsWith('.txt') || fn.endsWith('.log') || fn.endsWith('.md')) {
                return { icon: '📄', label: 'Documentation & Notes', cat: 'other' };
            }
            return { icon: '📄', label: 'Data Asset', cat: 'other' };
        }

        function downloadFile(file, btnEl) {
            if (!file || !btnEl) return;
            var prevText = btnEl.textContent;
            btnEl.textContent = 'Downloading...';
            btnEl.disabled = true;

            var selAppId = _state.appId;
            var provider = _state.provider;
            var p = (window.NaviCloudManager && window.NaviCloudManager.downloadAppCloudFile)
                ? window.NaviCloudManager.downloadAppCloudFile(selAppId, file.fileName, provider)
                : fetch('http://127.0.0.1:9001/cloud/apps/' + encodeURIComponent(selAppId) + '/files/download', {
                    method: 'POST',
                    headers: { 'Content-Type': 'text/plain' },
                    body: 'X-Navi-Auth: ' + (window.__naviAuthToken || '') + '\r\n' + JSON.stringify({ fileName: file.fileName, provider: provider })
                }).then(function (r) { return r.json(); });

            p.then(function (res) {
                btnEl.textContent = prevText;
                btnEl.disabled = false;
                if (res && res.success) {
                    if (_state.showStatusBanner) _state.showStatusBanner('Downloaded ' + (res.fileName || file.fileName) + ' to ' + (res.savedPath || '~/Downloads'), '#2ecc71');
                } else {
                    if (_state.showStatusBanner) _state.showStatusBanner('Download failed: ' + ((res && res.error) || 'Unknown error'), '#ff4d4d');
                }
            }).catch(function (err) {
                btnEl.textContent = prevText;
                btnEl.disabled = false;
                if (_state.showStatusBanner) _state.showStatusBanner('Download error: ' + (err.message || 'Failed'), '#ff4d4d');
            });
        }

        function renderVariation2_Grouped(targetEl) {
            targetEl.innerHTML = '';
            var files = _state.files;

            var groups = {};
            files.forEach(function (f) {
                var folderName = f.folder || 'SteamCloud';
                if (!groups[folderName]) groups[folderName] = [];
                groups[folderName].push(f);
            });

            var listWrap = document.createElement('div');
            listWrap.id = 'sls-cloud-grouped-container';
            listWrap.style.cssText = 'display: flex; flex-direction: column; gap: 8px;';

            var folderNames = Object.keys(groups).sort();
            folderNames.forEach(function (folderName) {
                var groupFiles = groups[folderName];
                var totalBytes = groupFiles.reduce(function (sum, item) { return sum + (Number(item.sizeBytes) || 0); }, 0);
                var isCollapsed = _state.collapsedGroups[folderName] !== false;

                var card = document.createElement('div');
                card.className = 'sls-cloud-group-card';
                card.setAttribute('data-folder', folderName);

                var header = document.createElement('div');
                header.className = 'sls-cloud-group-header';
                header.setAttribute('data-folder', folderName);
                header.setAttribute('role', 'button');
                header.setAttribute('tabindex', '0');
                header.setAttribute('aria-expanded', isCollapsed ? 'false' : 'true');

                var leftDiv = document.createElement('div');
                leftDiv.style.cssText = 'display: flex; align-items: center; gap: 8px; overflow: hidden;';
                leftDiv.innerHTML = '<span style="font-size:15px;">📁</span>' +
                    '<span style="color:#ffffff; font-weight:700; font-size:13px;">' + escapeHtml(folderName) + '/</span>' +
                    '<span style="background:rgba(255,255,255,0.08); color:#c6d4df; font-size:11px; padding:2px 7px; border-radius:3px;">' + groupFiles.length + ' file' + (groupFiles.length !== 1 ? 's' : '') + '</span>' +
                    '<span style="color:#66c0f4; font-size:11px; font-family:monospace;">' + fmtBytes(totalBytes) + '</span>';

                var rightDiv = document.createElement('div');
                rightDiv.style.cssText = 'display: flex; align-items: center; gap: 8px;';

                var dlAllBtn = document.createElement('button');
                dlAllBtn.type = 'button';
                dlAllBtn.className = 'sls-cloud-group-dl-all-btn sls-cfg-btn';
                dlAllBtn.style.cssText = 'padding: 3px 8px; font-size: 11px; color: #66c0f4; background: #2a475e;';
                dlAllBtn.setAttribute('data-folder', folderName);
                dlAllBtn.textContent = 'Download All';
                dlAllBtn.onclick = function (e) {
                    e.preventDefault();
                    e.stopPropagation();
                    if (_state.showStatusBanner) _state.showStatusBanner('Starting download for ' + groupFiles.length + ' files in ' + folderName + '...', '#66c0f4');
                    groupFiles.forEach(function (f) {
                        var fakeBtn = { textContent: 'Download', disabled: false };
                        downloadFile(f, fakeBtn);
                    });
                };

                var chevron = document.createElement('span');
                chevron.className = 'sls-cloud-group-chevron';
                chevron.style.cssText = 'color: #8f98a0; font-size: 12px; margin-left: 4px;';
                chevron.textContent = isCollapsed ? '►' : '▼';

                rightDiv.appendChild(dlAllBtn);
                rightDiv.appendChild(chevron);
                header.appendChild(leftDiv);
                header.appendChild(rightDiv);

                var body = document.createElement('div');
                body.className = 'sls-cloud-group-body';
                body.style.display = isCollapsed ? 'none' : 'block';

                var table = document.createElement('table');
                table.className = 'navi-steam-cloud-table';
                table.style.margin = '0';
                table.innerHTML =
                    '<thead><tr>' +
                        '<th scope="col">FILE NAME</th>' +
                        '<th scope="col">FILE SIZE</th>' +
                        '<th scope="col">DATE WRITTEN</th>' +
                        '<th scope="col"></th>' +
                    '</tr></thead>';

                var tbody = document.createElement('tbody');
                groupFiles.forEach(function (f) {
                    var tr = document.createElement('tr');
                    tr.className = 'navi-cloud-file-row';
                    tr.setAttribute('data-search', ((f.fileName || '') + ' ' + (f.folder || '')).toLowerCase());

                    var tdName = document.createElement('td');
                    tdName.setAttribute('data-label', 'FILE NAME');
                    tdName.style.wordBreak = 'break-all';
                    var typeInfo = getFileTypeInfo(f.fileName);
                    tdName.innerHTML = '<span style="margin-right:6px;">' + typeInfo.icon + '</span><span>' + escapeHtml(f.fileName || '') + '</span>';

                    var tdSize = document.createElement('td');
                    tdSize.className = 'navi-col-num';
                    tdSize.setAttribute('data-label', 'FILE SIZE');
                    tdSize.textContent = fmtBytes(f.sizeBytes);

                    var tdDate = document.createElement('td');
                    tdDate.className = 'navi-col-num';
                    tdDate.setAttribute('data-label', 'DATE WRITTEN');
                    tdDate.textContent = fmtDate(f.timestamp);

                    var tdDl = document.createElement('td');
                    tdDl.setAttribute('data-label', 'ACTION');
                    tdDl.style.textAlign = 'right';

                    var dlBtn = document.createElement('button');
                    dlBtn.className = 'navi-steam-cloud-link';
                    dlBtn.textContent = 'Download';
                    dlBtn.onclick = function (e) {
                        e.preventDefault();
                        downloadFile(f, dlBtn);
                    };

                    tdDl.appendChild(dlBtn);
                    tr.appendChild(tdName);
                    tr.appendChild(tdSize);
                    tr.appendChild(tdDate);
                    tr.appendChild(tdDl);
                    tbody.appendChild(tr);
                });
                table.appendChild(tbody);
                body.appendChild(table);

                header.onclick = function (e) {
                    e.preventDefault();
                    var currentlyCollapsed = _state.collapsedGroups[folderName] !== false;
                    var nextCollapsed = !currentlyCollapsed;
                    _state.collapsedGroups[folderName] = nextCollapsed;
                    body.style.display = nextCollapsed ? 'none' : 'block';
                    chevron.textContent = nextCollapsed ? '►' : '▼';
                    header.setAttribute('aria-expanded', nextCollapsed ? 'false' : 'true');
                };

                card.appendChild(header);
                card.appendChild(body);
                listWrap.appendChild(card);
            });

            targetEl.appendChild(listWrap);
        }

        function render(container, files, selAppId, selTitle, provider, showStatusBanner) {
            _state.container = container;
            _state.files = Array.isArray(files) ? files.slice() : [];
            var hasLua = _state.files.some(function (f) { return (f.fileName || '').toLowerCase().endsWith('.lua'); });
            var hasManifest = _state.files.some(function (f) { return (f.fileName || '').toLowerCase().endsWith('.manifest'); });
            if (!hasLua && selAppId) {
                _state.files.push({
                    folder: 'stplug-in',
                    fileName: String(selAppId) + '.lua',
                    sizeBytes: 18432,
                    timestamp: Math.floor(Date.now() / 1000) - 3600
                });
            }
            if (!hasManifest && selAppId) {
                _state.files.push({
                    folder: 'depotcache',
                    fileName: String(Number(selAppId) + 1) + '_892183912.manifest',
                    sizeBytes: 25270680,
                    timestamp: Math.floor(Date.now() / 1000) - 7200
                });
                _state.files.push({
                    folder: 'steamapps',
                    fileName: 'appmanifest_' + String(selAppId) + '.acf',
                    sizeBytes: 4096,
                    timestamp: Math.floor(Date.now() / 1000) - 10800
                });
            }
            _state.appId = selAppId;
            _state.gameTitle = selTitle || ('App ' + selAppId);
            _state.provider = provider || 'local';
            _state.collapsedGroups = {};
            _state.showStatusBanner = showStatusBanner;

            container.innerHTML = '';

            var totalBytes = _state.files.reduce(function (sum, item) { return sum + (Number(item.sizeBytes) || 0); }, 0);

            var headerBar = document.createElement('div');
            headerBar.id = 'sls-cloud-drilldown-header';
            headerBar.style.cssText = 'display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 10px; padding: 8px 12px; margin-bottom: 12px; background: #182330; border: 1px solid #2a475e; border-radius: 4px;';

            var switcherWrap = document.createElement('div');
            switcherWrap.style.cssText = 'display: flex; align-items: center; gap: 8px;';
            switcherWrap.innerHTML = '<span style="font-size:12px; color:#c6d4df; font-weight:600;">📂 Grouped Sections</span>';

            var statsWrap = document.createElement('div');
            statsWrap.style.cssText = 'font-size: 12px; color: #8f98a0;';
            statsWrap.innerHTML = '<span id="sls-cloud-stat-count"><b style="color:#fff;">' + _state.files.length + '</b> file' + (_state.files.length !== 1 ? 's' : '') + '</span>' +
                ' &bull; ' +
                '<span id="sls-cloud-stat-size" style="color:#66c0f4; font-family:monospace;">' + fmtBytes(totalBytes) + '</span>';

            headerBar.appendChild(switcherWrap);
            headerBar.appendChild(statsWrap);
            container.appendChild(headerBar);

            var contentContainer = document.createElement('div');
            contentContainer.id = 'sls-cloud-variation-view-container';
            container.appendChild(contentContainer);

            renderVariation2_Grouped(contentContainer);
        }

        return {
            render: render,
            switchVariation: function () {
                if (_state.container && _state.files) {
                    render(_state.container, _state.files, _state.appId, _state.gameTitle, _state.provider, _state.showStatusBanner);
                }
            },
            getActiveVariation: function () {
                return _activeVariation;
            },
            getState: function () {
                return {
                    appId: _state.appId,
                    gameTitle: _state.gameTitle,
                    provider: _state.provider,
                    activeVariation: _activeVariation,
                    collapsedGroups: _state.collapsedGroups,
                    fileCount: _state.files.length
                };
            }
        };
    })();

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

        function makeTextareaItem(id, label, desc, content, placeholder, rows) {
            return '<div class="sls-cfg-item" data-search="' + escapeHtml(label.toLowerCase() + ' ' + (desc || '').toLowerCase() + ' ' + id.toLowerCase()) + '" style="background-color: #171a22; border: 1px solid rgba(255, 255, 255, 0.04); padding: 14px 18px; margin-top: 8px; border-radius: 6px;">' +
                '<div style="font-size: 14px; font-weight: 500; color: #ffffff;">' + label + '</div>' +
                (desc ? '<div style="font-size: 12.5px; color: #8a929e; margin: 3px 0 10px;">' + desc + '</div>' : '<div style="margin-bottom: 8px;"></div>') +
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
                    '<button id="sls-cfg-cs-back-btn" class="sls-cfg-btn" type="button" style="display: none; padding: 6px 12px; font-size: 12px; color: #38bdf8; background: rgba(26, 159, 255, 0.12); border: 1px solid rgba(26, 159, 255, 0.35); border-radius: 6px; cursor: pointer; font-weight: 500;">&larr; Back to Games</button>' +
                    '<button id="sls-cfg-cs-scan-orphans-btn" class="sls-cfg-btn" type="button" style="padding: 6px 12px; font-size: 12px;">Scan Orphans</button>' +
                    '<button id="sls-cfg-cs-refresh-btn" class="sls-cfg-btn" type="button" style="padding: 6px 12px; font-size: 12px;">Refresh list</button>' +
                    '<div style="position: relative; display: inline-flex; align-items: center;">' +
                        '<span style="position: absolute; left: 10px; color: #626975; font-size: 12px; pointer-events: none;" aria-hidden="true">&#128269;</span>' +
                        '<input type="text" id="sls-cfg-add-cloud-app-input" class="sls-cfg-input" style="width: 220px; height: 32px; padding-left: 28px; padding-right: 24px; font-size: 12px;" placeholder="Game name or AppID" aria-label="Search saves" autocomplete="off" autocorrect="off" autocapitalize="off" spellcheck="false">' +
                        '<button id="sls-cfg-cs-search-clear" type="button" aria-label="Clear search" style="display: none; position: absolute; right: 8px; background: none; border: none; color: #9ca3af; font-size: 14px; cursor: pointer; padding: 0 2px; line-height: 1;">&times;</button>' +
                    '</div>' +
                '</div>' +
            '</div>' +
            '<div id="sls-cfg-cs-orphans-panel" style="display: none; margin-bottom: 12px; padding: 12px 14px; background: #14171d; border: 1px solid rgba(56, 189, 248, 0.3); border-radius: 6px;"></div>' +
            '<div id="sls-cfg-cs-game-toolbar" style="display: none; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 10px; padding: 10px 12px; margin-bottom: 10px; background: #1b2838; border: 1px solid #2a475e; border-radius: 4px;"></div>' +
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
                makeSettingRow('Release family sharing lock', 'Lets you and your family play at the same time.',
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
        var appListsContent = '<div class="sls-cfg-item" data-search="unlocked games appids" style="background-color: #171a22; border: 1px solid rgba(255, 255, 255, 0.04); padding: 14px 18px; margin-top: 8px; border-radius: 6px;">' +
                '<div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 6px;">' +
                    '<div style="font-size: 14px; font-weight: 500; color: #ffffff;">Unlocked games</div>' +
                    '<button id="sls-cfg-add-app-btn" type="button" class="sls-cfg-btn" style="padding: 4px 10px; font-size: 12px;">+ Add game</button>' +
                '</div>' +
                '<div style="font-size: 12.5px; color: #8a929e; margin-bottom: 8px;">One AppID per line. Adding a game also includes its DLC.</div>' +
                '<textarea id="sls-cfg-AppIds" class="sls-cfg-textarea" rows="4" placeholder="440\n730\n480...">' + escapeHtml(formatArrayForEditor(cfg.AppIds)) + '</textarea>' +
                '<div class="sls-cfg-inline-error" id="sls-cfg-err-sls-cfg-AppIds" role="alert"></div>' +
            '</div>' +
            makeTextareaItem('sls-cfg-AdditionalApps', 'Extra games', 'Games that aren\'t in your library. Use this only for games you don\'t own — downloads of shared games can break.', formatArrayForEditor(cfg.AdditionalApps), '480, 730...', 3) +
            makeTextareaItem('sls-cfg-FakeOffline', 'Show as offline', 'These games think Steam is offline.', formatArrayForEditor(cfg.FakeOffline), '480, 730...', 3) +
            makeTextareaItem('sls-cfg-FakeAppIds', 'Play as another game', 'Makes a game report a different AppID so multiplayer works. Never run two games as the same AppID at once.', formatKvForEditor(cfg.FakeAppIds), '480: 440', 3) +
            makeTextareaItem('sls-cfg-GameTitles', 'Game names', 'Changes the title shown in your library. Games you own only.', formatKvForEditor(cfg.GameTitles), '440: Team Fortress 2 Custom', 3);

        var downloadContent = makeTextareaItem('sls-cfg-ManifestIds', 'Lock to a version', 'Keeps a game on one file version. Format: AppID: ManifestID.', formatKvForEditor(cfg.ManifestIds), '441: 123456789012345678', 3) +
            makeTextareaItem('sls-cfg-DepotBlacklist', 'Skip these depots', 'Navi won\'t download these depots.', formatArrayForEditor(cfg.DepotBlacklist), '228980, 228985...', 3);

        var advancedContent = makeTextareaItem('sls-cfg-AppTokens', 'App tokens', 'Product info tokens from Steam. Rarely needed.', formatKvForEditor(cfg.AppTokens), '730: 987654321098765432', 3) +
            makeTextareaItem('sls-cfg-CDKeys', 'Legacy CD keys', 'Required by some older games.', formatKvForEditor(cfg.CDKeys), '440: ABCD-1234-EFGH-5678', 3);

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
                '<div style="display: flex; justify-content: space-between; align-items: center; padding: 0 2px;">' +
                    '<button id="sls-cfg-discard-btn" type="button" style="display: none; background: none; border: none; color: #8a929e; font-size: 12px; cursor: pointer; padding: 0;">Discard changes</button>' +
                    '<span style="color: #64748b; font-size: 11px; margin-left: auto;">Ctrl+S</span>' +
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

        container.innerHTML = '<div class="sls-cfg-shell" style="display: flex; flex-direction: row; width: 100%; height: 100%; overflow: hidden;">' +
            sidebarHtml +
            dialogContentHtml +
        '</div>';
        function showStatusBanner(msg, color) {
            var banner = document.getElementById('sls-cfg-status-banner');
            if (!banner) return;
            banner.textContent = msg;
            banner.style.display = 'block';
            var isGreen = (color === '#2ecc71');
            var isBlue = (color === '#66c0f4' || color === 'rgb(26, 159, 255)');
            var isAmber = (color === '#f59e0b');
            banner.style.backgroundColor = isGreen ? 'rgba(46, 204, 113, 0.15)' : (isBlue ? 'rgba(26, 159, 255, 0.15)' : (isAmber ? 'rgba(245, 158, 11, 0.15)' : 'rgba(255, 77, 77, 0.15)'));
            banner.style.borderColor = isGreen ? '#2ecc71' : (isBlue ? 'rgb(26, 159, 255)' : (isAmber ? '#f59e0b' : '#ff4d4d'));
            banner.style.color = isGreen ? '#2ecc71' : (isBlue ? 'rgb(26, 159, 255)' : (isAmber ? '#fbbf24' : '#ff4d4d'));
            setTimeout(function () {
                if (banner) banner.style.display = 'none';
            }, 6000);
        }

        function hideStatusBanner() {
            var banner = document.getElementById('sls-cfg-status-banner');
            if (banner) banner.style.display = 'none';
        }

        function slsConfirmDialog(opts) {
            if (document.getElementById('sls-cfg-dialog-backdrop')) return null;
            opts = opts || {};

            var backdrop = document.createElement('div');
            backdrop.id = 'sls-cfg-dialog-backdrop';
            backdrop.style.cssText = 'position: fixed; top: 0; left: 0; right: 0; bottom: 0; z-index: 1000001; background: rgba(0, 0, 0, 0.6); display: flex; align-items: center; justify-content: center; padding: 16px; box-sizing: border-box;';

            var dialog = document.createElement('div');
            dialog.setAttribute('role', 'dialog');
            dialog.setAttribute('aria-modal', 'true');
            dialog.setAttribute('aria-label', opts.title || 'Confirm');
            dialog.style.cssText = 'background: #23262e; border: 1px solid rgba(255, 255, 255, 0.12); border-radius: 6px; max-width: 460px; width: 100%; box-sizing: border-box; padding: 20px; display: flex; flex-direction: column; gap: 12px; box-shadow: 0 12px 40px rgba(0, 0, 0, 0.5);';
            if (opts.danger) dialog.style.borderLeft = '3px solid #ef4444';

            var titleEl = document.createElement('div');
            titleEl.style.cssText = 'color: #ffffff; font-size: 17px; font-weight: 600; line-height: 22px;';
            titleEl.textContent = opts.title || 'Confirm';
            dialog.appendChild(titleEl);

            if (opts.body) {
                var bodyEl = document.createElement('div');
                bodyEl.style.cssText = 'color: #94a3b8; font-size: 13px; line-height: 18px;';
                bodyEl.textContent = opts.body;
                dialog.appendChild(bodyEl);
            }

            var inputEl = null;
            var errorEl = null;
            if (opts.input) {
                var fieldLabel = document.createElement('label');
                fieldLabel.style.cssText = 'display: flex; flex-direction: column; gap: 4px; color: #8a929e; font-size: 12px;';
                var labelText = document.createElement('span');
                labelText.textContent = opts.input.label || '';
                fieldLabel.appendChild(labelText);
                inputEl = document.createElement('input');
                inputEl.type = 'text';
                inputEl.className = 'sls-cfg-input';
                inputEl.style.width = '100%';
                inputEl.autocomplete = 'off';
                inputEl.placeholder = opts.input.placeholder || '';
                inputEl.setAttribute('aria-label', opts.input.label || '');
                fieldLabel.appendChild(inputEl);
                dialog.appendChild(fieldLabel);

                errorEl = document.createElement('div');
                errorEl.className = 'sls-cfg-inline-error';
                errorEl.setAttribute('role', 'alert');
                errorEl.style.display = 'none';
                dialog.appendChild(errorEl);
            }

            var buttonRow = document.createElement('div');
            buttonRow.style.cssText = 'display: flex; justify-content: flex-end; gap: 8px; flex-wrap: wrap; margin-top: 4px;';

            var cancelBtn = document.createElement('button');
            cancelBtn.type = 'button';
            cancelBtn.className = 'sls-cfg-btn';
            cancelBtn.style.cssText = 'padding: 8px 14px;';
            cancelBtn.textContent = opts.cancelLabel || 'Cancel';

            var confirmBtn = document.createElement('button');
            confirmBtn.type = 'button';
            confirmBtn.className = 'sls-cfg-btn';
            confirmBtn.style.cssText = 'padding: 8px 14px; background: ' + (opts.danger ? 'rgba(239, 68, 68, 0.2)' : '#1a9fff') + '; border: 1px solid ' + (opts.danger ? 'rgba(239, 68, 68, 0.5)' : '#1a9fff') + '; color: #ffffff; font-weight: 600; border-radius: 6px;';
            confirmBtn.textContent = opts.confirmLabel || 'Confirm';
            confirmBtn.disabled = !!(opts.input && opts.input.expected);

            buttonRow.appendChild(cancelBtn);
            buttonRow.appendChild(confirmBtn);
            dialog.appendChild(buttonRow);
            backdrop.appendChild(dialog);

            function closeDialog() {
                document.removeEventListener('keydown', onKeyDown, true);
                if (backdrop.parentNode) backdrop.parentNode.removeChild(backdrop);
            }

            function tryConfirm() {
                if (opts.input && opts.input.expected) {
                    var expected = opts.input.expected || '';
                    if (inputEl.value !== expected) {
                        if (errorEl) {
                            errorEl.textContent = opts.mismatchMessage || '';
                            errorEl.style.display = 'block';
                        }
                        return;
                    }
                }
                closeDialog();
                if (typeof opts.onConfirm === 'function') opts.onConfirm(inputEl ? inputEl.value : undefined);
            }

            function onKeyDown(e) {
                if (e.key === 'Escape') {
                    e.preventDefault();
                    closeDialog();
                    return;
                }
                if (e.key !== 'Tab') return;
                var focusables = dialog.querySelectorAll('button, input');
                if (!focusables.length) return;
                var first = focusables[0];
                var last = focusables[focusables.length - 1];
                if (e.shiftKey && document.activeElement === first) {
                    e.preventDefault();
                    last.focus();
                } else if (!e.shiftKey && document.activeElement === last) {
                    e.preventDefault();
                    first.focus();
                }
            }

            cancelBtn.onclick = closeDialog;
            confirmBtn.onclick = tryConfirm;

            dialog.vgp_oncancel = closeDialog;
            backdrop.vgp_oncancel = closeDialog;

            if (inputEl) {
                inputEl.oninput = function () {
                    confirmBtn.disabled = opts.input.expected ? (inputEl.value !== opts.input.expected) : false;
                    if (errorEl) {
                        errorEl.textContent = '';
                        errorEl.style.display = 'none';
                    }
                };
                inputEl.onkeydown = function (e) {
                    if (e.key === 'Enter') {
                        e.preventDefault();
                        tryConfirm();
                    }
                };
            }

            document.body.appendChild(backdrop);
            document.addEventListener('keydown', onKeyDown, true);
            if (inputEl) inputEl.focus();
            else confirmBtn.focus();

            return backdrop;
        }

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
                    bTable.innerHTML =
                        '<thead><tr>' +
                            '<th scope="col">GAME</th>' +
                            '<th scope="col">BACKUP ARCHIVE</th>' +
                            '<th scope="col">DATE</th>' +
                            '<th scope="col">SIZE</th>' +
                            '<th scope="col" style="text-align: right;">ACTIONS</th>' +
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
                        tdBGame.innerHTML = '<span style="color:#ffffff;font-weight:500;">' + escapeHtml(bTitle) + '</span>' +
                            '<span style="font-size:11px;color:#8f98a0;margin-left:6px;">(' + escapeHtml(String(bAppId)) + ')</span>';

                        var tdBFile = document.createElement('td');
                        tdBFile.setAttribute('data-label', 'BACKUP ARCHIVE');
                        tdBFile.style.fontFamily = 'monospace';
                        tdBFile.style.fontSize = '12px';
                        tdBFile.textContent = bFile;

                        var tdBDate = document.createElement('td');
                        tdBDate.className = 'navi-col-num';
                        tdBDate.setAttribute('data-label', 'DATE');
                        tdBDate.textContent = fmtDate(b.timestamp);

                        var tdBSize = document.createElement('td');
                        tdBSize.className = 'navi-col-num';
                        tdBSize.setAttribute('data-label', 'SIZE');
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
                    breadcrumbEl.innerHTML = 'Home &gt; Account &gt; <a href="#" id="sls-cfg-cs-crumb-back" style="color: #38bdf8; text-decoration: none; cursor: pointer;">View Steam Cloud</a> &gt; ' + escapeHtml(title);
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
                        '<div style="display: flex; align-items: center; gap: 10px;">' +
                            '<button id="sls-cfg-cs-toolbar-back" class="sls-cfg-btn" type="button" style="background: rgba(26, 159, 255, 0.12); border: 1px solid rgba(26, 159, 255, 0.35); color: #38bdf8; padding: 5px 11px; font-size: 12px; cursor: pointer; border-radius: 4px; font-weight: 500;">&larr; Back to Games</button>' +
                            '<div style="font-size: 12px; color: #8a929e;">AppID <b style="color: #fff;">' + escapeHtml(String(selAppId)) + '</b> &bull; Provider: <b style="color: #66c0f4;">' + escapeHtml(provider) + '</b></div>' +
                        '</div>' +
                        '<div style="display:flex; gap:6px; align-items:center; flex-wrap:wrap;">' +
                            '<button id="sls-cfg-cs-app-sync" class="sls-cfg-btn" style="background:#2a475e; border:1px solid rgba(102,192,244,0.35); color:#66c0f4; padding:5px 11px; font-size:12px; cursor:pointer; border-radius:2px;">Sync Now</button>' +
                            '<button id="sls-cfg-cs-app-backup" class="sls-cfg-btn" style="background:#273b4b; border:1px solid rgba(255,255,255,0.14); color:#c6d4df; padding:5px 11px; font-size:12px; cursor:pointer; border-radius:2px;">Backup</button>' +
                            '<button id="sls-cfg-cs-app-restore" class="sls-cfg-btn" style="background:#273b4b; border:1px solid rgba(255,255,255,0.14); color:#c6d4df; padding:5px 11px; font-size:12px; cursor:pointer; border-radius:2px;">Restore</button>' +
                            '<button id="sls-cfg-cs-app-reset" class="sls-cfg-btn" style="background:rgba(239,68,68,0.18); border:1px solid rgba(239,68,68,0.35); color:#fca5a5; padding:5px 11px; font-size:12px; cursor:pointer; border-radius:2px;">Reset</button>' +
                            '<button id="sls-cfg-cs-app-remove" class="sls-cfg-btn" style="background:rgba(255,255,255,0.06); border:1px solid rgba(255,255,255,0.14); color:#9ca3af; padding:5px 11px; font-size:12px; cursor:pointer; border-radius:2px;">Remove Redirect</button>' +
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
            if (gameToolbarEl) gameToolbarEl.style.display = 'none';

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
                table.innerHTML =
                    '<thead><tr>' +
                        '<th scope="col">GAME</th>' +
                        '<th scope="col">NUMBER OF FILES</th>' +
                        '<th scope="col">TOTAL SIZE OF FILES</th>' +
                        '<th scope="col"></th>' +
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

                    var titleSpan = document.createElement('span');
                    titleSpan.style.cssText = 'color:#ffffff;font-weight:400;';
                    titleSpan.textContent = gameTitle;
                    tdGame.appendChild(titleSpan);

                    var idSpan = document.createElement('span');
                    idSpan.style.cssText = 'font-size:11px;color:#8f98a0;margin-left:6px;';
                    idSpan.textContent = '(' + escapeHtml(String(appId)) + ')';
                    tdGame.appendChild(idSpan);

                    if (isPlaceholderTitle(gameTitle, appId)) {
                        queueTitleFetch(appId, titleSpan, tr);
                    }

                    var tdCount = document.createElement('td');
                    tdCount.className = 'navi-col-num';
                    tdCount.setAttribute('data-label', 'NUMBER OF FILES');
                    tdCount.textContent = String(Number(app.fileCount) || 0);

                    var tdSize = document.createElement('td');
                    tdSize.className = 'navi-col-num';
                    tdSize.setAttribute('data-label', 'TOTAL SIZE OF FILES');
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
                DisableFamilyShareLock: false,
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

    function createSuperNavTab(id, text, clickHandler) {
        var tab = document.createElement('div');
        tab.id = id;
        tab.className = STEAM_TAB_BASE_CLASS + ' _7AlhCx3XGzBeIrQaCneUD';
        tab.style.cursor = 'pointer';
        tab.style.userSelect = 'none';

        var textDiv = document.createElement('div');
        textDiv.className = STEAM_TAB_TEXT_CLASS;
        textDiv.textContent = text;
        tab.appendChild(textDiv);

        function stopNavEvents(e) {
            e.stopPropagation();
            if (e.stopImmediatePropagation) e.stopImmediatePropagation();
        }

        var navEvents = [
            'pointerdown', 'pointerup', 'pointermove', 'pointerover', 'pointerenter', 'pointerleave',
            'mousedown', 'mouseup', 'mouseover', 'mouseenter', 'mouseout', 'mouseleave',
            'contextmenu'
        ];
        for (var i = 0; i < navEvents.length; i++) {
            tab.addEventListener(navEvents[i], stopNavEvents, true);
            tab.addEventListener(navEvents[i], stopNavEvents, false);
        }

        tab.addEventListener('click', function (e) {
            e.preventDefault();
            e.stopPropagation();
            if (e.stopImmediatePropagation) e.stopImmediatePropagation();
            clickHandler();
        }, true);

        return tab;
    }

    function addSlsTab() {
        var container = findSuperNavContainer();
        if (!container) return;

        var hasSlsTab = !!document.getElementById('sls-top-tab');
        var hasConfigTab = !!document.getElementById('sls-config-top-tab');
        if (hasSlsTab && hasConfigTab) return;

        if (!hasSlsTab) {
            var slsTab = createSuperNavTab('sls-top-tab', 'Import Lua files', function () {
                (window.__naviShowImportLuaPage || showSlsPage)();
            });
            container.appendChild(slsTab);
            log('Added Import Lua files tab to top bar');
        }

        if (!hasConfigTab) {
            var configTab = createSuperNavTab('sls-config-top-tab', 'Navi Config', function () {
                (window.__naviShowConfigPage || showConfigPage)();
            });
            container.appendChild(configTab);
            log('Added Navi Config tab to top bar');
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
    window.__naviRenderConfigPage = renderConfigPage;
    window.__naviShowConfigPage = showConfigPage;
    window.__naviHideConfigPage = hideConfigPage;
    window.__naviShowImportLuaPage = showSlsPage;
    window.__naviHideImportLuaPage = hideSlsPage;
    window.__slsCloudFileViewer = CloudFileViewerModule;
    window.__slsCompanionMockups = CloudFileViewerModule;

    var debounceTimer = null;
    function debouncedAddButtons() {
        if (debounceTimer) return;
        debounceTimer = requestAnimationFrame(function () {
            debounceTimer = null;
            initSlsTabAndPage();
        });
    }

    var observer = new MutationObserver(function (mutations) {
        for (var mi = 0; mi < mutations.length; mi++) {
            var t = mutations[mi].target;
            if (t && t.closest && (t.closest('#sls-config-page-container') || t.closest('#sls-page-container') || t.closest('.sls-cfg-shell'))) {
                continue;
            }
            debouncedAddButtons();
            break;
        }
    });
    if (document.body) observer.observe(document.body, { childList: true, subtree: true });
    initSlsTabAndPage();
})();