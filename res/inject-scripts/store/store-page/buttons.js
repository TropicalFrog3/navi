/**
 * @file  buttons.js
 * @brief Store page action button injection and controller setup for desktop and Big Picture modes
 */
function setupDownloadButton(luaLink, luaBtn, productID, cartBtn) {
    var span = luaLink.querySelector('span');
    if (span) span.innerText = 'Download Lua';
    luaLink.style.filter = 'hue-rotate(110deg) brightness(1.2)';
    luaLink.style.pointerEvents = '';
    luaLink.style.opacity = '';
    luaLink.onclick = function(e) {
        e.preventDefault();
        e.stopPropagation();
        luaBtn.dataset.slsAppid = productID;
        ping('Lua Click: ' + productID);
        openLuaProviderConfig(productID);
    };
}

function addButtons() {
    if (observer) observer.disconnect();

    // Big Picture Mode detection
    var bpSummary = document.getElementById('summaryBarTop');
    var match = window.location.href.match(/\/(app|sub)\/([0-9]+)/);
    
    if (bpSummary && match) {
        var productID = match[2];
        if (!document.getElementById('sls-bp-lua-btn-container-' + productID)) {
            var bpContainer = document.createElement('div');
            bpContainer.id = 'sls-bp-lua-btn-container-' + productID;
            bpContainer.style.cssText = 'display: flex; justify-content: center; z-index: 100; position: relative; margin: 12px 0px 12px 0px; width: 100%; box-sizing: border-box;';

            var bpBtn = document.createElement('button');
            bpBtn.id = 'sls-bp-lua-btn-' + productID;
            bpBtn.className = 'Focusable sls-lua-btn';
            bpBtn.dataset.slsProcessed = '1';
            bpBtn.dataset.slsAppid = productID;
            bpBtn.style.cssText = 'background: linear-gradient(135deg, #1a9fff 0%, #0073d4 100%); border: none; color: white; padding: 12px 30px; font-size: 18px; font-weight: bold; border-radius: 6px; cursor: pointer; box-shadow: 0 4px 15px rgba(0,0,0,0.4); text-transform: uppercase; transition: all 0.2s; width: 100%; display: flex; justify-content: center; box-sizing: border-box;';
            
            var bpLink = document.createElement('a');
            bpLink.style.display = 'block';
            bpLink.style.color = 'white';
            bpLink.style.textDecoration = 'none';

            var bpSpan = document.createElement('span');
            bpSpan.innerText = 'Checking Lua...';
            
            bpLink.appendChild(bpSpan);
            bpBtn.appendChild(bpLink);
            bpContainer.appendChild(bpBtn);
            
            bpSummary.parentNode.insertBefore(bpContainer, bpSummary);
            
            bpLink.onclick = function(e) {
                e.preventDefault();
                e.stopPropagation();
                ping('Lua Click (early): ' + productID);
                openLuaProviderConfig(productID);
            };

            // Check unlock status via callback server
            if (appUnlockStatus[productID] !== undefined) {
                var cached = appUnlockStatus[productID];
                if (cached && cached.exists) {
                    setupRemoveButton(bpLink, bpBtn, productID, null);
                } else {
                    setupDownloadButton(bpLink, bpBtn, productID, null);
                }
            } else {
                (function(ll, lb, pid) {
                    var controller = new AbortController();
                    var timeoutId = setTimeout(function() { controller.abort(); }, 5000);
                    naviFetch('http://127.0.0.1:9001/check?id=' + pid, { signal: controller.signal })
                        .then(function(r) { clearTimeout(timeoutId); return r.json(); })
                        .then(function(data) {
                            appUnlockStatus[pid] = data;
                            var isUnlocked = data.exists;
                            if (isUnlocked) {
                                setupRemoveButton(ll, lb, pid, null);
                            } else {
                                setupDownloadButton(ll, lb, pid, null);
                            }
                        })
                        .catch(function() {
                            clearTimeout(timeoutId);
                            ping('Check failed for ' + pid + ', defaulting to Download');
                            setupDownloadButton(ll, lb, pid, null);
                        });
                })(bpLink, bpBtn, productID);
            }

            // Register for gamepad navigation
            setTimeout(function() {
                registerSteamFocusNode(bpBtn);
                bpBtn.addEventListener('vgp_onfocus', function() {
                    bpBtn.style.outline = '4px solid #fff';
                    bpBtn.style.outlineOffset = '2px';
                    bpBtn.style.transform = 'scale(1.05)';
                });
                bpBtn.addEventListener('vgp_onblur', function() {
                    bpBtn.style.outline = 'none';
                    bpBtn.style.transform = 'scale(1)';
                });
                bpBtn.addEventListener('vgp_onok', function(e) {
                    e.preventDefault(); e.stopPropagation();
                    bpLink.click();
                });
            }, 100);
        }
    }

    var desktopMatch = window.location.href.match(/\/(app|sub)\/([0-9]+)/);
    var curProductId = desktopMatch ? desktopMatch[2] : null;
    var bpBtnContainer = curProductId ? document.getElementById('sls-bp-lua-btn-container-' + curProductId) : null;
    if (bpBtnContainer && bpBtnContainer.isConnected) {
        var rogueDesktopBtn = document.querySelector('.btn_addtocart.sls-lua-btn');
        if (rogueDesktopBtn) rogueDesktopBtn.remove();
    }

    if (!bpBtnContainer && desktopMatch && !document.querySelector('.sls-lua-btn[data-sls-appid="' + curProductId + '"]')) {
        var productID = curProductId;
        var cartBtn = null;
        var cartBtns = document.querySelectorAll('.btn_addtocart, .btn_add_to_cart');
        for (var i = 0; i < cartBtns.length; i++) {
            var c = cartBtns[i];
            var link = c.querySelector('a');
            if (!link) continue;
            var hrefLower = link.href.toLowerCase();
            if (hrefLower.indexOf('bundle') !== -1 || hrefLower.indexOf('dlc') !== -1) continue;
            cartBtn = c;
            break;
        }

        var luaBtn;
        if (cartBtn) {
            cartBtn.dataset.slsProcessed = '1';
        }
        luaBtn = document.createElement('div');
        luaBtn.className = 'btn_addtocart';
        luaBtn.innerHTML = '<a class="btn_green_steamui btn_medium" href="javascript:void(0)"><span>Checking Lua...</span></a>';

        luaBtn.classList.add('sls-lua-btn');
        luaBtn.dataset.slsProcessed = '1';
        luaBtn.dataset.slsAppid = productID;
        luaBtn.style.display = 'inline-block';
        luaBtn.style.marginRight = '4px';
        luaBtn.style.float = 'right';
        
        var luaLink = luaBtn.querySelector('a');
        if (luaLink) {
            luaLink.href = 'javascript:void(0)';
            luaLink.removeAttribute('id');

            // Avoid showing the download action until the installed state is known.
            var initialSpan = luaLink.querySelector('span');
            if (initialSpan) initialSpan.innerText = 'Checking Lua...';
            luaLink.style.filter = 'hue-rotate(200deg) brightness(1.0)';
            
            // Allow clicking even while checking
            luaLink.onclick = function(e) {
                e.preventDefault();
                e.stopPropagation();
                luaBtn.dataset.slsAppid = productID;
                ping('Lua Click (early): ' + productID);
                openLuaProviderConfig(productID);
            };

            // Check unlock status via callback server
            if (appUnlockStatus[productID] !== undefined) {
                var cached = appUnlockStatus[productID];
                if (cached && cached.exists) {
                    setupRemoveButton(luaLink, luaBtn, productID, cartBtn);
                } else {
                    setupDownloadButton(luaLink, luaBtn, productID, cartBtn);
                }
            } else {
                // Query the server with a short timeout
                (function(ll, lb, pid, cb) {
                    var controller = new AbortController();
                    var timeoutId = setTimeout(function() { controller.abort(); }, 5000);
                    naviFetch('http://127.0.0.1:9001/check?id=' + pid, { signal: controller.signal })
                        .then(function(r) { clearTimeout(timeoutId); return r.json(); })
                        .then(function(data) {
                            appUnlockStatus[pid] = data;
                            var isUnlocked = data.exists;
                            if (isUnlocked) {
                                setupRemoveButton(ll, lb, pid, cb);
                            } else {
                                setupDownloadButton(ll, lb, pid, cb);
                            }
                        })
                        .catch(function() {
                            clearTimeout(timeoutId);
                            ping('Check failed for ' + pid + ', defaulting to Download');
                            setupDownloadButton(ll, lb, pid, cb);
                        });
                })(luaLink, luaBtn, productID, cartBtn);
            }
        }

        var header = document.querySelector('div.apphub_HeaderStandardTop') || document.querySelector('.apphub_HeaderStandardTop');
        if (header && header.offsetParent !== null) {
            var appName = null;
            for (var childIndex = 0; childIndex < header.children.length; childIndex++) {
                if (header.children[childIndex].classList.contains('apphub_AppName')) {
                    appName = header.children[childIndex];
                    break;
                }
            }
            var insertionPoint = appName || header.firstChild;
            header.insertBefore(luaBtn, insertionPoint);
        } else if (cartBtn && cartBtn.parentNode) {
            cartBtn.parentNode.insertBefore(luaBtn, cartBtn);
        } else {
            var purchaseArea = document.getElementById('game_area_purchase') || document.querySelector('.game_area_purchase') || document.querySelector('.game_area_comingsoon') || document.querySelector('.game_bg') || document.querySelector('.rightcol');
            if (purchaseArea) {
                luaBtn.style.float = 'none';
                luaBtn.style.marginBottom = '15px';
                purchaseArea.prepend(luaBtn);
            }
        }

        // Register for gamepad navigation
        setTimeout(function() {
            registerSteamFocusNode(luaBtn);
            luaBtn.addEventListener('vgp_onfocus', function() {
                luaBtn.style.outline = '3px solid #fff';
                luaBtn.style.outlineOffset = '2px';
            });
            luaBtn.addEventListener('vgp_onblur', function() {
                luaBtn.style.outline = 'none';
            });
            luaBtn.addEventListener('vgp_onok', function(e) {
                e.preventDefault(); e.stopPropagation();
                var link = luaBtn.querySelector('a');
                if (link) link.click();
            });
        }, 100);
    }

    // Auto-resume download tracking if there is an active session for this product
    var pageMatch = window.location.href.match(/\/(app|sub)\/([0-9]+)/);
    if (pageMatch && pageMatch[2]) {
        var currentAppid = pageMatch[2];
        var saved = loadSessionFromStorage(currentAppid);
        if (saved && !saved.finished) {
            startGlobalDownloadTracking(currentAppid, null);
        }
    }

    if (observer && document.documentElement) observer.observe(document.documentElement, { childList: true, subtree: true });
}

