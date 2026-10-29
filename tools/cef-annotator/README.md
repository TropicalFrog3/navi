# Steam CEF Annotator

A developer annotation tool for the live Steam client UI. Connects directly to Steam's remote debugging port (`8080`) over Chrome DevTools Protocol (CDP), injects an in-client overlay, captures rich element context and element screenshots, and dual-stores annotations locally and in the Vibe Annotations server for consumption by coding agents.

---

## Architecture & Design

```
+-------------------------------------------------------------------------+
|                              Steam Client                               |
|                                                                         |
|  +-----------------------------+       +-----------------------------+  |
|  |     Desktop Shell (CEF)     |       |      Store Page (CEF)       |  |
|  |       (MAIN WORLD)          |       |      (ISOLATED WORLD)       |  |
|  |  * Traverses __reactFiber$* |       |  * Page.createIsolatedWorld |  |
|  |  * Extracts component name  |       |  * Runtime.addBinding       |  |
|  |  * Closed Shadow DOM Host   |       |    (executionContextName)   |  |
|  |  * Dynamic Pin Badges       |       |  * Zero window pollution    |  |
|  +--------------+--------------+       +--------------+--------------+  |
+-----------------|-------------------------------------|-----------------+
                  | CDP WebSocket                       | CDP WebSocket
                  +------------------+------------------+
                                     |
                                     v
                  +-------------------------------------+
                  |   annotator.py (Python 3 Daemon)   |
                  |  * Target discovery (127.0.0.1/::1) |
                  |  * Session-scoped script injection  |
                  |  * Hardened payload validator       |
                  |  * Page.captureScreenshot (clip)    |
                  +------------------+------------------+
                                     |
                         +-----------+-----------+
                         |                       |
                         v                       v
            +-------------------------+ +-------------------------+
            |  Local JSON File Store  | |  Vibe Annotations Server|
            |    (annotations.json)   | |  http://127.0.0.1:3846  |
            |     Atomic tmp+replace  | |  (read_annotations MCP) |
            +-------------------------+ +-------------------------+
```

### Two World Execution Modes
1. **Desktop Shell (`main` world)**:
   * Target: `about:blank?createflags=18...` (`title: "Steam"`).
   * Runs in the main execution context so `overlay.js` can inspect `__reactFiber$*` on DOM elements and resolve React component names (e.g. `AppDetailsSection`, `PlayBarButton`).
2. **Remote Store Pages (`isolated` world)**:
   * Target: `https://store.steampowered.com/...`
   * Runs in an isolated world created via `Page.createIsolatedWorld` (`worldName: "SteamAnnotator"`).
   * Binding is registered with `executionContextName: "SteamAnnotator"`.
   * **Security Guarantee**: `typeof window[bindingName]` is `undefined` in the main world. Untrusted store scripts cannot detect, call, or tamper with the annotator.

---

## Quick Start

### 1. Start Steam with Remote Debugging
Ensure Steam is launched with CEF remote debugging enabled:
```bash
steam -cef-enable-debugging
```
Port `8080` opens on loopback.

### 2. Start the Annotator (One Command)
Run the unified launcher (from any terminal):
```bash
steam-annotator
```
*(Or `./tools/cef-annotator/run.sh` from the repository root).*

This single command:
1. Checks for Steam's debugging port `8080`.
2. Automatically starts the Vibe Annotations Server (`http://127.0.0.1:3846`) in the background if it is not already running.
3. Launches the daemon and automatically attaches to Steam windows.

---

## Command Shortcuts

| Command | Description |
| :--- | :--- |
| `steam-annotator` | Start Vibe server & annotator daemon (auto-attaches). |
| `steam-annotator toggle` | Remotely toggle in-client annotate mode. |
| `steam-annotator status` | Check status of Steam, Vibe server, and annotations. |
| `steam-annotator list` | List all saved annotations. |
| `steam-annotator clear` | Clear all saved annotations from local store and Vibe server. |
| `steam-annotator stop` | Stop any running annotator daemon processes. |

---

## In-Client Overlay Usage

* **Toggle Mode**:
  * Press **`Alt+Shift+A`** inside Steam, OR
  * Click the **"Annotate"** pill button in the bottom-right corner, OR
  * Run `python3 tools/cef-annotator/annotator.py toggle` from the terminal.
* **Movable Floating Toolbar**:
  * In standby: Drag anywhere on screen to reposition. Click to activate.
  * In active mode: Displays `[Logo] | Click to capture • [ ↑ ] [ ↓ ] [ ↵ ] to fine-tune target • [ Del ] to delete all • [ Esc ] to stop | ✕`.
  * **Fine-tuning shortcuts**:
    * `↑` (ArrowUp): Walk up to parent element in the DOM tree.
    * `↓` (ArrowDown): Walk down to first child element.
    * `↵` (Enter): Capture currently highlighted target and open the inspector dialog.
    * `Del` (or `Shift+Backspace`): Instantly delete all annotations from view, local store, and Vibe server.
    * `Esc` / `✕`: Exit capture mode.
* **Movable & Resizable Inspector Dialog**:
  * **Header Drag**: Drag from header bar or top drag pill to move dialog anywhere.
  * **Interactive Resize**: Drag bottom-right corner grip or edges to resize (min: 320x340px).
  * **3 Tabs**:
    1. **Comment**: Describe changes with file attachments (`📎`), screenshots (`📷`), and `Ctrl+Enter` to save.
    2. **Design**: Interactive controls for Layout (Flex/Grid alignment matrix, gap, wrap, reverse), Spacing (Padding/Margin linked/per-side), Appearance (Border/Background colors and widths), Sizing, and live CSS editors (inline overrides + custom CSS rules).
    3. **Variants**: Design variant exploration prompt instructions.
* **Numbered Pins**: Existing annotations on the current page are marked with circular number badges. Hovering displays the comment tooltip. Pins automatically reposition on DOM mutations, route changes, and window resizing.

---

## CLI Reference

| Command | Description |
| :--- | :--- |
| `annotator.py run [--match <re>]` | Start the target discovery and injection daemon. |
| `annotator.py toggle` | Remotely toggle annotate mode on all active targets. |
| `annotator.py list` | List all saved annotations with metadata, selectors, and comments. |
| `annotator.py done <id>` | Mark an annotation resolved; deletes from local JSON and Vibe server. |
| `annotator.py clear` | Clear all annotations from local storage. |
| `annotator.py status` | Probe port 8080, active targets, Vibe server health, and store size. |

---

## Dual Storage & Integration

1. **Local JSON Store (`tools/cef-annotator/annotations.json`)**:
   * Updated atomically via temporary file and `os.replace`.
   * Gitignored so local notes don't pollute repo diffs.
2. **Vibe Annotations Server (`http://127.0.0.1:3846`)**:
   * Synchronized via `POST /api/annotations` and `POST /api/annotations/:id/attachments` (kind=`capture`).
   * Readable by AI agents using the Antigravity `vibe-annotations/read_annotations` MCP tool.
   * If the Vibe server is offline, the daemon logs a warning and continues writing to `annotations.json`.

---

## Security Model

* **Isolated World Boundary**: Binding functions are strictly inaccessible from untrusted store contexts.
* **Payload Size Limit**: Hard ceiling of 64 KB per payload; oversized payloads are rejected immediately.
* **Rate Limiting**: Clamped to a maximum of 5 saves per second per target to prevent flooding.
* **Execution Context Verification**: The daemon checks that incoming `Runtime.bindingCalled` events originate from the specific context ID created for that target.
* **Untrusted Data Policy**: All text, outerHTML, titles, and class names collected from store targets are treated as untrusted strings.

