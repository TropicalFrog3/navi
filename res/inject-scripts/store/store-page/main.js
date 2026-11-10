/**
 * @file  main.js
 * @brief Initialization lifecycle, mutation observer setup, and debounced store page runner
 */
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
if (document.documentElement) observer.observe(document.documentElement, { childList: true, subtree: true });

