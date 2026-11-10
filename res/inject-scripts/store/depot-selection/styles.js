/**
 * @file  styles.js
 * @brief Injects stylesheet for depot selection dialog into overlay container
 */
function ensureDepotStyles(overlay) {
    var style = document.createElement('style');
    style.id = 'sls-depot-styles';
    style.textContent = SLS_DEPOT_STYLES;
    overlay.appendChild(style);
}
