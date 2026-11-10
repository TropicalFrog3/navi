/**
 * @file  supernav.js
 * @brief SuperNav top-bar tab injector, history sync, and DOM mutation observer
 */
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
