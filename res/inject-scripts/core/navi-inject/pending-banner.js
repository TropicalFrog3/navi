/**
 * @file  pending-banner.js
 * @brief Viewport warning banner controller for pending cloud save restart operations
 */
function updatePendingBanner(hasPending) {
    var existingBanner = document.getElementById('navi-cloud-pending-banner');
    if (!hasPending) {
        if (existingBanner) existingBanner.remove();
        return;
    }
    if (existingBanner) return;

    if (!document.body) {
        setTimeout(function() { updatePendingBanner(hasPending); }, 250);
        return;
    }

    var container = document.createElement('div');
    container.innerHTML = PENDING_BANNER_HTML;
    var banner = container.firstElementChild;

    var restartBtn = banner.querySelector('#navi-banner-restart-btn');
    if (restartBtn) {
        restartBtn.addEventListener('mouseenter', function() { restartBtn.style.background = '#fbbf24'; });
        restartBtn.addEventListener('mouseleave', function() { restartBtn.style.background = '#f59e0b'; });
        restartBtn.onclick = function(e) {
            e.preventDefault();
            e.stopPropagation();
            restartBtn.innerText = 'Restarting...';
            restartBtn.disabled = true;
            restartBtn.style.opacity = '0.7';
            restartBtn.style.cursor = 'default';
            secureFetch('/restart', { method: 'POST', mode: 'no-cors' }).catch(function(err) {
                console.warn('[NaviCloudManager] Restart error:', err);
            });
        };
    }

    document.body.appendChild(banner);
}

