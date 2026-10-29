#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace CDPInject
{
    /**
     * Represents a CEF/Chromium debug page from the /json endpoint.
     */
    struct CDPPage
    {
        std::string url;
        std::string title;
        std::string webSocketDebuggerUrl;
        std::string type;
    };

    /**
     * Fetch the list of debuggable pages from CEF's /json endpoint.
     * Returns an empty vector on failure.
     */
    std::vector<CDPPage> fetchPages(const char* host = "127.0.0.1", int port = 8080);

    /**
     * Connect to host:port with a timeout. Supports both IPv4 and IPv6.
     * Returns socket fd or -1.
     */
    int tcpConnect(const char* host, int port, int timeoutSec = 2);

    /**
     * Inject JavaScript into a page via its CDP WebSocket debugger URL.
     * Uses Runtime.evaluate over a minimal WebSocket client (RFC 6455).
     *
     * Returns true if the JS was sent successfully, false on any error.
     */
    bool injectJS(const std::string& wsUrl, const std::string& jsCode);

    /**
     * Check if a script has already been injected by evaluating an expression (e.g. "!!window.__slsLuaBtnAdded").
     * Returns true if the expression evaluates to true, false otherwise.
     */
    bool isScriptInjected(const std::string& wsUrl, const std::string& checkExpression);

    /**
     * Evaluate an expression in a page and return its string result ("" on failure
     * or empty result). The result must only contain [A-Za-z0-9%._~!*'()-] (e.g. the
     * output of encodeURIComponent) because it is extracted without JSON unescaping.
     */
    std::string evalString(const std::string& wsUrl, const std::string& expression);

    /**
     * Run the full injection pass: fetch pages, filter for Steam store
     * pages, and inject the "Download Lua" button script into each.
     *
     * This is the direct C++ replacement for inject_cef.py.
     */
    void injectStorePages();

    /**
     * Injects a depot selection UI for the given appId into any active Steam Client library or store page.
     */
    void injectDepotSelectionUI(uint32_t appId);

    /**
     * Use the Steam browser to download a URL by injecting fetch() into an existing page.
     * Bypasses Cloudflare and similar protections because it runs in a real browser context.
     * Writes the response body as raw bytes to destPath.
     *
     * Returns the HTTP status code on success, or -1 on failure.
     */
    int downloadViaPage(const std::string& url, const std::string& destPath);

    /**
     * Delete an uninstalled/removed app from CEF's SharedJSContext appStore.m_mapApps.
     */
    void notifyAppRemoved(uint32_t appId);

    /**
     * Ensure Steam on Linux assigns a compatibility tool (Proton) to an app so it installs without "Invalid platform".
     */
    void ensureCompatTool(uint32_t appId);

    /**
     * Injects and triggers the Navi Cross-Device Sync overlay modal on active Steam CEF contexts.
     */
    void injectSyncOverlayModal();
}

