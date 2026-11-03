# `tests/reports/` — Human-Readable Gate Run Reports

One dated markdown report per verification gate run, named
**`YYYY-MM-DD-<gate>-<summary>.md`** (e.g. `2026-11-03-gate07-cloudredirect-live-trip.md`).

Rules:

- Write a report only after a gate run (pass or fix-fail) — never for dry runs.
- Raw machine output (dom snapshots, console logs, screenshots) stays in the
  gitignored `test-results/` tree written by
  `tests/integration/ui/harness/browser_driver.py`; reference the path, don't inline it.
- Committed historical evidence lives in `../../artifacts/test-runs/` (read-only).
- Never place these reports inside `tests/unit/` or `tests/integration/`.
