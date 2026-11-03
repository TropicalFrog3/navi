# CONTEXT.md — `tests/` Verification & Gates Workspace

## Workspace Role & Workflow

This room owns the staged verification pipeline — run it ascending, do not skip stages:
(1) **unit** (Googletest, `tests/unit/*.cpp` → `make test`); (2) **integration**
(Bubblewrap Gate 0.7 hermetic harness, `tests/integration/*` → `make test-gate07`);
(3) **UI flows** (`tests/integration/ui/` journeys → `make test-ui-p0` → `test-ui` →
`test-ui-a11y`); (4) **device live** (real Steam Deck only, `pytest --live` +
`scripts/dev/deck-ctl.sh`). Failures are documented, never silently triaged: run reports
go to `tests/reports/YYYY-MM-DD-<gate>-<summary>.md`. Bug fixes always start here with
a RED test (`.agents/rules/test-regression.md`) before any `src/` change.

| Task / Stage | Read These Files | Skip These Files | Skills / MCPs / Commands Needed |
|---|---|---|---|
| Run verification gate | `Makefile` targets, `pytest.ini` (testpaths = `tests/integration`; markers `live`, `ui_p0`, `ui_p1`, `a11y`, `responsive`), `tests/conftest.py` (`run_installer`) | `src/` except the code under test, `pkg/`, `include/` | `./install.sh build` first, then `make test` → `make test-gate07` → `make test-ui-p0` |
| Add unit test | `.agents/rules/test-regression.md`, `tests/unit/test_stubs*.cpp` (stub conventions), owning `src` file | `tests/integration/`, `tools/`, `bin/` | `make test` (Googletest; coverage variant `make coverage`) |
| Add integration / UI journey | `tests/conftest.py`, `tests/integration/ui/harness/browser_driver.py` (writes gitignored `test-results/ui/<test_name>/`), closest existing `test_ujNN_*.py` as template | `artifacts/test-runs/` (historical), `test-results/` (generated), `test-artifacts/` (removed) | `pytest -q tests/integration -m ui_p0`; `--live` only with a device attached |
| Device / live testing | `scripts/dev/deck-ctl.sh`, `pytest.ini` `live` marker | `bin/`, `obj/`, `docs/` | Skills `steamdeck-wireless-workflow` + `cef-cdp-injection`; `deck-ctl.sh` is orchestrator-only |
| Record / publish run report | latest raw logs from `test-results/`, the gate's journey file | `src/**` beyond the failing area | Naming `YYYY-MM-DD-<gate>-<summary>.md` into `tests/reports/` (see its README); produce summaries only after gates pass or are fixed |
