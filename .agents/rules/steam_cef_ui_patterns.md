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
