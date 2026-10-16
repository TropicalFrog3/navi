# Implementation Plan: Hide Lua-Added Items in Steam Store When Browsing

## 1. Executive Summary & Objective

In **SLSsteam / ACCELA**, games unlocked via Lua scripts (`~/.local/share/Steam/config/stplug-in/<appid>.lua`) appear owned inside the client library (`SharedJSContext` MobX store `window.appStore.m_mapApps`) through native hooks on `CSteamApp` and `FillInAppOverview`.

However, Valve's Store servers (`store.steampowered.com`) are unaware of these client-side unlocks. When browsing the Steam Store (in Desktop mode or SteamOS Gamepad UI) with **"Hide items in my library"** enabled, legitimate Steam games are hidden from search and recommendation feeds, but **Lua-added games remain visible as unowned titles**.

### Goal
Implement a system that hides Lua-added games from the Steam Store (search results, category pages, top grossing, sale hubs, and recommendation queues) **if and only if** the user has turned on the **"Hide items in my library"** preference in Steam.

---

## 2. Reverse Engineering Findings (`steamui.so`)

Using the `/linux-elf-reversing` protocol on `~/.local/share/Steam/ubuntu12_32/steamui.so` (GOT Base: `0x2501204`):

```
+-----------------------------------------------------------------------------------+
|                                 steamui.so                                        |
|                                                                                   |
|  [Library Overview Controller]                  [CEF Unified Service Dispatcher]  |
|  CSteamUIAppController::FillInAppOverview       SharedConnection.SendMsg...       |
|  (0xaeade0, regparm(3))                         (String at 0xad13a8)              |
|         |                                                |                        |
|         v                                                v                        |
|  Populates CAppOverview                         Native handler (0x1757f00)        |
|  -> SharedJSContext                             -> CClientUnifiedServiceTransport |
|     window.appStore.m_mapApps (461 apps)           SendAndRecvMsg (0x17580f0)     |
+-----------------------------------------------------------------------------------+
                                                           |
                                                           v
                                            +-------------------------------+
                                            |   store.steampowered.com      |
                                            |   - GDynamicStore.s_rgOwned   |
                                            |   - StoreItemCache.m_mapApps  |
                                            |   - CsearchPreferences        |
                                            +-------------------------------+
```

### Key Technical Insights:
1. **Separation of Concerns**:
   - `steamui.so` manages the native UI and library representation (`CSteamUIAppController`).
   - Store browsing is executed as a web application loaded from `https://store.steampowered.com` inside Chromium Embedded Framework (CEF).
2. **IPC Dispatching**:
   - In `steamui.so`, `SharedConnection.SendMsgAndAwaitResponse` (string offset `-0x1a2fe5c` from GOT at `0x1754e6e`) routes RPCs from the webview to `CClientUnifiedServiceTransport::SendAndRecvMsg` at `0x17580f0`.
3. **Protobuf Descriptors in `steamui.so`**:
   - `CStoreBrowse_GetItems_Request` / `CStoreBrowse_GetItems_Response`
   - `StoreItem`: Field 4 (`visible`), Field 70 (`user_filter_failure: StoreBrowseFilterFailure`)
   - `StoreBrowseFilterFailure`: Field 5 (`already_owned`), Field 1 (`filter_failure`)
   - `CStore_GetStorePreferences_Request` / `CStore_GetStorePreferences_Response`
4. **Architectural Conclusion**:
   - Because store pages load over HTTPS and are rendered in CEF, filtering must be applied where the store DOM and data stores exist—specifically inside the Store webview context via [SLSsteam/res/inject-scripts/store-page-script.js](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/SLSsteam/res/inject-scripts/store-page-script.js), augmented by a backend endpoint in [SLSsteam/src/feats/storeinject.cpp](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/SLSsteam/src/feats/storeinject.cpp).

---

## 3. Steam Store Filtering Mechanics

### A. Classic Store Pages (`/search/`, `/genre/`, etc.)
1. **Ownership Registry**:
   - Queries `https://store.steampowered.com/dynamicstore/userdata/?id=<accountid>`.
   - Populates `GDynamicStore.s_rgOwnedApps = { [appId]: true }`.
   - `GDynamicStore.BIsAppOwned(appId)` checks this dictionary.
2. **Capsule Flagging**:
   - `GDynamicStore.DecorateDynamicItems()` scans `[data-ds-appid]`. If owned, it attaches `.ds_flagged.ds_owned` and appends `<div class="ds_flag ds_owned_flag">IN LIBRARY</div>`.
3. **Hide Filter Application**:
   - Search results container receives class `.hide_owned` when `CsearchPreferences.hide_owned === 1`.
   - CSS rule `#search_results.hide_owned .ds_owned` sets `display: none !important`.

### B. Modern Content Hubs & React Pages (`/sale/`, `/category/`, `/tags/`, `/adultonly`, etc.)
1. Items are fetched via `StoreBrowse.GetItems` or rendered dynamically via React Query (`V(R1, Y1)` querying `user_filter_failure`).
2. Unlike legacy search pages, React Content Hub items:
   - Do **not** have `.search_result_row` or `[data-ds-appid]`.
   - Render as `<a href=".../app/<appid>/...">` with Webpack CSS module classes (e.g., `_3Z1skIYVoqLbYiP2wy46Ce` for `ItemCapsule`).
   - If owned, Steam adds `_3Tsl0PufrncX7yyZgZZIGJ` (`ds_flagged`) and `aeeMsJQ4JhwpTeEyXMKFF` (`ds_collapse_flag`).
   - If the item was unlocked via Lua only, Valve's server is unaware of it, so React does not attach `ds_collapse_flag`.
   - Therefore, hiding requires targeting `a[href*="/app/<aid>/"]`, container `:has()` rules, and JavaScript DOM capsule resolution (`findCapsuleElement()`).

### C. Where the "Hide Items in Library" Preference Lives
* **Modern Content Hubs & Browsing Pages**: `<div id="application_config" data-search_preferences='{"hide_owned": true, ...}'>`
* **Account Preferences**: `<input id="hide_owned" class="search_preferences_checkbox" type="checkbox" name="hide_owned" value="1" checked>`
* **Search Context**: `var CsearchPreferences = { "hide_owned": 1, ... };`
* **Search Results Container**: `#search_results.hide_owned`
* **Search Filter Sidebar**: `<span data-param="hide" data-value="hide_owned" class="tab_filter_control_row checked">`
* **Top Grossing / Charts**: LocalStorage `topgrossing_prefs = { "bFilterLibraryItems": true }`
* **Session Cache**: `sessionStorage.getItem('sls_steam_hide_owned')` preserves preference across client SPA route transitions and redirects (such as `/adultonly`).

---

## 4. Implementation Steps

```mermaid
sequenceDiagram
    autonumber
    participant CEF as store.steampowered.com (CEF)
    participant Script as store-page-script.js
    participant Backend as SLSsteam (storeinject.cpp)
    participant Disk as stplug-in/*.lua

    Note over CEF,Backend: Page Loads store.steampowered.com
    Script->>Backend: GET /lua_apps
    Backend->>Disk: Scan stplug-in/ + g_config.isAddedAppId()
    Backend-->>Script: 200 OK: { "appids": [2914050, 1035120, ...] }
    Script->>CEF: GDynamicStore.s_rgOwnedApps[id] = true
    Script->>CEF: Evaluate isHideLibraryEnabled()
    alt Preference is ENABLED
        Script->>CEF: Inject scoped hiding stylesheet & flag StoreItemCache
        Note over CEF: Lua items collapsed from search, hubs, and grids
    else Preference is DISABLED
        Script->>CEF: Remove hiding stylesheet; leave "IN LIBRARY" badge
    end
```

### Step 1: Add `/lua_apps` Endpoint in SLSsteam Core
**Target File**: [SLSsteam/src/feats/storeinject.cpp](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/SLSsteam/src/feats/storeinject.cpp)

Add a route to the existing embedded HTTP server (which already handles `/check?id=`):
1. On receiving `GET /lua_apps`:
   - Scan `g_config.getPluginDir()` for all `<appid>.lua` files.
   - Include any additional IDs from `g_config.getAddedAppIds()`.
   - Respond with HTTP 200:
     ```json
     {
       "appids": [2914050, 1035120, 1062830, ...]
     }
     ```
   - Headers: `Access-Control-Allow-Origin: *`, `Content-Type: application/json`.

### Step 2: Implement Preference Resolver in Store Script
**Target File**: [SLSsteam/res/inject-scripts/store-page-script.js](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/SLSsteam/res/inject-scripts/store-page-script.js)

Add a multi-source preference detection function:
```javascript
function isHideLibraryEnabled() {
    // 1. Search page global inline object
    if (typeof CsearchPreferences !== 'undefined' && CsearchPreferences.hide_owned !== undefined) {
        return Boolean(CsearchPreferences.hide_owned);
    }
    // 2. Active filter sidebar checkbox in /search/
    var filterControl = document.querySelector('span[data-param="hide"][data-value="hide_owned"]');
    if (filterControl) {
        return filterControl.classList.contains('checked');
    }
    // 3. Category / Top-grossing preference in localStorage
    try {
        var topPrefs = JSON.parse(localStorage.getItem('topgrossing_prefs') || '{}');
        if (topPrefs.bFilterLibraryItems !== undefined) {
            return Boolean(topPrefs.bFilterLibraryItems);
        }
    } catch(e) {}
    // 4. Fallback: check GDynamicStore preferences
    if (window.GDynamicStore && GDynamicStore.s_preferences && GDynamicStore.s_preferences.hide_owned !== undefined) {
        return Boolean(GDynamicStore.s_preferences.hide_owned);
    }
    return false;
}
```

### Step 3: Populate Store Ownership & Apply Dynamic Filtering
**Target File**: [SLSsteam/res/inject-scripts/store-page-script.js](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/SLSsteam/res/inject-scripts/store-page-script.js)

1. **Ownership Sync**:
   * Request `http://127.0.0.1:<port>/lua_apps`.
   * When `window.GDynamicStore` is ready:
     ```javascript
     luaAppIds.forEach(function(appId) {
         GDynamicStore.s_rgOwnedApps[appId] = true;
     });
     if (typeof GDynamicStore.DecorateDynamicItems === 'function') {
         GDynamicStore.DecorateDynamicItems();
     }
     ```
2. **Conditional Hiding Application**:
   * Create or update `<style id="sls-hide-lua-library">`:
     ```javascript
     function updateLuaHiding() {
         var hide = isHideLibraryEnabled();
         var styleEl = document.getElementById('sls-hide-lua-library');
         if (!hide) {
             if (styleEl) styleEl.remove();
             return;
         }
         if (!styleEl) {
             styleEl = document.createElement('style');
             styleEl.id = 'sls-hide-lua-library';
             document.head.appendChild(styleEl);
         }
         
         // Generate exact CSS selectors for all Lua AppIDs
         var selectors = luaAppIds.map(function(id) {
             return [
                 '.search_result_row[data-ds-appid="' + id + '"]',
                 '[data-ds-appid="' + id + '"].ds_owned',
                 '.hide_owned [data-ds-appid="' + id + '"]',
                 '#search_results.hide_owned [data-ds-appid="' + id + '"]',
                 'div[data-panel*="\\"appid\\":' + id + '"]'
             ].join(', ');
         }).join(',\n');
         
         styleEl.textContent = selectors + ' { display: none !important; }';
         
         // Update React StoreItemCache if present
         if (window.StoreItemCache && StoreItemCache.m_mapApps) {
             luaAppIds.forEach(function(id) {
                 var app = StoreItemCache.m_mapApps.get(Number(id));
                 if (app) {
                     if (!app.m_userFilterFailure) app.m_userFilterFailure = {};
                     app.m_userFilterFailure.already_owned = true;
                     app.m_bVisible = false;
                 }
             });
         }
     }
     ```
3. **Capsule Wrapper Collapsing & Grid Reflow**:
   * Previously, hiding only the inner `<a>` left its parent grid cell `<div>` (e.g. `ItemCapsule`, `SaleCapsuleWrapper`, `GridCell`) with fixed aspect-ratio / min-height in the CSS grid, leaving a blank empty space.
   * `collapseSingleGameContainers(link, aid)` ascends through all exclusive ancestors up to the outermost grid item and applies `display: none !important; width: 0 !important; height: 0 !important; margin: 0 !important; padding: 0 !important; border: none !important; position: absolute !important;`.
   * This completely removes the cell from the CSS Grid formatting context, prompting the CSS Grid engine to immediately shift the next game into the slot without blank gaps.
4. **Pre-render Data Filtering on `#application_config`**:
   * Parses and strips Lua AppIDs from `data-ch_main_list_data`, `data-section_*`, and `data-browser_contenthub_*`.
   * When React initializes, mounts, or switches category tabs, the hidden AppIDs are absent from the dataset and the next AppID is rendered directly.
5. **Event Listeners & Observers**:
   * Attach `click` / `change` handlers to the search sidebar control:
     `span[data-param="hide"][data-value="hide_owned"]`
   * Register a `MutationObserver` on `#search_results` / `.responsive_page_frame` to re-apply hiding whenever new rows are loaded via infinite scroll.

---

## 5. Verification & Test Plan

| Test Case | Steps | Expected Outcome |
| :--- | :--- | :--- |
| **Search with Hide ON** | 1. Ensure "Hide items in my library" is checked.<br/>2. Search for a known Lua game (e.g. `Jugs Bay`, AppID 2914050). | Game is omitted from search results. |
| **Search with Hide OFF** | 1. Uncheck "Hide items in my library" in the search filter.<br/>2. Re-search or wait for live filter update. | Game appears in search results with the "IN LIBRARY" badge. |
| **Content Hubs & Sale Grids** | 1. Browse `/adultonly/`, `/tags/en/Loot`, or `/category/hentai` with library filtering enabled.<br/>2. Inspect items. | Lua games are filtered from capsules and recommendation belts; no blank spaces remain; next games shift seamlessly into place. |
| **Non-Lua Games Integrity** | 1. Check unowned games.<br/>2. Check legitimately owned games. | Unowned games remain visible; legitimate owned games follow normal Steam behavior. |
| **Dynamically Added Lua** | 1. Download and import a new Lua via the in-game store button.<br/>2. Return to search. | The new AppID is immediately recognized in `/lua_apps` and hidden if the setting is ON. |

---

## 6. Files Modified

1. [SLSsteam/src/feats/storeinject.cpp](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/SLSsteam/src/feats/storeinject.cpp):
   - Added `/lua_apps` handler to the embedded HTTP server.
2. [SLSsteam/res/inject-scripts/store-page-script.js](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/SLSsteam/res/inject-scripts/store-page-script.js):
   - Added `isHideLibraryEnabled()`, `/lua_apps` fetching, `GDynamicStore.s_rgOwnedApps` enrichment, `collapseSingleGameContainers()` multi-ancestor collapsing, `filterApplicationConfigData()`, and scoped CSS `:has()` rules.
3. [prod-release/SLSsteam/res/inject-scripts/store-page-script.js](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/prod-release/SLSsteam/res/inject-scripts/store-page-script.js):
   - Synchronized script changes to prod-release.

---

## 7. Bug Fix: Entire Store Page Removal on Browsing Pages

### Root Cause
In `applyLuaLibraryHiding()`, generated CSS selectors included:
```javascript
'div[data-panel]:has(a[href*="/app/' + aid + '/"])'
```
Steam's top-level layout wrapper element for the entire store page body is:
```html
<div role="main" class="responsive_page_template_content" id="responsive_page_template_content" data-panel='{"autoFocus":true,"navKey":"responsive_page_template_content"}'>
```
Because `#responsive_page_template_content` contains all page elements and matches `div[data-panel]`, any Lua app link anywhere on the page caused this selector to set `#responsive_page_template_content` to `display: none !important;`, hiding the entire store page.

### Resolution
1. **Removed broad `:has()` CSS selectors**: Eliminated `div[data-panel]:has(...)`, `div:has(...)`, `[class*="item_"]:has(...)`, and generic capsule selectors from the dynamic stylesheet. Retained only specific item-level selectors (`.search_result_row`, `.tab_item`, `.carousel_item`, `.item_wrapper`, `[class*="ItemCapsule"]`, `[class*="SaleItem"]`).
2. **Safeguarded `#responsive_page_template_content`**:
   - Added explicit protection in `isMultiItemContainer()` to prevent ascending to or collapsing `#responsive_page_template_content`, `#page_content`, `[role="main"]`, `main`, and page section containers.
   - Added a reset check at the top of `applyLuaLibraryHiding()` to clear any accidental `display: none` or `.sls-lua-library-hidden` on `#responsive_page_template_content`.
3. **Synchronized & Verified**:
   - Synchronized script across source, `prod-release`, and `~/.local/share/SLSsteam/res/inject-scripts/store-page-script.js`.
   - Verified on live target `3E5CFFED9995383A4CFDF8CB4ED961D4` (`https://store.steampowered.com/adultonly`): computed root display is `flow-root`, height `4526px`, Lua games hidden, and non-Lua games rendered properly without page loss.

---

## 8. Bug Fix: Blank Spaces in Carousels Replaced by Subsequent Games

### Root Cause
In Steam's modern store content hubs (`/adultonly`, sale events, category carousels):
1. Games in horizontal carousels (`.carousel__slider-tray`) are partitioned into discrete `.carousel__slide` elements.
2. Inside each slide, rows (`.SaleSectionContainer`) use fixed CSS grid templates (e.g. `grid-template-columns: repeat(3, 1fr)` based on `ItemCount_3`).
3. When game `3155980` in Slide 1 Row 2 was hidden (`display: none`), Row 2 only had 2 visible items left. Because the next game (`2808930`) resided in Slide 2, CSS grid could not pull it across slide boundaries, leaving column 3 completely blank and empty.

### Resolution
1. **Dynamic Cross-Slide Carousel Reflow (`reflowCarouselTrays`)**:
   - Targets `.carousel__slider-tray` elements.
   - Gathers all visible cards and hidden cards across all rows/slides in the tray.
   - For each row `r`, reads its grid capacity (`ItemCount_N`).
   - Slices the required count from `allVisible` and shifts subsequent cards forward to fill each row to capacity (e.g. shifting `2808930` from Slide 2 into Slide 1 Row 2 Column 3).
   - Only mutates the DOM if current row visible cards differ from the target slice, avoiding unnecessary DOM churn and mutation observer loops.
   - Quarantines all hidden items together at the end of the last row (`height: 0px`).
   - Automatically hides empty slides with `display: none !important;`.
2. **Reversible Restoration**:
   - Stores original parent and nextSibling on cards marked with `data-sls-moved`.
   - If the user turns off "Hide items in my library", all moved cards restore to their original positions and rows.
3. **Dynamic Script Ingestion in `cdpinject.cpp`**:
   - Removed `static std::string cachedStorePageScript;` so that newly compiled or edited scripts are read fresh whenever store pages are injected.
4. **Live Verification on Target `5A7A20E49ADD7B295C66CAF7DECB81C8`**:
   - Slide 1 Row 0 (`ItemCount_2`): 2 cards (`Isekai Waifu Overlord`, `Pussy Island`), 0 empty space.
   - Slide 1 Row 1 (`ItemCount_3`): 3 cards (`LoveCraft`, `Being a DIK`, `Lust Goddess`), 0 empty space.
   - Slide 2 Row 0 (`ItemCount_2`): 2 cards (`The Assistant`, `Much Bigger Than my Boyfriend`), 0 empty space.
   - Slide 2 Row 1 (`ItemCount_3`): 3 cards (`Horizon Walker`, `Dragn Wash`, `Rise of Eros`), 0 empty space.
   - Slide 3 Row 0 (`ItemCount_2`): 2 cards (`Everlusting Life`, `Undercover with Nora`).
   - Slides 4 and 5: Hidden (`display: none`).
   - Carousel navigation (`Next`, `Prev`) functions seamlessly.

