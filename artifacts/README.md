# `artifacts/` — Committed Evidence, Write-Generated Only

## `test-runs/`

Historical UI-test artifacts (DOM snapshots, failure screenshots, console/network dumps)
committed as regression evidence, moved here from `test-artifacts/` in November 2026.

Rules:

- **Read-only evidence**: never edit or delete; preserve for future reproductions.
- **New runs do not go here** — the UI harness writes current runs to the gitignored
  `test-results/` tree (hardcoded path in `tests/integration/ui/harness/browser_driver.py`).
- Human-readable summaries go to `tests/reports/YYYY-MM-DD-<gate>-<summary>.md`,
  referencing this directory's evidence when reproducing old failures.
