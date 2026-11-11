# DevTools Interaction Constraint

When working with Chrome DevTools or executing JavaScript in a browser console, do **NOT** use the `browser_subagent` tool. The subagent struggles with complex web interfaces like DevTools. 

Instead:
1. **Autonomous CDP Injection (Preferred)**:
   If a local Chromium/CEF remote debugging port is open (e.g., Steam on port 8080 via `http://127.0.0.1:8080/json`), do **NOT** ask the user to paste anything.
   Autonomously inspect targets and evaluate JavaScript using the `cef-cdp-injection` skill (Python script via `Runtime.evaluate`).
2. **Manual Fallback (Only if CDP is unreachable)**:
   Only if no debugging port is listening, write the JavaScript snippet cleanly, provide it directly in chat, and ask the user to paste it into their console.
3. **Multi-Target Verification Invariant (Live Steam CDP Only)**:
   * When a change introduces mode-specific logic (e.g. Desktop vs Big Picture / Steam Deck):
     * Inspect both `"Steam"` (Desktop) and `"Steam Big Picture Mode"` via CDP ONLY if Steam is actively running with the remote debugging port open (`http://127.0.0.1:8080/json`).
     * Query computed styles (`window.getComputedStyle`) on both targets to ensure styles evaluate correctly under both DOM trees.
     * When visual layout or alignment is altered ON LIVE STEAM, capture a verification screenshot via `Page.captureScreenshot` to confirm the rendered layout.
     * **PROHIBITED**: Do NOT capture screenshots for static template editing (`res/inject-scripts/**`). Static CEF fragments rely on the Steam Client DOM runtime and will render blank or broken in standalone browsers.
4. **Subshell Process & Preview Guardrails**:
   * **NEVER** spin up ad-hoc web servers (e.g. `python3 -m http.server`, `node http-server`) or mock HTML wrappers in `/tmp` to "preview" CEF templates.
   * **NEVER** execute unescaped `pkill` commands (e.g. `pkill -f "http.server 8765"`) in tool subshells: the regex matches the wrapper process arguments and immediately terminates the agent's own execution subshell via `SIGTERM`. If process termination is required, use precise PIDs or escaped patterns (`pkill -f "http\\.server 8765"`).
