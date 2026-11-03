# CONTEXT.md — `src/` Native Engine Workspace

## Workspace Role & Workflow

This room owns the injected 32-bit engine (`libnavi.so`), strictly layered as MVC —
`src/core` (hooks/patterns/curl/zip/IPC), `src/model` (config, depotkeys, lua/manifest,
vdf, migrations), `src/presenter` (REST :9001 + IPC controllers), `src/view` (CEF
injection/wiring). Work sequence: (1) identify the owning layer and read its files +
the matching `.agents/rules/*` invariants; (2) implement minimal targeted changes with
`grep_search` and bounded `view_file` reads only; (3) for bug fixes, write the RED test
first per `.agents/rules/test-regression.md`, then fix; (4) verify `./install.sh build`
→ `make test`; (5) hand higher-level verification to the `tests/` workspace and never
run device or release side effects (deck-ctl.sh, `./install.sh release`) from here.

| Task / Stage | Read These Files | Skip These Files | Skills / MCPs / Commands Needed |
|---|---|---|---|
| New Steam client version | `.agents/skills/steam-version-update/SKILL.md` (5 gates), `res/version`, `res/updates.yaml`, `src/core/patterns.*`, `src/core/vftableinfo.*` | `include/`, `lib/`, `third_party/`, `pkg/`, `bin/`, `obj/` | Skill `steam-version-update` (+ its `scripts/audit_client_hash.py`, `scripts/audit_vftable.py`); `./install.sh build` |
| New native hook / feature | `.agents/rules/cpp_hooking_patterns.md`, `.agents/rules/steam_cef_ui_patterns.md`, owning MVC-layer files, `src/core/globals.hpp`, `src/core/mtvar.hpp` | `res/`, `tools/`, `docs/references/` | Skill `steam-native-hooking`; `./install.sh build` |
| Cloud-saves change | `docs/references/cloud-redirect-architecture.md`, `src/core/cloud_oauth.*`, `src/presenter/{cloud_manager,cloudredirect,cloud_pending_ops,prefix_bucket_sync}.*`, `tools/cloud_pending/main.cpp`, `docs/api/rest.md` | `tools/{schema-grabber,ticket-grabber}/`, `bin/` | `make check-cloudredirect`; keep `bin/navi-cloud-apply-pending` semantics (exit codes, fail-closed rollback) intact |
| Config / model / migration change | `src/model/config.*`, `src/model/config_default.hpp`, `res/config.yaml`, `res/config.diff.yaml`, `res/cache.diff.yaml`, `src/model/migrations/`, `tests/unit/test_config.cpp` | `src/view/`, `res/inject-scripts/` | `make test`; migration rules use `$(OldKey)$(NewKey):` bridge syntax |
| Bug fix (all defects) | `.agents/rules/test-regression.md`, `tests/unit/test_stubs*.cpp` (stub conventions), offending `src` file | unrelated test suites | Write RED test in `tests/unit` / `tests/integration` first; `./install.sh build && make test` |
| IPC / REST API change | `docs/api/ipc.md` or `docs/api/rest.md`, `src/presenter/*` route owner, `src/core/api.*` | `docs/references/` | Update the matching `docs/api/*.md` contract in the same change |
| CEF-facing change | `res/CONTEXT.md` first — scripts live there | `src/core/patterns.*` unless hook wiring needed | Skill `vibe-implement-annotations`; MCP `vibe-annotations` |
