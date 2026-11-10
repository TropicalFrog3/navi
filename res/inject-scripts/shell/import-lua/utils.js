/**
 * @file  utils.js
 * @brief Core utilities, Steam class definitions, JSZip loader, and SuperNav helpers
 */
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
