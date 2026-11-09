var luaProviders = [
    { name: 'Ryuu' },
    { name: 'DepotBox' },
    { name: 'HubcapDB' },
    { name: 'Spinoza' }
];

function openChangeProviderModal(appid, currentProvider) {
    if (document.getElementById('sls-provider-modal')) return;
    ensureStyles();
    var overlay = document.createElement('div');
    overlay.id = 'sls-provider-modal';
    overlay.setAttribute('role', 'dialog');
    overlay.setAttribute('aria-modal', 'true');
    overlay.setAttribute('data-testid', 'sls-provider-modal');
    overlay.style.cssText = 'position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.8);z-index:999999;display:flex;justify-content:center;align-items:center;backdrop-filter:blur(8px);transition:all 0.3s ease;opacity:0;';

    var rows = '';
    for (var i = 0; i < luaProviders.length; i++) {
        var isCurrent = currentProvider && currentProvider.toLowerCase().indexOf(luaProviders[i].name.toLowerCase()) !== -1;
        var badgeHtml = isCurrent ? '<span style="margin-left:8px;font-size:12px;padding:2px 6px;background:rgba(172,178,201,0.2);border-radius:2px;color:#dcdedf;">Current</span>' : '';
        var btnStyle = isCurrent
            ? 'background:rgba(172,178,201,0.05);color:#969696;cursor:default;opacity:0.6;'
            : '';
        rows += '<div style="display:flex;align-items:center;justify-content:space-between;padding:12px 14px;margin-bottom:6px;background:rgba(0,0,0,0.2);border:1px solid rgba(172,178,201,0.1);border-radius:2px;">' +
            '<span style="flex:1;color:#dcdedf;font-size:16px;font-weight:400;font-family:\'Motiva Sans\', sans-serif;">' + luaProviders[i].name + badgeHtml + '</span>' +
            '<button data-sls-provider-idx="' + i + '" class="sls-btn-action Focusable" tabindex="-1" role="button" ' + (isCurrent ? 'disabled ' : '') + 'style="height: 36px; padding: 0 16px; ' + btnStyle + '">' + (isCurrent ? 'Active' : 'Use') + '</button>' +
        '</div>';
    }

    var cardHtml = PROVIDER_MODAL_HTML
        .replace(/\{\{APPID\}\}/g, appid)
        .replace('{{ROWS}}', rows);
    overlay.innerHTML = cardHtml;
    document.body.appendChild(overlay);

    requestAnimationFrame(function() {
        overlay.style.opacity = '1';
        var card = document.getElementById('sls-provider-card');
        if (card) { card.style.transform = 'scale(1)'; card.style.opacity = '1'; }
    });

    function closeModal() {
        overlay.style.opacity = '0';
        var card = document.getElementById('sls-provider-card');
        if (card) { card.style.transform = 'scale(0.95)'; card.style.opacity = '0'; }
        setTimeout(function() { overlay.remove(); }, 300);
    }

    document.getElementById('sls-prov-close').onclick = closeModal;
    document.getElementById('sls-prov-cancel').onclick = closeModal;

    var provBtns = overlay.querySelectorAll('[data-sls-provider-idx]');
    
    var modalButtons = [document.getElementById('sls-prov-close')];
    provBtns.forEach(function(b) { modalButtons.push(b); });
    modalButtons.push(document.getElementById('sls-prov-cancel'));
    trapSteamGamepadModal(overlay, modalButtons);
    provBtns.forEach(function(btn) {
        btn.onclick = function(e) {
            e.preventDefault(); e.stopPropagation();
            var idx = btn.getAttribute('data-sls-provider-idx');
            var statusEl = document.getElementById('sls-prov-status');
            statusEl.style.display = 'block';
            statusEl.innerText = 'Switching to ' + luaProviders[idx].name + '...';

            // Disable all buttons
            provBtns.forEach(function(b) { b.disabled = true; b.style.opacity = '0.5'; b.style.cursor = 'default'; });

            fetch('http://127.0.0.1:9001/change-provider?id=' + appid + '&provider=' + idx)
                .then(function(r) { return r.json(); })
                .then(function(data) {
                    if (data.success) {
                        statusEl.innerText = 'Download started from ' + luaProviders[idx].name + '. Please wait...';
                        statusEl.style.background = 'rgba(16,185,129,0.1)';
                        statusEl.style.borderColor = 'rgba(16,185,129,0.3)';
                        statusEl.style.color = '#6ee7b7';
                        
                        var pollCount = 0;
                        var lastStatus = '';
                        var timer = setInterval(function() {
                            var btn = document.querySelector('.sls-lua-btn[data-sls-appid="' + appid + '"]');
                            if (!btn) return;
                            var currentStatus = btn.dataset.slsStatus;
                            if (currentStatus && currentStatus !== lastStatus) {
                                lastStatus = currentStatus;
                                statusEl.innerText = currentStatus;
                                
                                if (/installed!?/i.test(currentStatus)) {
                                    clearInterval(timer);
                                    statusEl.style.color = '#34d399';
                                    var pl = document.querySelector('.sls-provider-label');
                                    if (pl) pl.innerHTML = 'Lua Provider: <span style="color:#a5b4fc;font-weight:600;">' + luaProviders[idx].name + '</span>';
                                    var nb = document.querySelector('.sls-navi-btn[data-sls-appid="' + appid + '"]');
                                    if (nb) nb.dataset.slsProvider = luaProviders[idx].name;
                                    var np = document.getElementById('sls-navi-modal-provider');
                                    if (np) np.innerText = luaProviders[idx].name;
                                    provBtns.forEach(function(b) { b.disabled = false; b.style.opacity = '1'; b.style.cursor = 'pointer'; });
                                }
                                else if (/not available|failed|error|invalid/i.test(currentStatus)) {
                                    clearInterval(timer);
                                    statusEl.style.color = '#fca5a5';
                                    statusEl.style.background = 'rgba(239,68,68,0.1)';
                                    statusEl.style.borderColor = 'rgba(239,68,68,0.3)';
                                    provBtns.forEach(function(b) { b.disabled = false; b.style.opacity = '1'; b.style.cursor = 'pointer'; });
                                }
                            }
                            pollCount++;
                            if (pollCount > 600) clearInterval(timer);
                        }, 500);

                    } else {
                        statusEl.innerText = 'Failed to start download.';
                        statusEl.style.color = '#fca5a5';
                    }
                })
                .catch(function() {
                    statusEl.innerText = 'Connection error.';
                    statusEl.style.color = '#fca5a5';
                });
        };
    });
}

