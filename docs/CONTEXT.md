# CONTEXT.md — `docs/` Product Knowledge Workspace

## Workspace Role & Workflow

This room owns user-facing and agent-facing knowledge: the public `README.md` and
`docs/api/API.md` index, the CloudRedirect entry guide, modular deep-dives under `docs/references/`
and `docs/api/`, third-party attribution. Work sequence: (1) decide the document tier —
entry doc (`docs/cloud-saves.md`, `README.md`, `docs/api/API.md` stay slim) vs deep reference
(`docs/references/*.md`, `docs/api/*.md`); (2) edit the tier that matches your change —
never regress entry docs into monoliths; (3) keep every relative link resolvable
(README → entry → references); (4) architecture changes regenerate diagrams via the
`archify` skill (exports land in gitignored `.archify/`, `<type>-<topic>-<timestamp>` naming);
(5) structural drift (moved files, renamed commands) triggers the `routing-docs-maintainer` skill.

| Task / Stage | Read These Files | Skip These Files | Skills / MCPs / Commands Needed |
|---|---|---|---|
| CloudRedirect docs | `docs/cloud-saves.md` (entry), `docs/references/cloud-redirect-{architecture,runbook,backends}.md`, `README.md` §Cloud Saves | `.agents/skills/**` internals, `bin/` | Skill `archify` for diagram regen into `.archify/` |
| API surface change | `docs/api/{ipc,rest}.md`, `docs/api/API.md` (index), `src/presenter/` route owner + `src/core/api.*` | `src/model/`, `src/core/` internals beyond api | Update contract docs in same change as code (see `src/CONTEXT.md` row) |
| Third-party / licensing | `docs/THIRD_PARTY.md`, `docs/LICENSE/<component>/`, `.gitmodules` | `pkg/autocrack/` (vendored runtime dump), `lib/` | Never hand-list vendored `pkg/autocrack` DLLs in THIRD_PARTY.md |
| Version / changelog write-up | `res/version` (Changelog block), `docs/cloud-saves.md` if cloud behavior changed | `bin/`, `obj/` | Skill `release-workflow` for packaging gates |
| Workspace / routing housekeeping | root `AGENTS.md`, all six `*/CONTEXT.md`, `.agents/rules/*` inventory | `src/**`, `bin/**` | Skill `routing-docs-maintainer` |
