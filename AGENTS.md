# AGENTS.md — Navi Workspace Router

## Project Overview

**Navi** (repo `SLSsteam_n_ACCELA`) is a 32-bit C++ preload engine (`bin/libnavi-<hash>.so`)
injected into the Linux/SteamOS Steam client: it unlocks unowned games (FakeAppIds, depot
keys, manifest/lua), integrates CloudRedirect cloud saves (local / gdrive / onedrive / r2 / s3),
merges achievements, and enriches the Steam CEF UI via injected JavaScript.
Stack: C++20 `-m32` under strict MVC (`src/{core,model,presenter,view}`), CEF JS,
Googletest + pytest (Bubblewrap Gate 0.7 integration harness), C# grabber tools,
Bash installer (`install.sh` + `scripts/lib/*`), Makefile build with hash-keyed
`obj/`/`bin/` caches, Nix flake + Docker packaging, Arch PKGBUILDs (`pkg/`), and
live-hardware verification on real Steam Decks (`scripts/dev/deck-ctl.sh`).

## The Floor Plan — enter the room that matches the task, read its CONTEXT.md first

| Workspace | When to enter |
|---|---|
| `src/CONTEXT.md` | Any change to the native engine: hooks, models, REST presenter, config, migrations |
| `res/CONTEXT.md` | CEF inject-scripts, config/migration templates, `cloud_redirect.so`, version & branding |
| `tools/CONTEXT.md` | cef-annotator, schema/ticket grabbers, steamstub-unpacker, helper binaries |
| `tests/CONTEXT.md` | Running or adding gates: unit, integration (Gate 0.7), UI journeys, device live tests |
| `docs/CONTEXT.md` | Architecture/API write-ups, diagrams, third-party attribution |
| `plans/CONTEXT.md` | New ideas, specs, active feature states, completed-item archives |
| `.agents/AGENTS.md` | Agent conduct: Jev routing (`route_subagent.py`), model selection, quota failover |

## Global Routing Rules (mandatory)

1. **Never read the repository wholesale.** Repo-wide `**` globs, recursive `cat`/`grep`
   dumps, and full-tree reads are forbidden.
2. On task start: pick ONE workspace from the floor plan → read only its `CONTEXT.md` →
   follow the task row's "Read These Files" and honor its "Skip These Files". Cross only
   into another workspace if the routing table says so.
3. Never open: `include/`, `lib/`, `third_party/`, `pkg/autocrack/` (vendored runtimes),
   `bin/`, `obj/`, `test-results/`, `.archify/`, `.pytest_cache/`, `docs/LICENSE/`,
   `artifacts/test-runs/` (historical evidence; read only when explicitly asked).
   Search with bounded globs (e.g. `find src -name '*.cpp'`), never unbounded finds.
4. Path anchoring — do not move or rename: `src/`, `res/`, `tools/`,
   `tests/integration/ui/harness/`, `Makefile`, `install.sh`, `scripts/**`, `.agents/**`,
   `test-results/` (hardcoded in `tests/integration/ui/harness/browser_driver.py`),
   submodules `res/SLSlogos` and `third_party/SteamAutoCrack`.
   Context files (`AGENTS.md`, `*/CONTEXT.md`) are the freely maintainable layer.
5. Verification set before claiming done: `./install.sh build` → `make test` →
   `make test-gate07`; add `pytest -q tests/integration` / `make test-ui-p0` for UI work.
   Bug fixes follow `.agents/rules/test-regression.md` (RED test before GREEN fix).
6. Domain invariants live in `.agents/rules/*` (`cpp_hooking_patterns.md`,
   `steam_cef_ui_patterns.md`, `steam_controller_mapping.md`, `devtools_interaction.md`,
   `test-regression.md`); deep procedures live in `.agents/skills/*`
   (`steam-version-update`, `steam-native-hooking`, `steamdeck-wireless-workflow`,
   `release-workflow`, `cef-cdp-injection`, `linux-elf-reversing`,
   `vibe-implement-annotations`, `plans-pipeline`, `routing-docs-maintainer`, …).
   Routing tables reference them by name — never duplicate their content here.

## File Naming & Versioning Conventions

- Tests: `test_<subsystem>.cpp` (Googletest unit) · `test_ujNN_<flow>.py` (UI journeys) ·
  `test_<topic>_integration.py` (pytest integration).
- Gates / release steps: `gateNN`; commits follow `cr<gate><letter>` (e.g. `cr7a`).
- Plans: one file per work item, moved folder→folder
  `plans/01-backlog → 02-specs → 03-active → 04-done`; specs versioned
  `<topic>-spec-vN.md`; historical roadmap seed:
  `plans/01-backlog/dev-plan.md` (kept untracked/ignored, mirroring legacy `Dev-Plan.txt`).
- Reports & handoffs: `YYYY-MM-DD-<type>-<topic>.md` — test-run reports specifically go
  to `tests/reports/YYYY-MM-DD-<gate>-<summary>.md`, never inside `tests/unit|integration`.
- Archify/diagram exports keep their own `<type>-<topic>-<timestamp>` naming under `.archify/`.
- Built/deployable artifacts are produced by `make`/`install.sh`, never hand-copied;
  versions come from `res/version` (currently `beta-v1.2.0`).
