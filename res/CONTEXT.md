# CONTEXT.md — `res/` Runtime Resources & CEF Injection Workspace

## Workspace Role & Workflow

This room owns everything the installed client loads at runtime: `res/inject-scripts/`
(CEF JS by category — `core/`, `store/`, `library/`, `cloud/`, `shell/`, `vendor/`),
config template + declarative migration rules, `cloud_redirect.so` + `cloud_redirect_cli`,
version file, branding, desktop entries. Work sequence: (1) read
`res/inject-scripts/README.md` before touching any script — it is the category map and
per-file spec; (2) edit inside the existing category folders; (3) confirm the loader
search paths in `src/view/cdpinject.cpp:850-875` still cover your file; (4) run the UI
gates (`make test-ui-p0`, `make test-flow`); (5) release packaging copies this folder
verbatim (`scripts/lib/release.sh`) — never rename without updating that flow.
`res/SLSlogos/` is a git submodule: never modify or commit into it.

| Task / Stage | Read These Files | Skip These Files | Skills / MCPs / Commands Needed |
|---|---|---|---|
| CEF inject-script change | `res/inject-scripts/README.md`, target script inside its category dir (for library app details: edit modular `res/inject-scripts/library/app-details/` and run `make bundle-scripts`), `src/view/cdpinject.cpp` (search dirs) | `src/core/`, `src/model/`, `pkg/`, `bin/` | `make bundle-scripts`; `make test-ui-p0` / `make test-flow`; MCP `vibe-annotations` + Skill `vibe-implement-annotations` for annotation work; CDP live check via Skill `cef-cdp-injection` |
| Config template change | `res/config.yaml`, `res/config.diff.yaml`, `res/cache.diff.yaml`, `src/model/migrations/`, `tests/unit/test_config.cpp` | `res/inject-scripts/` | `make test`; follow the `$(OldKey)$(NewKey):` bridge syntax (actions: rename, rename_and_convert, remove, convert) |
| `cloud_redirect.so` replacement | `.gitignore` exception line `!res/cloud_redirect.so`, `docs/references/cloud-redirect-architecture.md` (§1 Components), `src/core/cloud_oauth.*` | `bin/`, `pkg/` | `make check-cloudredirect` — engine version (v2.6.5) and docs must stay in lockstep |
| Version / release bump | `res/version` (`Version: beta-v1.2.0` + Changelog — feeds Makefile `DATE`), `res/updates.yaml` (SafeModeHashes per client build) | `bin/`, `obj/` | Skill `release-workflow` owns `./install.sh release` — orchestrator-only |
| Branding / launcher assets | `res/banner.png`, `res/logo.svg`, `res/navi.desktop`, `res/navi-native.desktop`, `res/update-all.sh` | `res/SLSlogos/` (git submodule — read-only), `res/vendor`-style blobs | — |
