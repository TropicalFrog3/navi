/**
 * @file  main.js
 * @brief Initialization lifecycle, mutation observer setup, and debounced store page runner
 */
if (window.__slsStoreObserver && typeof window.__slsStoreObserver.disconnect === 'function') {
    window.__slsStoreObserver.disconnect();
}

var observer = null;
var debounceTimer = null;

function debouncedAddButtons() {
    if (debounceTimer) return;
    debounceTimer = requestAnimationFrame(function() {
        debounceTimer = null;
        addButtons();
        applyLuaLibraryOwnership();
    });
}

initLuaLibraryOwnership();
addButtons();
observer = new MutationObserver(debouncedAddButtons);
window.__slsStoreObserver = observer;
if (document.documentElement) observer.observe(document.documentElement, { childList: true, subtree: true });

