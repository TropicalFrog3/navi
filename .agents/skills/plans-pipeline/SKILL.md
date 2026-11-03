---
name: plans-pipeline
description: enforces the feature lifecycle pipeline in the plans/ workspace of SLSsteam_n_ACCELA — one file per work item moved 01-backlog → 02-specs → 03-active → 04-done, with versioned specs and datestamped handoffs. Use when capturing an idea from a session, writing or revising a spec, starting implementation, closing out an item, or reviewing the roadmap.
---

# Plans Pipeline

## Stage Contract

| Stage | Location | Content | Exit condition |
|---|---|---|---|
| Backlog | `plans/01-backlog/` | Raw ideas, one file per item | Item is understood enough to spec |
| Specs | `plans/02-specs/` | `<topic>-spec-vN.md` | Applied-to layers, verification plan, risks defined |
| Active | `plans/03-active/` | Item file while in flight | Work verified via `tests/reports/` evidence |
| Done | `plans/04-done/` | Archived item + outcome line | Outcome line + date recorded, no open tasks |

## Invariants

1. **One file per item** — move folder→folder with the phase transition; never copy or fork threads.
2. **Specs version by appending** — a revised spec becomes `<topic>-spec-v<next>.md`; previous `vN` is superseded, not overwritten.
3. **`03-active/` stays honest** — only items currently being implemented; route to the target workspace's `CONTEXT.md` (`src/`, `res/`, or `tools/`) as soon as implementation starts, and back to `tests/CONTEXT.md` for gates.
4. **The seed is immutable** — `01-backlog/dev-plan.md` is the legacy `Dev-Plan.txt` wishlist (untracked/ignored). New ideas become new files next to it; never re-edit the seed's original content.
5. **Close-out requires one outcome line** — `Outcome: <shipped/rejected/deferred> — <1 line> — <YYYY-MM-DD>` at the very top of the archived spec, plus a link to the `tests/reports/YYYY-MM-DD-*.md` report if one exists.
6. **Datestamped attachments** — session handoffs: `YYYY-MM-DD-handoff-<topic>.md`; gate summaries: `YYYY-MM-DD-<gate>-<summary>.md`; they never share names with plan files.

## Typical Sequences

- **Intake:** session reveals an idea → write `plans/01-backlog/<topic>.md` (problem, motivation, pointer to relevant `src/`/`res/` files; no solutions yet).
- **Spec:** pull the item into `02-specs/`, extend with: owning MVC layer, invariants touched (`.agents/rules/*`), verification plan mapped to `tests/CONTEXT.md` commands, risks.
- **Execute:** move to `03-active/`; Jev-routed implementation per `.agents/AGENTS.md`; append gate evidence links as verification progresses.
- **Close:** verify, append outcome line + date, move to `04-done/`.
- **Review:** read only `01-backlog`, `02-specs` (latest `vN` per topic), `03-active`; skip `04-done` except for archaeology.

## Output Convention

When this skill's host agent finishes a session-level plan operation, it reports:
files created/moved (stages), the resulting pipeline state per item, and any spec version bumps.
