/**
 * @file  styles.js
 * @brief Dynamic stylesheet injection for store page elements and modals
 */
function ensureStoreStyles() {
    if (document.getElementById('sls-bar-style')) return;
    var s = document.createElement('style');
    s.id = 'sls-bar-style';
    s.textContent = SLS_STORE_STYLES;
    document.head.appendChild(s);
}

// Alias for backward-compatibility with internal call sites
var injectLoadingBarStyles = ensureStoreStyles;

