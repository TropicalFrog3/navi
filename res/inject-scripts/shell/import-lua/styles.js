/**
 * @file  styles.js
 * @brief Injects custom CSS rules for Navi top-bar tabs, config shell, and manual install modal
 */
function ensureCustomTabStyles() {
    if (document.getElementById('sls-custom-tab-styles')) return;
    var styleEl = document.createElement('style');
    styleEl.id = 'sls-custom-tab-styles';
    var css = SLS_SHELL_STYLES
        .replace(/__STEAM_TAB_ACTIVE_CLASS__/g, STEAM_TAB_ACTIVE_CLASS)
        .replace(/__STEAM_TAB_BASE_CLASS__/g, STEAM_TAB_BASE_CLASS)
        .replace(/__STEAM_TAB_TEXT_CLASS__/g, STEAM_TAB_TEXT_CLASS);
    styleEl.innerHTML = css;
    document.head.appendChild(styleEl);
}
