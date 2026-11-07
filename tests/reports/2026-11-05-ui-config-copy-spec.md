# 2026-11-05 — UI gate run: Navi Configuration copy & state spec (v1)

Scope: `plans/04-done/navi-config-copy-spec-v1.md` (created as
`plans/02-specs/navi-config-copy-spec-v1.md`, archived after verification).
Change surface: `res/inject-scripts/shell/import-lua-script.js` (config page only)
plus four assertion lines in `tests/integration/ui/test_uj05_config_editor_validation.py`.

## Gates run

| Gate | Command | Result |
|---|---|---|
| Syntax | `node --check res/inject-scripts/shell/import-lua-script.js` | PASS |
| Asset identity | `cmp res/inject-scripts/shell/import-lua-script.js src/view/assets/shell/import-lua-script.js` | PASS (identical — `src/view/assets` is a symlink to `res/inject-scripts`) |
| Build | `./install.sh build` | PASS (`Navi build completed successfully`) |
| Unit | `make test` | PASS — 98/98 tests, 22 suites, run twice (26.2 s, 29.7 s) |
| Gate 0.7 | `make test-gate07` | PASS — 7/7 checkpoints, byte-for-byte diverted-save round trip |
| UI journeys | `python3 -m pytest -q tests/integration/ui/` | PASS — 42 passed in 61.6 s |

Targeted re-run before the full suite:
`pytest -q tests/integration/ui/test_uj05_config_editor_validation.py
tests/integration/ui/test_uj07_live_search_debounce.py
tests/integration/ui/test_uj09_companion_mockup_switcher.py` → 9 passed in 18.6 s.

Raw DOM snapshots / console logs for the UI runs are under the gitignored
`test-results/` tree written by `tests/integration/ui/harness/browser_driver.py`.

## Contract changes asserted by tests (not weakened)

`tests/integration/ui/test_uj05_config_editor_validation.py`:

1. `wait_for_status_banner("saved and reloaded")` → `wait_for_status_banner("saved")`
   (three occurrences) — the success banner is now
   `Saved. Your new settings are active.`
2. `assert config_page.is_save_disabled() is False` → `is True` after a completed
   save — Save is now disabled while the form is clean.

Untouched: `body_data["CloudSaves"]["BackupRetentionCount"] == 0`, i.e. retention
validation stays advisory because the server clamps to 1–100.

## Notes

- Pre-existing flake `test_uj04_gamepad_navigation_osk_dismissal_and_disabled_skip`
  (`assert 'BODY' == 'INPUT'`) did **not** reproduce in this run.
- Reset payload deliberately omits `ManagedApps` and `PrefixSync` (merge-only POST to
  `/config/full`) and writes `AdditionalApps: []` rather than the template's leftover
  `3934270`, so a reset cannot inject an unowned game.
- Manual device checks (375/768/1280 reflow, controller focus ring, dirty-leave guard)
  were **not** run in this pass; the script-level work is verified by the gates above.
