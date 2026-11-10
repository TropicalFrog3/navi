/**
 * @file  pending-banner.js
 * @brief Persistent restart warning banner for pending cloud save operations
 */
/**
 * Render or update the persistent warning banner at the top of the viewport
 * when pending operations exist.
 */
function updatePendingBanner(hasPending) {
    var existingBanner = document.getElementById('navi-cloud-pending-banner');
    if (!hasPending) {
        if (existingBanner) {
            existingBanner.remove();
        }
        return;
    }

    if (existingBanner) {
        return; // Banner is already visible
    }

    if (!document.body) {
        setTimeout(function() { updatePendingBanner(hasPending); }, 250);
        return;
    }

    var banner = document.createElement('div');
    banner.id = 'navi-cloud-pending-banner';
    banner.style.cssText = 'position: fixed; top: 0; left: 0; right: 0; z-index: 999998; ' +
        'background: linear-gradient(90deg, #78350f, #92400e); border-bottom: 2px solid #f59e0b; ' +
        'color: #fef3c7; padding: 10px 18px; display: flex; align-items: center; justify-content: space-between; ' +
        'font-family: "Motiva Sans", -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; ' +
        'box-shadow: 0 4px 14px rgba(0,0,0,0.6); box-sizing: border-box;';

    var textContainer = document.createElement('div');
    textContainer.style.cssText = 'display: flex; align-items: center; gap: 10px; font-size: 14px; font-weight: 500;';

    var icon = document.createElement('span');
    icon.style.cssText = 'font-size: 18px; color: #fbbf24; flex-shrink: 0;';
    icon.innerText = '⚠️';

    var text = document.createElement('span');
    text.innerText = 'Pending cloud save operation waiting for Steam restart. Please restart Steam via Navi to safely apply changes.';

    textContainer.appendChild(icon);
    textContainer.appendChild(text);

    var restartBtn = document.createElement('button');
    restartBtn.id = 'navi-banner-restart-btn';
    restartBtn.className = 'Focusable';
    restartBtn.setAttribute('tabindex', '-1');
    restartBtn.setAttribute('role', 'button');
    restartBtn.style.cssText = 'background: #f59e0b; color: #1e1e1e; border: none; border-radius: 3px; ' +
        'padding: 6px 16px; font-size: 13px; font-weight: 700; font-family: "Motiva Sans", sans-serif; ' +
        'cursor: pointer; white-space: nowrap; transition: background 0.15s ease; flex-shrink: 0; margin-left: 12px;';
    restartBtn.innerText = 'Restart Steam Now';

    restartBtn.addEventListener('mouseenter', function() { restartBtn.style.background = '#fbbf24'; });
    restartBtn.addEventListener('mouseleave', function() { restartBtn.style.background = '#f59e0b'; });
    restartBtn.onclick = function(e) {
        e.preventDefault();
        e.stopPropagation();
        restartBtn.innerText = 'Restarting...';
        restartBtn.disabled = true;
        restartBtn.style.opacity = '0.7';
        restartBtn.style.cursor = 'default';
        window.NaviCloudManager.restartSteam().catch(function(err) {
            console.warn('[NaviCloudManager] Restart error:', err);
        });
    };

    banner.appendChild(textContainer);
    banner.appendChild(restartBtn);
    document.body.appendChild(banner);
}

