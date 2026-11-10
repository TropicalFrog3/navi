/**
 * @file  main.js
 * @brief Modular entry point for library app details page enhancements
 */
var noLuaAppIds = {};
var pendingChecks = {}; // keyed by appid + '_' + container-random-key

function checkAndCreateButton(manageContainer, appid, retryCount) {
    if (!retryCount) retryCount = 0;

    var parentNode = manageContainer.parentNode;
    var containerKey = appid + '_' + (parentNode ? parentNode.dataset.slsKey : appid);

    if (retryCount > 5) {
        delete pendingChecks[containerKey];
        return;
    }

    fetch('http://127.0.0.1:9001/check?id=' + appid)
        .then(function(r) { return r.json(); })
        .then(function(data) {
            delete pendingChecks[containerKey];

            if (data.exists || data.hasLua || (data.provider && data.provider.length > 0) || data.pending) {
                if (parentNode) delete parentNode.dataset.slsNoLua;
                createRemoveButton(manageContainer, appid, data.pending, data.onlineFixInstalled, data.autoCrackInstalled, data.provider);
            } else {
                if (parentNode) parentNode.dataset.slsNoLua = appid;
            }
        })
        .catch(function() {
            var delay = Math.min(1000 * Math.pow(2, retryCount), 8000);
            log('check failed for ' + appid + ', retry #' + (retryCount + 1) + ' in ' + delay + 'ms');
            setTimeout(function() {
                if (manageContainer.parentNode) {
                    checkAndCreateButton(manageContainer, appid, retryCount + 1);
                } else {
                    delete pendingChecks[containerKey];
                }
            }, delay);
        });
}

function addRemoveLuaButton() {
    var btns = document.querySelectorAll('div[aria-label="Manage"]');

    btns.forEach(function(manageBtn) {
        var manageContainer = manageBtn.parentNode;
        if (!manageContainer || !manageContainer.parentNode) return;

        var appid = extractAppId(manageBtn);
        if (!appid) return;

        var parentNode = manageContainer.parentNode;

        // If this exact container is already injected for this appid, skip
        if (parentNode.dataset.slsInjected === appid) return;

        // If this container was already checked and confirmed to have no lua, skip
        if (parentNode.dataset.slsNoLua === appid) return;

        // If a /check is already in-flight for this appid+container, skip
        var containerKey = appid + '_' + (parentNode.dataset.slsKey || (parentNode.dataset.slsKey = Math.random().toString(36).slice(2)));
        if (pendingChecks[containerKey]) return;
        pendingChecks[containerKey] = true;

        checkAndCreateButton(manageContainer, appid, 0);
    });
}

var debounceTimer = null;
function debouncedAddButtons() {
    if (debounceTimer) return;
    debounceTimer = requestAnimationFrame(function() {
        debounceTimer = null;
        addRemoveLuaButton();
    });
}

window.__slsRefreshButtons = function(targetAppId) {
    pendingChecks = {};
    document.querySelectorAll('[data-sls-injected], [data-sls-no-lua]').forEach(function(el) {
        if (!targetAppId || el.dataset.slsInjected === String(targetAppId) || el.dataset.slsNoLua === String(targetAppId)) {
            delete el.dataset.slsInjected;
            delete el.dataset.slsNoLua;
        }
    });
    addRemoveLuaButton();
};

var observer = new MutationObserver(debouncedAddButtons);
if (document.body) observer.observe(document.body, { childList: true, subtree: true });
ensureStyles();
addRemoveLuaButton();

