# Timing Report — Cloud Apps Inventory After D3 (O(files) Upsert/Dedup)

- **Date:** 2026-11-03
- **Type:** Live-hardware verification (`timing-cloud-apps-inventory-d3`)
- **Baseline / prior checkpoints:** `tests/reports/2026-11-03-timing-cloud-apps-inventory.md`
  (pre-D1), `tests/reports/2026-11-03-timing-cloud-apps-inventory-d1.md` (D1 + D4).
- **Verified item:** spec `plans/03-active/cloud-apps-inventory-speedup-spec-v1.md` —
  D3: `upsertFile` and the `ListAppCloudFiles` child-file dedup move from
  O(files²) comparisons to O(file) hash lookups.
- **Raw artifacts:** `test-results/timing-cloud-apps-inventory-d3/`.

## Environment

| Item | Value |
|---|---|
| Steam client | live `ubuntu12_32/steam` **pid 220478** (payload deployed while Steam was down, then client relaunched) |
| Navi preload | `navi/libnavi` mapped into pid 220478 (verified via `/proc/maps`) |
| Deployed payload | `~/.local/share/navi/libnavi.so` sha256 `121f9342 1b845c5a…` = repo `bin/libnavi.so` (D1+D3 engine) |
| Library shape | unchanged: 63 manifests / 114 lua / 457 depotcache manifests / 2 storage accounts / **169 apps** |

## Engine changes (D3)

1. **`upsertFile`** (`ListAppCloudFilesDirect`): linear `fileMap` scan replaced by
   `std::unordered_map<std::string, size_t>` slot index (insert order preserved;
   merge semantics kept byte-identical: folder precedence, flag ORs,
   remoteBlobPath replacement, size/mtime guards).
2. **Child-file dedup** (`ListAppCloudFiles`): linear `results` scan per child file
   replaced by `std::unordered_set<std::string> resultNames` (seeded from the base-app
   results; new child names inserted as they are merged).
3. **Instrumentation** (test-only, mirrors the D1 `ScanCounters` pattern):
   `ScanCounters::fileUpsertProbes` and `fileDedupProbes` count the lookup probes;
   `GetScanCountersForTest`/`ResetScanCountersForTest` extended.

## Unit-level RED → GREEN (per `.agents/rules/test-regression.md`)

| Test | RED measurement | GREEN measurement | Bound |
|---|---|---|---|
| `CloudManagerTest.UpsertFileIndexIsLinearNotQuadratic`<br>1200-file mirror fixture, N upserts | probes = **719 400** (= N(N−1)/2) | ≈ N (1 probe per upsert) | `< 4N = 4800` |
| `CloudManagerTest.ChildFileDedupIsLinearNotQuadratic`<br>600 base + 600 child files (20 overlap) | probes = **520 430** | ≤ ~child-file count (set lookup + insert) | `< 3×600 = 1800` |

RED states were confirmed failing first, fixes applied after (both tests in
`tests/unit/test_cloud_manager.cpp`). The dedup test additionally asserts the
no-duplicate-`fileName` contract (exact-count assertions are fixture-dependent — e.g.
the 3d block lists the base app's own `.lua`, +1 entry).

## Verification set

| Gate | Result |
|---|---|
| `./install.sh build` | ✅ (`libnavi.so` → `121f9342…`, deployed 21:08 while Steam was down) |
| `make test` | ✅ 96/96 unit (incl. two new D3 tests) + 91 passed / 1 skipped pytest integration |
| `make test-gate07` | ✅ all 7 checkpoints |
| `/cloud/apps` equivalence | ✅ **169/169 apps exact vs baseline** (clean warm pass, below) |
| Startup interference note | warm pass in the middle of prefix-sync startup captured data drift; clean pass restored exact equivalence |

## Live probe results (pid 220478)

| Run | Window | Wall | utime Δ | stime Δ | syscr Δ | rchar Δ | CLI spawns |
|---|---|---|---|---|---|---|---|
| cold | 21:08:41 → 21:09:01 | 20.04 s | 3.77 s* | 1.27 s | 54 553 | 291.82 MB* | 26 |
| warm1† | 21:09:06 → 21:09:24 | 17.59 s | 1.83 s† | 0.76 s | 30 420 | 872.08 MB† | 24 |
| **warm2 (clean)** | 21:10:37 → 21:10:56 | **18.82 s** | **1.58 s** | 0.69 s | **18 852** | **2.93 MB** | **22** |

`*` cold window on this boot contained cold page cache (library files read from disk;
baseline note "cold includes Steam-boot IO noise" applies). `†` warm1 fell into Navi's
**startup prefix-sync job** (`PrefixBucketSync` enqueue `Mode::Full "startup"` —
`src/presenter/prefix_bucket_sync.cpp:2184`): the census captured that worker's
`cloud_redirect_cli sync-remote-app 250900` + `publish-full-manifest 250900`
children whose mirror materialization changed app 250900's listing mid-request
(fileCount 19 → 34) and inflated IO counters (872 MB = remote blob pull). This is a
background worker cycle, NOT the inventory request path; warm2 (after the job
drained) shows zero such children and the pre-D1-equivalent IO profile.

### Interpretation

- Wall remains ≈17.5–19 s, still owned by the serialized `cloud_redirect_cli` chain
  (22 spawns in the clean pass) — D2's scope.
- Engine CPU sits at ~1.4–1.6 s utime on warm passes (same as after D1): the
  inventory's in-process work is no longer measurable above the request's CLI
  orchestration noise at this fixture size — the linear-time property is proven at
  unit level (probes collapse from 719 400 / 520 430 to O(N)); cross-boot wall/CPU
  windows are not comparable to a ±0.1 s precision.
- **Response equivalence: 169/169 apps exact vs the baseline JSON in the clean warm
  pass** — the 3856460 count observed differing at 20:37 (data drift, D1 report) is
  no longer present in this state: the later background sync reshaped that app's
  mirror; engine-side semantics identical.
- Behavior guard: the two D3 tests back up the same counters that would catch any
  regression to O(files²).

## Next step in spec rollout

D2 — single `list-remote-apps` batch replacing the per-app
`list-remote-app-files` chain (spec test
`CloudManagerTest.ListRemoteAppsBatchedCallCountIsOne`, RED first). Targets the
≤3 s warm / ≤7 s cold spec goals: the serial chain is now the entire wall-clock
cost of the request.
