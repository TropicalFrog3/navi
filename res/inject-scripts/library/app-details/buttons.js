/**
 * @file  buttons.js
 * @brief DOM button injectors for Navi and CloudRedirect library actions
 */
function makeSteamButton(appid, className, text, extraAttrs) {
    var isDesktop = !!((document.body && document.body.classList.contains('DesktopUI')) || document.querySelector('.DesktopUI'));
    var isDeckOrBigPicture = !isDesktop;
    var buttonMargin = isDeckOrBigPicture ? 'margin-left: 10px;' : 'margin-right: 10px;';
    var buttonHeight = isDeckOrBigPicture ? '48px' : '32px';
    var buttonPadding = isDeckOrBigPicture ? '8px 16px' : '0 16px';
    var buttonFontSize = isDeckOrBigPicture ? '16px' : '14px';

    var steamBtnStyle = 'display: flex; align-items: center; justify-content: center; ' +
        'background: rgba(172, 178, 201, 0.14); color: rgb(220, 222, 223); ' +
        'border: none; border-radius: 2px; padding: ' + buttonPadding + '; ' + buttonMargin + ' ' +
        'font-size: ' + buttonFontSize + '; font-weight: 400; font-family: "Motiva Sans", sans-serif; ' +
        'height: ' + buttonHeight + '; box-sizing: border-box; cursor: pointer; ' +
        'text-decoration: none; transition: background 0.1s ease;';

    var btn = document.createElement('div');
    btn.className = className + ' Focusable';
    btn.setAttribute('tabindex', '-1');
    btn.setAttribute('role', 'button');
    btn.dataset.slsAppid = appid;
    btn.dataset.slsAppId = appid;
    btn.style.cssText = steamBtnStyle;
    if (extraAttrs) {
        for (var k in extraAttrs) btn.setAttribute(k, extraAttrs[k]);
    }

    var label = document.createElement('span');
    label.style.cssText = 'white-space: nowrap;';
    label.innerText = text;
    btn.appendChild(label);

    // Gamepad focus styling (match Steam's focus ring)
    btn.addEventListener('vgp_onfocus', function() {
        btn.style.background = 'rgba(172, 178, 201, 0.30)';
        btn.style.outline = '2px solid white';
        btn.style.outlineOffset = '2px';
    });
    btn.addEventListener('vgp_onblur', function() {
        btn.style.background = 'rgba(172, 178, 201, 0.14)';
        btn.style.outline = 'none';
    });

    // Hover styling
    btn.addEventListener('mouseenter', function() {
        btn.style.background = 'rgba(172, 178, 201, 0.25)';
    });
    btn.addEventListener('mouseleave', function() {
        btn.style.background = 'rgba(172, 178, 201, 0.14)';
    });

    return btn;
}

function createRemoveButton(manageContainer, appid, isPending, onlineFixInstalled, autoCrackInstalled, provider) {
    var parentNode = manageContainer.parentNode;

    // Remove any existing SLS buttons from this container before re-inserting
    parentNode.querySelectorAll(
        '.sls-remove-lua-btn, .sls-config-btn, .sls-provider-label, .sls-change-provider-btn, .sls-navi-btn, .sls-cloud-toggle-btn, .sls-cloud-backup-btn, .sls-cloud-restore-btn, .sls-cloud-reset-btn'
    ).forEach(function(el) { el.remove(); });

    // Detect Steam Deck / Big Picture Mode (GamepadUI) vs Desktop Mode
    var isDesktop = !!((document.body && document.body.classList.contains('DesktopUI')) || document.querySelector('.DesktopUI'));
    var isDeckOrBigPicture = !isDesktop;

    // Use a single compact "Navi" menu button on both Desktop and Big Picture Mode
    // to prevent overflowing the action row and covering other elements.
    var naviBtn = makeSteamButton(appid, 'sls-navi-btn sls-lua-btn', isPending ? 'Navi (Restart)' : 'Navi');
    naviBtn.dataset.slsProvider = provider || '';
    if (isPending) {
        naviBtn.dataset.slsPending = 'true';
        naviBtn.style.color = '#fde047';
    }
    if (isDeckOrBigPicture) {
        naviBtn.style.marginLeft = '10px';
        naviBtn.style.marginRight = '0px';
    } else {
        naviBtn.style.marginLeft = '0px';
        naviBtn.style.marginRight = '10px';
    }

    function setRestartState() {
        isPending = true;
        naviBtn.dataset.slsPending = 'true';
        var span = naviBtn.querySelector('span');
        if (span) span.innerText = 'Navi (Restart)';
        naviBtn.style.color = '#fde047';
    }

    naviBtn.addEventListener('vgp_onok', function(e) {
        e.preventDefault(); e.stopPropagation();
        naviBtn.click();
    });

    naviBtn.onclick = function(e) {
        e.preventDefault();
        e.stopPropagation();
        var currentProv = naviBtn.dataset.slsProvider || provider;
        var currentPending = naviBtn.dataset.slsPending === 'true' || isPending;
        openNaviModal(appid, currentPending, onlineFixInstalled, autoCrackInstalled, currentProv, function() {
            setRestartState();
        });
    };

    parentNode.dataset.slsInjected = appid;
    parentNode.insertBefore(naviBtn, parentNode.firstChild);

    setTimeout(function() {
        registerSteamFocusNode(naviBtn);
    }, 10);
}
