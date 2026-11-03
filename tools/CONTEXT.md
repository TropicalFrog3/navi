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
| library-inject | `tools/library-inject/{main.cpp,Makefile}` → `bin/library-inject.so` | `src/`, `res/` | `make tools` + `make link-bins` |
| cloud_pending idle helper | `tools/cloud_pending/main.cpp` → `bin/navi-cloud-apply-pending`, `docs/references/cloud-redirect-runbook.md` §1 | `src/view/` | `make check-cloudredirect`; preserve `AssertSteamIdle()` fail-closed behavior |
