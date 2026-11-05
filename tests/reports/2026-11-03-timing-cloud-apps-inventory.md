# Timing — View Steam Cloud inventory fetch, live client

- **Date / gate:** 2026-11-03, per-device live timing of `GET /cloud/apps` (no gate test variant; plan item `plans/03-active/…` was `verify-cloud-apps-fetch-timing.md`, now archived)
- **Environment:** local dev machine, live Steam client (pid 51496, `ubuntu12_32/steam`) with Navi injected — installed engine `~/.local/share/navi/libnavi.so` **hash-identical to `bin/libnavi-96e71e5e….so` (repo build, Nov 3 12:59)**, so the profiled binary is current code. Provider `gdrive`, authenticated. Raw artifacts: `test-results/timing-cloud-apps-inventory/` (census.log, cpuwatch.log, per-run response JSONs).
- **Plan item:** `plans/01-backlog/verify-cloud-apps-fetch-timing.md` — hypotheses H1–H6.

## Baseline timings (12 measured requests, all same payload 43521 B / 169 apps)

| # | Run | time_total |
|---|---|---|
| 1 | cold #1 | 79.96 s |
| 2 | warm #1 | 159.62 s ⚠ (single outlier — see Anomalies) |
| 3 | warm #2 | 80.39 s |
| 4 | strace-attempt run | 84.68 s |
| 5 | census v1 | 86.39 s |
| 6 | census v2 | 85.33 s |
| 7 | census v3 | 82.71 s |
| 8 | census v4 | 82.86 s |
| 9 | cpuwatch v1 | 83.12 s |
| 10 | cpuwatch v2 | 83.31 s |
| 11 | instrumented-IO run | 85.10 s |
| 12 | census-final | 86.04 s |

Steady state ≈ **80–86 s per page view**. No within-process caching across requests (warm ≈ cold).

## Attribution (85.1 s instrumented run)

| Component | Cost | Evidence |
|---|---|---|
| **Engine CPU — repeated in-process library scans/parsing** | **~70.4 s user CPU** (ut 7041 ticks @100Hz) + 1.58 s sys | Δ `/proc/51496/stat` during request vs idle baseline **2.7% CPU** (20 s window) → request-side CPU ≈ 82% of one core for the full request, i.e. ~70 s attributable |
| `cloud_redirect_cli` child spawns (serial) | **~22 spawns / ≈ 11–12 s wall** | 50ms `ps` census: 22 unique pids, ppid=51496, zero overlaps, 21/22 back-to-back chained, ~0.45–1.2 s lifespans each; 225 sampled lines `list-remote-app-files`, 9 × `list-remote-app-ids` |
| Real disk IO | **0 B** (page-cache hot) | `/proc/51496/io` `read_bytes` delta = 0 (writes 135 KB logs only) |
| File-scan volume | **204 484 read() syscalls, 65.3 MB rchar** per request | Δ `/proc/51496/io` (`syscr` 4 689 011 − 4 484 527; `rchar` +64.9 M) |

Library shape (drives the scan volume): 63 installed `appmanifest_*.acf`, 114 `stplug-in/*.lua`, 457 `depotcache/*.manifest`, 113 compatdata dirs (106 with `steamuser` profiles), 169 apps returned, only 16 mirror app dirs (6 with sync markers), 2 storage accounts, `appinfo.vdf` just 5 MB.

**Manual CLI per-command cost (4 runs):** `list-remote-app-files` 0.40–0.68 s; `list-remote-app-ids` 0.62 s — engine spawns are not the stall.

## Verdict per hypotheses

| Hyp | Verdict | Notes |
|---|---|---|
| **H1** per-app serialized CLI spawns | ❌ **major but not dominant** | Only ~22 spawns — because `ListCloudApps` sets `effectiveProvider="local-only"` for apps not confirmed remote (`cloud_manager.cpp:2409`), so most of the 169 candidates never touch the CLI. ≈ 11–12 s of 85 s |
| **H2** depot/title resolution re-scans | ✅ **DOMINANT** | ~70 s user CPU + 204k read syscalls/req; structurally matches `GetParentAppIdForDepot` → `FindDepotOwnerInAppManifests` per candidate over all 63 manifests, `GetChildDepotIdsForApp` re-parsing all 114 lua per top-level app (called twice per app), title lookups per `.manifest` candidate |
| **H3** Proton-prefix walks | ❌ falsified | All 106 compatdata `steamuser` walks = **0.23 s total** (warm) |
| **H4** discovery CLI spawns | ❌ falsified | 1–2 spawns (`list-remote-app-ids`) ≈ 1 s |
| **H5** `appinfo.vdf` full read | ❌ falsified | File is 5 MB here (per-process-mu cached anyway) |
| **H6** provider/credential stalls | ❌ not dominant | All CLI spawns ≤ 1.2 s in-run; manual calls 0.4–0.7 s. One 159.6 s outlier suggests possible transient provider slowness, not reproducible |

## Anomalies

- Run 2 (159.6 s) is the single non-reproducible outlier; a later back-to-back request was normal (80.4 s). Not attributable to engine code; noted for completeness.
- `strace -f -p 51496` is unavailable on this host (`kernel.yama.ptrace_scope=1`); census built from 50 ms `ps` sampling with ppid + subcommand anchoring instead (validated with synthetic spawns, 19/30 hits).
- The single-threaded REST accept loop (`storeinject.cpp:898-940`) serializes requests; concurrent clients pay a connect penalty (observed 134 s "Failed to connect" for queued curls). This is a fix-relevant finding, not an anomaly.

## Conclusion & gate decision

The wall-clock is **not** spawn-dominated: **~82% of the view's latency is the engine re-scanning the whole library per candidate** (manifest/lua/title scans + per-app local walks, echoed through `syscr` ≈ 204k/read syscalls and ~70 s user CPU), plus ~11–12 s of strictly serial CLI listing for the 6-ish remote-confirmed apps.

**Outcome: H2 confirmed → per the gate rule, revise `plans/01-backlog/cloud-apps-inventory-speedup.md` ranking first** (scan-once/deps-map + parsing memoization now leads; CLI batch/lazy becomes secondary although still worth taking since it is a structural constant per remote app), then spec.

Fix-relevant notes for the revision (evidence-based):
1. **Depot/title maps must be built once** per inventory pass: today every candidate re-reads every manifest (`GetParentAppIdForDepot`), every top-level app re-parses the entire `stplug-in` dir, twice (`GetChildDepotIdsForApp` inside step 3 and inside `ListAppCloudFiles`).
2. **Title resolution** per `.manifest` candidate is uncached CPU; `appinfo.vdf` path is already size-cached (good pattern to reuse).
3. The 22 CLI spawns are the second item: batching (`list-remote-apps` once, ~4.7 s per code comment) or lazy per-app fetching drops their ~12 s serial chunk.
4. Request scope note: the **169 candidates** are mostly `localConfirmedApps` umbrella (lua/manifest discovered), not provider apps; discovery breadth itself is a lever (e.g. respecting `inRemoteCloud`/flags from config before scanning).
