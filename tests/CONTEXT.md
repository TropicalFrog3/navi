# CONTEXT.md — `tests/` Verification & Gates Workspace

## Workspace Role & Workflow

This room owns the staged verification pipeline — execute test suites **only when the user
explicitly asks**, always via `./install.sh` subcommands in ascending order:
(1) **unit** (Googletest, `tests/unit/*.cpp` → `./install.sh test`); (2) **integration**
(Bubblewrap Gate 0.7 hermetic harness, `tests/integration/*` → `./install.sh test-gate07`);
(3) **UI flows** (`tests/integration/ui/` journeys → `./install.sh test-ui` →
`./install.sh test-flow`); (4) **device live** (real Steam Deck only,
`scripts/dev/deck-ctl.sh`). Failures are documented, never silently triaged: run reports
go to `tests/reports/YYYY-MM-DD-<gate>-<summary>.md`. Bug fixes include a regression
test (`.agents/rules/test-regression.md`) alongside any `src/` change.

| Task / Stage | Read These Files | Skip These Files | Skills / MCPs / Commands Needed |
|---|---|---|---|
| Run verification gate | `Makefile` targets, `pytest.ini` (testpaths = `tests/integration`; markers `live`, `ui_p0`, `ui_p1`, `a11y`, `responsive`), `tests/conftest.py` (`run_installer`) | `src/` except the code under test, `pkg/`, `include/` | Only when user asks: `./install.sh build` first, then `./install.sh test` → `./install.sh test-gate07` → `./install.sh test-ui` → `./install.sh test-flow` |
| Add unit test | `.agents/rules/test-regression.md`, `tests/unit/test_stubs*.cpp` (stub conventions), owning `src` file | `tests/integration/`, `tools/`, `bin/` | `./install.sh build`; when user asks: `./install.sh test` (coverage variant `./install.sh coverage`) |
| Add integration / UI journey | `tests/conftest.py`, `tests/integration/ui/harness/browser_driver.py` (writes gitignored `test-results/ui/<test_name>/`), closest existing `test_ujNN_*.py` as template | `artifacts/test-runs/` (historical), `test-results/` (generated), `test-artifacts/` (removed) | When user asks: `./install.sh test-gate07` / `./install.sh test-ui` / `./install.sh test-flow` |
| Device / live testing | `scripts/dev/deck-ctl.sh`, `pytest.ini` `live` marker | `bin/`, `obj/`, `docs/` | Skills `steamdeck-wireless-workflow` + `cef-cdp-injection`; `deck-ctl.sh` is reserved for the main agent (never a subagent task) |
| Record / publish run report | latest raw logs from `test-results/`, the gate's journey file | `src/**` beyond the failing area | Naming `YYYY-MM-DD-<gate>-<summary>.md` into `tests/reports/` (see its README); produce summaries only after gates pass or are fixed |

## File Manifest

<!-- MANIFEST:BEGIN -->
| File | Purpose |
|---|---|
| `conftest.py` | Shared pytest fixtures and test environment setup configuration |
| `fixtures/legacy_migration_fixtures.py` | Test fixtures providing legacy v0.9 config and cache files |
| `inspect_store_ui.py` | Headless browser script inspecting Steam store DOM elements |
| `integration/gate07_harness.cpp` | Gate 0.7 Bubblewrap integration test harness launcher |
| `integration/mock_cloud_relay_server.py` | Mock HTTP server simulating remote cloud storage endpoints |
| `integration/test_bundle_sync.py` | Tests verifying bundle-app-details.py, bundle-cloud-manager.py, bundle-navi-inject.py, bundle-import-lua.py, bundle-depot-selection.py, and bundle-store-page.py stay in sync with sources |
| `integration/test_cloudredirect_integration.py` | Integration tests for CloudRedirect save diversion and runbook |
| `integration/test_installer.py` | Integration tests for install.sh script logic and directory layout |
| `integration/test_migration.py` | Integration tests verifying config and cache schema migrations |
| `integration/test_steam_ui_flow.py` | End-to-end integration test of Steam UI CEF script injection flow |
| `integration/test_store_script.py` | Integration test for store page script DOM manipulation |
| `integration/ui/conftest.py` | Pytest configuration and browser driver fixtures for UI tests |
| `integration/ui/harness/browser_driver.py` | Playwright browser automation driver for Steam CEF UI tests |
| `integration/ui/harness/callback_server_stub.py` | Stub server capturing CEF JavaScript callback events |
| `integration/ui/harness/navi_queue_bridge.py` | Bridge facilitating communication with Navi operation queue |
| `integration/ui/pages/base_modal_page.py` | Page object base class for CEF modal dialog interactions |
| `integration/ui/pages/cloud_settings_page.py` | Page object representing the CloudRedirect settings view |
| `integration/ui/pages/import_config_page.py` | Page object representing the config import modal view |
| `integration/ui/pages/library_details_page.py` | Page object representing the Steam library game details view |
| `integration/ui/pages/store_app_page.py` | Page object representing Steam store application pages |
| `integration/ui/test_phase1_harness_smoke.py` | Smoke test verifying browser harness initialization and teardown |
| `integration/ui/test_phase4_diagnostics_and_sla.py` | UI tests verifying diagnostic modal rendering and latency SLAs |
| `integration/ui/test_uj01_cloud_reset_input.py` | User journey test verifying cloud save reset modal confirmation |
| `integration/ui/test_uj02_cloud_provider_oauth.py` | User journey test for cloud provider OAuth login and feedback |
| `integration/ui/test_uj03_manual_upload_dragdrop.py` | User journey test for drag-and-drop manual save upload |
| `integration/ui/test_uj04_store_bridge_depots.py` | User journey test verifying depot selection bridge in store page |
| `integration/ui/test_uj05_config_editor_validation.py` | User journey test validating config editor input fields and save |
| `integration/ui/test_uj06_library_app_actions.py` | User journey test verifying library action buttons trigger events |
| `integration/ui/test_uj07_live_search_debounce.py` | User journey test verifying live search input debounce timing |
| `integration/ui/test_uj08_a11y_gamepad_responsive.py` | Accessibility and gamepad navigation responsiveness test |
| `integration/ui/test_uj09_companion_mockup_switcher.py` | UI journey test verifying companion mockup switcher functionality |
| `integration/ui/test_uj10_navi_menu_drilldown.py` | User journey test for the Navi Menu drill-down sections |
| `test_project_map.py` | Verifies workspace floor plan and CONTEXT.md routing tables |
| `unit/test_apps.cpp` | Unit tests for Apps lifecycle, Family Share lock behavior, and CM packet handling |
| `unit/test_cloud_manager.cpp` | Unit tests for CCloudManager CLI execution and state parsing |
| `unit/test_cloud_oauth.cpp` | Unit tests for CloudRedirect OAuth loopback server and token exchange |
| `unit/test_cloud_pending_ops.cpp` | Unit tests for CPendingOpsQueue pending operation serialization |
| `unit/test_cloudredirect_bridge.cpp` | Unit tests for CloudRedirect native API endpoints and security |
| `unit/test_config.cpp` | Unit tests for YAML configuration parsing and value validation |
| `unit/test_depotkeys.cpp` | Unit tests for depot key decryption and storage lookup |
| `unit/test_manifest_and_native.cpp` | Unit tests for manifest code storage and native game install |
| `unit/test_migration.cpp` | Unit tests for declarative config and cache migration bridges |
| `unit/test_patterns_and_utils.cpp` | Unit tests for pattern matching, string utilities, and hashing |
| `unit/test_prefix_bucket_sync.cpp` | Unit tests for Proton prefix discovery and bucket sync routines |
| `unit/test_stubs.cpp` | Unit test mock stubs for Steam SDK and system calls |
| `unit/test_stubs_appinfo.cpp` | Unit test mock stubs for Steam app info queries |
| `unit/test_stubs_player.cpp` | Unit test mock stubs for Steam user and friend interfaces |
| `unit/test_stubs_unified.cpp` | Unit test mock stubs for unified service message transport |
| `unit/test_vdf_and_lua.cpp` | Unit tests for Valve VDF format parser and Lua manifest parser |
| `unit/test_zip_and_atomic.cpp` | Unit tests for atomic file writer and ZIP extraction routines |
<!-- MANIFEST:END -->
