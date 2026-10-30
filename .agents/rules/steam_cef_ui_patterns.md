# Steam CEF Injected UI Architecture

## 1. Dual-Display Pattern for Long-Running Tasks
For background operations (like downloads, verify, or installs) injected into Steam:
* **Inline Card**: Embedded directly as `firstChild` inside the game details action row (`div[aria-label="Manage"]` or `div[aria-label="Configure Controller"]`). Height should match Steam action buttons (48px) with `box-sizing: border-box`.
* **Global Floating Indicator**: Fixed at bottom-right (`position: fixed; bottom: 28px; right: 28px; z-index: 999990`) with glassmorphism (`backdrop-filter: blur(12px)`).
* **Mutual Exclusion**: When the inline card is currently mounted and visible on screen, hide the floating indicator to avoid visual duplication. Display the floating indicator whenever the user navigates elsewhere in Steam.

## 2. Inter-Script Coordination
* When one injected script triggers an action affecting another (e.g. Depot Selection Modal triggering a download):
  * Dispatch a custom event: `window.dispatchEvent(new CustomEvent('navi-download-started', { detail: { appid } }))`
  * Post a window message: `window.postMessage({ type: 'navi-download-started', appid }, '*')`
  * Forward to `window.top` if in an iframe or nested view.
  * Receiver immediately invalidates cached "idle" state and scans without waiting for poll intervals.

## 3. Desktop vs Steam Deck / Big Picture Mode Detection & Spacing
Steam CEF operates in two strictly mutually exclusive UI environments:
* **Desktop Mode**: Has `.DesktopUI` present on `document.body` or within the root hierarchy.
* **Steam Deck / Big Picture Mode (GamepadUI)**: Never contains `.DesktopUI`. Containers run under `GamepadMode`, window title contains `"Steam Big Picture Mode"`, and `window.name` contains `"BPM"`.

### Canonical Detection Pattern:
```javascript
var isDesktop = !!((document.body && document.body.classList.contains('DesktopUI')) || document.querySelector('.DesktopUI'));
var isDeckOrBigPicture = !isDesktop;
var buttonMargin = isDeckOrBigPicture ? 'margin-left: 10px;' : 'margin-right: 10px;';
```

### Action Button Spacing Invariants:
* **Desktop Mode**:
  * Must use `margin-right: 10px;` and `margin-left: 0px;`.
  * Preserves 10px spacing flowing left-to-right toward the native `Manage` button.
* **Steam Deck / Big Picture Mode**:
  * Must use `margin-left: 10px;` and `margin-right: 0px;`.
  * Matches Steam's native Big Picture action button spacing (`margin: 0px 0px 0px 10px`).

## 4. Cloud Manager UI & Mutating Operations Guardrails

### REST API Security & Origin Verification
- All Cloud Manager REST endpoints (`/cloud/status`, `/cloud/login`, `/cloud/app/redirect`, `/cloud/app/reset`, `/cloud/pending-ops`) require authentication and origin validation:
  - **Constant-Time Token Verification**: Mutating and status requests must include the `X-Navi-Auth` header. Verify against the running session token using constant-time comparison (`CRYPTO_memcmp`) to eliminate timing side-channels.
  - **Strict Origin Allowlist**: Validate incoming requests against an explicit origin allowlist:
    - `https://steamloopback.host` (Steam CEF internal web context)
    - `http://localhost:9001`
    - `http://127.0.0.1:9001`
  - On origin mismatch or missing/invalid token, immediately reject with `HTTP 403 Forbidden`.

### Safe DOM Construction (Anti-XSS)
- Never use direct string concatenation or template literals of untrusted/dynamic data (such as backup archive paths, file names, provider account IDs, or app titles) into `element.innerHTML`.
- Build DOM structures using `document.createElement()`, assign text through `element.textContent`, and configure attributes using `element.setAttribute()`.
- Prevents DOM-based Cross-Site Scripting (XSS) vulnerabilities inside Steam CEF contexts.

### Modal Recursion Safety
- Avoid mutual or self-delegation between modal opening/closing logic.
- Never delegate outer modal handler functions into nested callbacks or inner functions that re-invoke the outer modal wrapper. Uncontrolled re-entrancy triggers `RangeError: Maximum call stack size exceeded` and crashes the Steam CEF JavaScript context.
- Maintain a single source of truth for modal display states via explicit state flags or singleton controllers.

### Destructive Action Safety
- **Typed Confirmation for Resets**:
  - Save data wipes or resets (`POST /cloud/app/reset`) must require explicit user typing of `"RESET"` in a dedicated input field before enabling the action button and dispatching the POST request.
  - Requests missing `{"confirm": "RESET"}` must be rejected with `HTTP 400 Bad Request`.
- **Pending Operations Banner**:
  - Cloud Manager status polling (every 5–10 seconds) must inspect the pending operations queue (`/cloud/pending-ops`).
  - When pending operations are queued, display a persistent, non-dismissible restart warning banner indicating that Steam must be restarted for idle helper operations to take effect.
