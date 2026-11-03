---
name: routing-docs-maintainer
description: keeps the 3-layer routing docs of SLSsteam_n_ACCELA accurate — the root AGENTS.md floor plan and every workspace CONTEXT.md (src, res, tools, tests, docs, plans). Use after adding/removing a skill or rule, moving a file a routing row references, renaming Makefile/install.sh commands, creating a workspace folder, or when a referenced file/README link stops resolving. Enforces exact-uppercase CONTEXT.md naming and the 4-column routing-table contract.
---

# Routing Docs Maintenance

## Why This Exists

Navi routes agent work through Layer-1/2 context files instead of repo-wide reads.
Those docs rot whenever the tree moves under them; this skill audits and heals them
without touching product code.

## Triggers

- A file/directory referenced by a routing "Read" column was moved, renamed, or deleted.
- A new `.agents/rules/*`, `.agents/skills/<id>/`, or Makefile/`install.sh` command appeared or disappeared.
- A workspace folder was added/renamed (new `*​/CONTEXT.md` peer needed).
- Any lowercase or drifted `CONTEXT.md` / `AGENTS.md` reference is spotted.

## Audit Procedure

1. **Inventory reality first** (bounded commands, never repo-wide dumps):
   `ls .agents/rules .agents/skills` · `grep -E '^[A-Za-z-]+:' Makefile` ·
   `ls docs docs/api docs/references plans tests` · `ls src/{core,model,presenter,view}`
2. **Root `AGENTS.md`**: floor plan covers every top-level workspace with a `CONTEXT.md`;
   skill enumeration matches `.agents/skills/`; the never-read list matches actual
   generated/vendored dirs; naming conventions still match reality — never invent.
3. **Each `CONTEXT.md`**: every path in "Read These Files" exists; every "Skip" folder
   exists and is genuinely skippable for that row; every skill/command in the last
   column resolves to `.agents/skills/<id>/SKILL.md` or a real command.
4. **Column contract**: header row must remain exactly
   `| Task / Stage | Read These Files | Skip These Files | Skills / MCPs / Commands Needed |`.
5. **Case discipline**: files are `AGENTS.md` / `CONTEXT.md` (exact uppercase); all
   references across routing docs use that casing.
6. **Public-link check**: `README.md` → `docs/cloud-saves.md` → reference tiers, and
   root `API.md` → `docs/api/*` still resolve (`grep` + spot `test -e`).

## Output

Concise report: per-file diff summary, list of healed dead references, and any drift
that needs a *product-code* change instead (report separately — never bundle rule or
`src/` edits into this maintenance action).
