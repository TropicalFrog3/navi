---
name: fast-browser-agent
description: Execute ultra-fast, cheap web browser automation workflows pairing Jev (TypeSafe System One decision classifier) with the Antigravity IDE reasoning engine.
---

# Fast-Browser Agent Skill (Jev + Antigravity IDE)

## Purpose
Run high-speed browser automation workflows combining Jev (sub-20ms micro-decisions against structured DOM element catalogs) with Antigravity IDE (using your active Google Account via the native `agy` CLI bridge, no Gemini API key required) for planning, dynamic typing, error recovery, and independent verification of completion.

## Architecture & Workflow
1. **DOM to Numbers**: The active browser DOM is extracted into a numbered catalog of visible interactive controls (`[1] link 'Pricing'`, `[2] button 'Start Trial'`) tagged with `data-jev-id="N"`. Screenshots and pixels are stripped away.
2. **Jev Batch Classifier**: Evaluates `action_type`, `target_element`, and `is_cookie_banner` in a single batched call with calibrated confidence scores.
3. **Deterministic Execution Rules**:
   - Both confidences $\ge 0.7$ -> Pure-code execution via Playwright locator (`page.locator('[data-jev-id=N]').click()`).
   - Action is `type` -> Static text extracted directly from goal, or Antigravity dynamically composes the string.
   - Low confidence ($<0.7$) or `blocked` -> Antigravity recovery planner reasons over DOM and history to supply an updated plan.
   - Cookie banners & consent dialogs -> Deterministically dismissed automatically.
4. **Independent Verification**: When Jev returns `done`, Antigravity inspects the execution trail and final DOM state to rule out false positives and premature termination.
5. **Memory Constraint**: Sliding window strictly capped at the last 5 actions to prevent hallucination loops.

## Execution

Run via the runner script in `.agents/tools/jev-browser-agent/`:

```bash
cd .agents/tools/jev-browser-agent

# Execute a live browser task
./run.sh --url "https://news.ycombinator.com" --goal "Find the search box, search for 'playwright', and view the first result"

# Run in deterministic offline simulation mode (no API keys required)
./run.sh --mock --headless --url "https://news.ycombinator.com" --goal "Find search box and type 'playwright'"
```

Run test suite:
```bash
cd .agents/tools/jev-browser-agent
source .venv/bin/activate
pytest tests/ -v
```

