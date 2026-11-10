# CONTEXT.md — `tools/` Standalone Tooling Workspace

## Workspace Role & Workflow

This room owns self-contained helper programs that are *not* part of `libnavi.so`:
Python utilities (`tools/cef-annotator`, `tools/steamstub-unpack`), C# grabbers
(`tools/schema-grabber`, `tools/ticket-grabber`), and C++ helpers built into `bin/`
(`tools/library-inject` → `bin/library-inject.so`, `tools/cloud_pending` →
`bin/navi-cloud-apply-pending`). Work sequence: (1) change only the one tool you were
routed to; (2) build it via its `Makefile` or the aggregate `make tools`; (3) verify
with the task-specific command below; (4) never link tool code into the engine or
wire tool outputs into `src/` releases without a spec from the `plans/` workspace.

| Task / Stage | Read These Files | Skip These Files | Skills / MCPs / Commands Needed |
|---|---|---|---|
| cef-annotator (visual annotations) | `tools/cef-annotator/{README.md,SKILL.md,annotator.py,overlay.js}`, `.agents/mcp_config.json` (MCP on port 3846) | `src/`, `tests/` | Skills `vibe-implement-annotations` + `vibe-server-setup`; `python3 tools/cef-annotator/annotator.py` |
| schema-grabber / ticket-grabber | `tools/{schema-grabber,ticket-grabber}/{Program.cs,Makefile,*.csproj}`, `nix-modules/{schema-grabber,ticket-grabber}.nix` | `src/`, `res/`, `pkg/` | `make schema-grabber` / `make ticket-grabber` (C# toolchain) |
| steamstub-unpacker | `tools/steamstub-unpack/{steamstub_elf_unpack.py,xkb_fix.c}` | `pkg/`, `third_party/`, `bin/` | Skill `linux-elf-reversing` (pyelftools workflow); offline only |
| library-inject | `tools/library-inject/{inject_main.cpp,Makefile}` → `bin/library-inject.so` | `src/`, `res/` | `make tools` + `make link-bins` |
| cloud_pending idle helper | `tools/cloud_pending/cloud_pending_main.cpp` → `bin/navi-cloud-apply-pending`, `docs/references/cloud-redirect-runbook.md` §1 | `src/view/` | `./install.sh check-cloudredirect`; preserve `AssertSteamIdle()` fail-closed behavior |

## File Manifest

<!-- MANIFEST:BEGIN -->
| File | Purpose |
|---|---|
| `bundle-app-details.py` | Bundles modular app-details JS and HTML into monolithic inject script. |
| `cef-annotator/annotator.py` | Visual annotation server and overlay for Steam CEF UI inspection |
| `cloud_pending/cloud_pending_main.cpp` | Out-of-process idle helper executable applying queued save ops |
| `library-inject/inject_main.cpp` | Lightweight LD_PRELOAD injection helper for library hook testing |
| `schema-grabber/Program.cs` | CLI tool for downloading Steam app schemas and achievement data |
| `steamstub-unpack/steamstub_elf_unpack.py` | Offline unpacker and decryptor for SteamStub DRM ELF binaries |
| `ticket-grabber/Program.cs` | CLI tool generating authentication tickets for Steam client emulation |
| `ui-live-preview/cdp_reloader.py` | WebSocket CDP client reloading CEF scripts on source changes |
| `ui-live-preview/file_watcher.py` | Inotify file watcher monitoring inject-scripts for live reload |
| `ui-live-preview/mock_api.py` | Mock REST API server simulating engine responses for live preview |
| `ui-live-preview/server.py` | Live preview HTTP server serving mock Steam UI and scripts |
| `ui-live-preview/test_preview_server.py` | Tests for live preview server endpoints and WebSocket reload |
<!-- MANIFEST:END -->
