/**
 * @file  download-tracker.js
 * @brief Progress tracking, row loading bar animations, and download session state management
 */
// ── Row loading bar ───────────────────────────────────────────────────
// Phases reported via pushStatus (button span text) → progress milestones.
// Between milestones the bar crawls automatically; it snaps forward when the
// next phase text is detected. On success it completes to 100% then fades
// out; on failure it turns red and fades out after a short pause.

var SLS_PHASE_MAP = [
    { match: /checking/i,    pct: 5  },
    { match: /trying/i,      pct: 35 },
    { match: /browser/i,     pct: 45 },
    { match: /extracting/i,  pct: 65 },
    { match: /installing/i,  pct: 82 },
    { match: /installed/i,   pct: 100 },
];
var SLS_FAIL_TEXTS = /not available|failed|not found|steam not|no api|no decryption key|missing decryption key/i;

/**
 * Attach a loading bar to `luaLink`'s parent wrapper and poll the span
 * text for phase updates until the download completes or fails.
 * Works identically for single-provider and auto-download.
 */
function startRowLoadingBar(luaBtn, luaLink) {
    injectLoadingBarStyles();

    // Find the actual clickable <a> element's parent to host the bar
    var host = luaLink.parentElement || luaBtn;
    host.classList.add('sls-bar-host');

    // Remove any stale bar
    var oldBar = host.querySelector('.sls-bar-fill');
    if (oldBar) oldBar.remove();

    var bar = document.createElement('div');
    bar.className = 'sls-bar-fill';
    host.insertBefore(bar, host.firstChild);

    var currentPct  = 0;
    var targetPct   = 0;
    var pollId      = null;
    var lastText    = '';
    var finished    = false;

    function setTarget(pct) {
        if (pct <= targetPct) return;
        targetPct = pct;
        bar.style.width = targetPct + '%';
    }

    function finish(success) {
        if (finished) return;
        finished = true;
        clearInterval(pollId);
        if (success) {
            bar.classList.add('sls-bar-done');
            bar.style.width = '100%';
            setTimeout(function() {
                bar.style.transition = 'opacity 0.6s ease';
                bar.style.opacity = '0';
                setTimeout(function() {
                    bar.remove();
                    host.classList.remove('sls-bar-host');
                }, 650);
            }, 900);
        } else {
            bar.classList.add('sls-bar-fail');
            bar.style.width = '100%';
            setTimeout(function() {
                bar.style.transition = 'opacity 0.8s ease';
                bar.style.opacity = '0';
                setTimeout(function() {
                    bar.remove();
                    host.classList.remove('sls-bar-host');
                }, 850);
            }, 2200);
        }
    }

    // Crawl the bar a tiny bit every tick so it always looks alive
    function crawl() {
        if (finished) return;
        currentPct = parseFloat(bar.style.width) || 0;
        // Slow organic crawl up to targetPct — max 0.18% per tick
        if (currentPct < targetPct) {
            var step = Math.max(0.05, (targetPct - currentPct) * 0.04);
            currentPct = Math.min(targetPct, currentPct + step);
            bar.style.width = currentPct + '%';
        }
    }
    var crawlId = setInterval(crawl, 80);

    // Start at 2% immediately so the bar is visible right away
    setTarget(2);

    pollId = setInterval(function() {
        var span = luaLink.querySelector('span');
        var text = span ? span.innerText : '';
        if (text === lastText) return;
        lastText = text;

        if (SLS_FAIL_TEXTS.test(text)) {
            clearInterval(crawlId);
            finish(false);
            return;
        }
        if (/installed!?/i.test(text)) {
            clearInterval(crawlId);
            finish(true);
            return;
        }
        for (var i = 0; i < SLS_PHASE_MAP.length; i++) {
            if (SLS_PHASE_MAP[i].match.test(text)) {
                setTarget(SLS_PHASE_MAP[i].pct);
                break;
            }
        }
    }, 150);
}

var globalDownloadSessions = {};

function saveSessionToStorage(appid) {
    var session = globalDownloadSessions[appid];
    if (!session) return;
    try {
        var dataToSave = {
            requestedProviders: session.requestedProviders,
            providerStates: session.providerStates,
            allowedIndices: session.allowedIndices,
            currentActiveIdx: session.currentActiveIdx,
            targetPct: session.targetPct,
            finished: session.finished
        };
        localStorage.setItem('sls-session-' + appid, JSON.stringify(dataToSave));
    } catch(e) {}
}

function loadSessionFromStorage(appid) {
    try {
        var raw = localStorage.getItem('sls-session-' + appid);
        if (!raw) return null;
        var data = JSON.parse(raw);
        return data;
    } catch(e) {
        return null;
    }
}

function getOrCreateGlobalSession(appid) {
    if (!globalDownloadSessions[appid]) {
        var saved = loadSessionFromStorage(appid);
        if (saved) {
            globalDownloadSessions[appid] = {
                requestedProviders: saved.requestedProviders || {},
                providerStates: saved.providerStates || {},
                allowedIndices: saved.allowedIndices || [],
                currentActiveIdx: typeof saved.currentActiveIdx === 'number' ? saved.currentActiveIdx : -1,
                targetPct: saved.targetPct || 0,
                finished: !!saved.finished,
                lastText: '',
                pollId: null,
                crawlId: null
            };
        } else {
            globalDownloadSessions[appid] = {
                requestedProviders: {},
                providerStates: {},
                allowedIndices: [],
                currentActiveIdx: -1,
                targetPct: 0,
                finished: false,
                lastText: '',
                pollId: null,
                crawlId: null
            };
        }
    }
    return globalDownloadSessions[appid];
}

function syncModalUI(appid) {
    var overlay = document.getElementById('sls-overlay-modal');
    if (!overlay || overlay.dataset.slsAppid !== String(appid)) return;
    var session = globalDownloadSessions[appid];
    if (!session) return;

    Object.keys(session.requestedProviders).forEach(function(pIdxStr) {
        var pIdx = parseInt(pIdxStr, 10);
        var btn = overlay.querySelector('[data-sls-provider-download="' + pIdx + '"]');
        if (btn) {
            // btn.disabled = true;
            // btn.style.opacity = '0.4';
            // btn.style.cursor = 'not-allowed';
        }
    });

    Object.keys(session.providerStates).forEach(function(pIdxStr) {
        var pIdx = parseInt(pIdxStr, 10);
        var state = session.providerStates[pIdxStr];
        var rowEl = overlay.querySelector('[data-sls-provider="' + pIdx + '"]');
        if (!rowEl) return;
        var bar = rowEl.querySelector('.sls-bar-fill');
        if (!bar) {
            injectLoadingBarStyles();
            bar = document.createElement('div');
            bar.className = 'sls-bar-fill';
            rowEl.insertBefore(bar, rowEl.firstChild);
        }
        bar.style.width = state.pct + '%';
        if (state.status === 'done') {
            bar.classList.remove('sls-bar-fail');
            bar.classList.add('sls-bar-done');
        } else if (state.status === 'fail') {
            bar.classList.remove('sls-bar-done');
            bar.classList.add('sls-bar-fail');
        } else {
            bar.classList.remove('sls-bar-done', 'sls-bar-fail');
        }
    });
}

function findProviderIndexFromText(text) {
    if (!text) return -1;
    var lower = text.toLowerCase();
    for (var i = 0; i < luaProviders.length; i++) {
        var keyName = luaProviders[i].name.split(' ')[0].toLowerCase();
        if (lower.indexOf(keyName) !== -1) {
            return i;
        }
    }
    return -1;
}

function startGlobalDownloadTracking(appid, newAllowedIndices) {
    injectLoadingBarStyles();
    var session = getOrCreateGlobalSession(appid);
    session.allowedIndices = newAllowedIndices || session.allowedIndices;

    if (newAllowedIndices) {
        newAllowedIndices.forEach(function(pIdx) {
            session.requestedProviders[pIdx] = true;
        });
    }

    if (session.currentActiveIdx === -1 || session.finished) {
        if (session.allowedIndices && session.allowedIndices.length > 0) {
            session.currentActiveIdx = session.allowedIndices[0];
        }
        session.targetPct = 5;
        session.finished = false;
        if (session.currentActiveIdx !== -1) {
            session.providerStates[session.currentActiveIdx] = { status: 'active', pct: 5 };
        }
    }

    saveSessionToStorage(appid);
    syncModalUI(appid);

    if (session.crawlId) clearInterval(session.crawlId);
    session.crawlId = setInterval(function() {
        if (session.finished || session.currentActiveIdx === -1) return;
        var st = session.providerStates[session.currentActiveIdx];
        if (!st) return;
        if (st.pct < session.targetPct) {
            var step = Math.max(0.05, (session.targetPct - st.pct) * 0.04);
            st.pct = Math.min(session.targetPct, st.pct + step);
            saveSessionToStorage(appid);
            syncModalUI(appid);
        }
    }, 80);

    if (session.pollId) clearInterval(session.pollId);
    session.pollId = setInterval(function() {
        var pageBtn = document.querySelector('.sls-lua-btn[data-sls-appid="' + appid + '"]');
        var text = pageBtn ? (pageBtn.dataset.slsStatus || (pageBtn.querySelector('a span') ? pageBtn.querySelector('a span').innerText : '')) : '';
        if (text === session.lastText) return;
        session.lastText = text;

        if (SLS_FAIL_TEXTS.test(text)) {
            finishGlobalTracking(appid, false);
            return;
        }
        if (/installed!?/i.test(text)) {
            finishGlobalTracking(appid, true);
            return;
        }

        var allowed = session.allowedIndices || [];
        var foundIdx = findProviderIndexFromText(text);
        if (foundIdx !== -1 && allowed.indexOf(foundIdx) !== -1) {
            if (session.currentActiveIdx !== -1 && session.currentActiveIdx !== foundIdx) {
                session.providerStates[session.currentActiveIdx] = { status: 'fail', pct: 100 };
                session.targetPct = 5;
            }
            session.currentActiveIdx = foundIdx;
            session.targetPct = Math.max(session.targetPct, 35);
            if (!session.providerStates[foundIdx]) {
                session.providerStates[foundIdx] = { status: 'active', pct: 5 };
            } else if (session.providerStates[foundIdx].pct < 5) {
                session.providerStates[foundIdx].pct = 5;
            }
            session.providerStates[foundIdx].status = 'active';
        }

        for (var i = 0; i < SLS_PHASE_MAP.length; i++) {
            if (SLS_PHASE_MAP[i].match.test(text)) {
                if (SLS_PHASE_MAP[i].pct > session.targetPct) {
                    session.targetPct = SLS_PHASE_MAP[i].pct;
                }
                break;
            }
        }
        saveSessionToStorage(appid);
        syncModalUI(appid);
    }, 150);
}

function finishGlobalTracking(appid, success) {
    var session = globalDownloadSessions[appid];
    if (!session || session.finished) return;
    session.finished = true;
    if (session.crawlId) clearInterval(session.crawlId);
    if (session.pollId) clearInterval(session.pollId);

    var allowed = session.allowedIndices || [];
    if (success) {
        if (session.currentActiveIdx !== -1) {
            session.providerStates[session.currentActiveIdx] = {
                status: 'done',
                pct: 100
            };
        }
    } else {
        allowed.forEach(function(pIdx) {
            session.providerStates[pIdx] = {
                status: 'fail',
                pct: 100
            };
        });
        if (session.currentActiveIdx !== -1) {
            session.providerStates[session.currentActiveIdx] = {
                status: 'fail',
                pct: 100
            };
        }
    }
    saveSessionToStorage(appid);
    syncModalUI(appid);
}

function clearAppCache(appid, btnEl) {
    try {
        localStorage.removeItem('sls-session-' + appid);
    } catch (e) {}

    if (globalDownloadSessions[appid]) {
        var session = globalDownloadSessions[appid];
        if (session.crawlId) clearInterval(session.crawlId);
        if (session.pollId) clearInterval(session.pollId);
        session.requestedProviders = {};
        session.providerStates = {};
        session.allowedIndices = [];
        session.currentActiveIdx = -1;
        session.targetPct = 0;
        session.finished = false;
    }

    var modal = document.getElementById('sls-overlay-modal');
    if (modal) {
        modal.querySelectorAll('.sls-bar-fill').forEach(function(el) {
            el.remove();
        });
    }

    delete appUnlockStatus[appid];

    naviFetch('http://127.0.0.1:9001/clear-cache?id=' + appid).catch(function() {});

    if (btnEl) {
        var origText = btnEl.innerText;
        btnEl.innerText = 'App Cache Cleared!';
        setTimeout(function() {
            btnEl.innerText = origText;
        }, 1500);
    }
}

function clearAllCache(appid, btnEl) {
    try {
        var keysToRemove = [];
        for (var i = 0; i < localStorage.length; i++) {
            var k = localStorage.key(i);
            if (k && k.indexOf('sls-session-') === 0) {
                keysToRemove.push(k);
            }
        }
        keysToRemove.forEach(function(k) {
            localStorage.removeItem(k);
        });
    } catch (e) {}

    Object.keys(globalDownloadSessions).forEach(function(id) {
        var session = globalDownloadSessions[id];
        if (session) {
            if (session.crawlId) clearInterval(session.crawlId);
            if (session.pollId) clearInterval(session.pollId);
        }
    });
    globalDownloadSessions = {};

    var modal = document.getElementById('sls-overlay-modal');
    if (modal) {
        modal.querySelectorAll('.sls-bar-fill').forEach(function(el) {
            el.remove();
        });
    }

    appUnlockStatus = {};

    naviFetch('http://127.0.0.1:9001/clear-all-cache').catch(function() {});

    if (btnEl) {
        var origText = btnEl.innerText;
        btnEl.innerText = 'All Cache Cleared!';
        setTimeout(function() {
            btnEl.innerText = origText;
        }, 1500);
    }
}

/**
 * Called by the C++ side via CDP injection when a download completes or
 * fails while the store page is open (i.e. pushStatus calls come through
 * as innerText changes on the button span).  The loading bar already polls
 * those text changes via startRowLoadingBar — no extra wiring needed here.
 * This hook exists so any future direct-JS callers can still trigger bars.
 */
function slsTriggerLoadingBar(productID) {
    var btn  = document.querySelector('.sls-lua-btn[data-sls-appid="' + productID + '"]');
    var link = btn ? btn.querySelector('a') : null;
    if (btn && link) startRowLoadingBar(btn, link);
}
window.slsTriggerLoadingBar = slsTriggerLoadingBar;

