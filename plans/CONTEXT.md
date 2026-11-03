# CONTEXT.md — `plans/` Feature Lifecycle Workspace

## Workspace Role & Workflow

This room is the single pipeline for every work item, executed as folder moves —
never copies, never duplicated threads:

```
plans/01-backlog/  (idea captured, one file per item)
      │  spec it
plans/02-specs/    (<topic>-spec-vN.md — versioned, vN+1 appended not overwritten)
      │  start implementation
plans/03-active/   (only what is in flight right now; one item per concern)
      │  close out
plans/04-done/     (outcome line + date appended; item archived here)
```

The historical seed is [`01-backlog/dev-plan.md`](01-backlog/dev-plan.md) — the legacy
`Dev-Plan.txt` wishlist (SLSsteam wishlist UI, copied-steam-wishlist page, family sharing
of lua+manifest, cross-device sync states). It is kept **untracked/ignored**, exactly as
the original was: new ideas move forward as their own files, never by editing the seed's
history into new shapes. Implementation work that starts here immediately routes to the
`src/` workspace rows; verification evidence comes from `tests/reports/`.

| Task / Stage | Read These Files | Skip These Files | Skills / MCPs / Commands Needed |
|---|---|---|---|
| Idea intake | `01-backlog/dev-plan.md` (seed wishlist) | `02-specs/`, `03-active/`, `04-done/`, `src/**` | Skill `plans-pipeline` |
| Spec an item | its backlog entry + `docs/CONTEXT.md` for reference-tier placement | `src/**` code beyond architecture entry files | Skill `plans-pipeline`; Skill `deep-reasoning` for architecturally risky items |
| Start implementation | `02-specs/<topic>-spec-vN.md` → then switch to `src/CONTEXT.md` task rows | `01-backlog/`, `04-done/` | Jev routing per `.agents/AGENTS.md` (`python3 .agents/scripts/route_subagent.py spawn …`) |
| Close-out / archive | this item's spec + latest `tests/reports/YYYY-MM-DD-*.md` evidence | all source trees | Skill `plans-pipeline`; session handoffs named `YYYY-MM-DD-handoff-<topic>.md` |
| Roadmap review | `01-backlog/`, `02-specs/`, `03-active/` | `04-done/` history, `src/**`, `res/**` | Skill `plans-pipeline` |
