/**
 * @file  appid.js
 * @brief AppId resolution and extraction from current library DOM state
 */
function extractAppId(manageBtn) {
    var appid = null;
    var curr = manageBtn;
    while (curr && curr !== document.body) {
        var cls = (typeof curr.className === 'string') ? curr.className : '';
        var m = cls.match(/\bapp_([0-9]+)\b/);
        if (m) { appid = m[1]; break; }
        var da = curr.getAttribute('data-appid');
        if (da) { appid = da; break; }
        curr = curr.parentElement;
    }

    if (!appid) {
        try {
            var currFiber = manageBtn;
            while (currFiber && currFiber !== document.body && !appid) {
                for (var key in currFiber) {
                    if (key.startsWith('__reactInternalInstance$') || key.startsWith('__reactFiber$')) {
                        var fiber = currFiber[key];
                        while (fiber) {
                            if (fiber.memoizedProps) {
                                if (fiber.memoizedProps.appid) { appid = String(fiber.memoizedProps.appid); break; }
                                if (fiber.memoizedProps.appID) { appid = String(fiber.memoizedProps.appID); break; }
                                if (fiber.memoizedProps.unAppID) { appid = String(fiber.memoizedProps.unAppID); break; }
                                if (fiber.memoizedProps.overview && fiber.memoizedProps.overview.appid) { appid = String(fiber.memoizedProps.overview.appid); break; }
                                if (fiber.memoizedProps.app && fiber.memoizedProps.app.appid) { appid = String(fiber.memoizedProps.app.appid); break; }
                                if (fiber.memoizedProps.details && fiber.memoizedProps.details.appid) { appid = String(fiber.memoizedProps.details.appid); break; }
                            }
                            fiber = fiber.return;
                        }
                    }
                    if (appid) break;
                }
                currFiber = currFiber.parentElement;
            }
        } catch(e) {}
    }

    if (!appid) {
        var match = window.location.href.match(/\/app\/([0-9]+)/);
        if (match) appid = match[1];
        if (!appid) {
            match = window.location.hash.match(/\/app\/([0-9]+)/);
            if (match) appid = match[1];
        }
    }

    if (!appid) {
        var elWithStyle = document.querySelector('[style*="/assets/"]');
        if (elWithStyle) {
            var m = elWithStyle.style.backgroundImage.match(/\/assets\/([0-9]+)\//);
            if (m) appid = m[1];
        }
    }
    if (!appid) {
        var links = document.querySelectorAll('a[href*="/app/"]');
        for (var i = 0; i < links.length; i++) {
            var m = links[i].href.match(/\/app\/([0-9]+)/);
            if (m) { appid = m[1]; break; }
        }
    }
    return appid;
}

