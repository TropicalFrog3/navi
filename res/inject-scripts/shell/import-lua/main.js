/**
 * @file  main.js
 * @brief Main entry point, double-injection guard, window exports, and initialization
 */

// Expose helpers globally on window
window.__naviRenderConfigPage = renderConfigPage;
window.__naviShowConfigPage = showConfigPage;
window.__naviHideConfigPage = hideConfigPage;
window.__naviShowImportLuaPage = showSlsPage;
window.__naviHideImportLuaPage = hideSlsPage;
window.__slsCloudFileViewer = CloudFileViewerModule;
window.__slsCompanionMockups = CloudFileViewerModule;

// Initialize top bar tabs and observers
initSlsTabAndPage();
