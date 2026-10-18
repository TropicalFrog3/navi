# DevTools Interaction Constraint

When working with Chrome DevTools or executing JavaScript in a browser console, do **NOT** use the `browser_subagent` tool. The subagent struggles with complex web interfaces like DevTools. 

Instead:
1. **Autonomous CDP Injection (Preferred)**:
   If a local Chromium/CEF remote debugging port is open (e.g., Steam on port 8080 via `http://127.0.0.1:8080/json`), do **NOT** ask the user to paste anything.
   Autonomously inspect targets and evaluate JavaScript using the `cef-cdp-injection` skill (Python script via `Runtime.evaluate`).
2. **Manual Fallback (Only if CDP is unreachable)**:
   Only if no debugging port is listening, write the JavaScript snippet cleanly, provide it directly in chat, and ask the user to paste it into their console.
3. **Multi-Target Verification Invariant**:
   * When a change introduces mode-specific logic (e.g. Desktop vs Big Picture / Steam Deck):
     * Do NOT declare the task complete after verifying only one target window.
     * Must inspect both `"Steam"` (Desktop) and `"Steam Big Picture Mode"` via CDP.
     * Query computed styles (`window.getComputedStyle`) on both targets to ensure styles evaluate correctly under both DOM trees.
     * When visual layout or alignment is altered, capture a verification screenshot via `Page.captureScreenshot` to confirm the rendered layout.
