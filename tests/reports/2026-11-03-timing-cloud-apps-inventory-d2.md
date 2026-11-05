# D2 Decision Report — CLI Listing Consolidation (Fallback Branch Taken)

- **Date:** 2026-11-03
- **Type:** Spec-item decision record (`tests/reports/…-inventory-d2.md`)
- **Spec:** `plans/03-active/cloud-apps-inventory-speedup-spec-v1.md` — D2
  (CLI listing consolidation).

## Decision per spec verify-then-fallback clause

> "Only if the verified output shape lacks per-app file inventories, fall back to
> today's per-app spawns voluntarily (keep D1 gains) and record that outcome in
> the run report — **no new CLI verb is spec'ed here**."

### Verified shape (against the live provider, 21:11:53–21:12:03)

`cloud_redirect_cli list-remote-apps gdrive 1254383366` returns one JSON per run:

```json
{"success":true,"apps":[
  {"app_id":"250900","file_count":21,"total_size":1792328},
  {"app_id":"3264850","file_count":11,"total_size":50814},
  ...
]}
```

- **No per-app `files` blocks are present** — the batch verb returns only
  `app_id / file_count / total_size` summaries, so it can NOT feed the per-app
  file maps the spec requires (`semantics equal to today's list-remote-app-files`
  results would be void; inventories would be lost).
- Artifacts: `test-results/timing-cloud-apps-inventory-d2/list-remote-apps-shape.json`
  (response) and `…/list-remote-apps-run.log` (log: 6 remote folders listed,
  ~10 s wall for the single batched call on this account — 6 remote-confirmed
  apps: 250900, 3264850, 3575730, 3628760, 3856460, 4110220).

### Consequence (adopted)

- **Fallback branch: taken.** The engine keeps one `list-remote-app-ids`
  discovery + per-remote-app `list-remote-app-files` spawns exactly as today;
  D1 + D3 + D4 improvements stay fully in effect (no code regression path).
- No new CLI verb introduced (spec keeps `make check-cloudredirect` engine
  lockstep out of scope).
- Spec targets degrade gracefully per the rollout plan to **≤ 3 s warm /
  ≤ 11 s cold**; measured today: **18.8 s wall idle-state request**
  (≈22 serial spawns ≈ 16.5 s of it; engine CPU ≈ 1.4–1.6 s).
- Follow-on work items still possible later as NEW specs (not requested here):
  parity materialization of remote inventories through PrefixBucketSync pull
  jobs to serve listings locally, or a new batch verb (which would trigger
  engine/CLI lockstep checking).

## Rollout posture at close-out

- D1 — implemented + verified (`tests/reports/2026-11-03-timing-cloud-apps-inventory-d1.md`).
- D3 — implemented + verified (`tests/reports/2026-11-03-timing-cloud-apps-inventory-d3.md`).
- D4 — delivered as part of `ObtainScanIndex()` stamp cache (verified within D1).
- D2 — fallback branch (this report).
