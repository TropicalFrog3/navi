# Timing Report — Cloud Apps Inventory After D1 (Scan-Once `ScanIndex`)

- **Date:** 2026-11-03
- **Type:** Live-hardware verification (`timing-cloud-apps-inventory-d1`)
- **Baseline reference:** `tests/reports/2026-11-03-timing-cloud-apps-inventory.md`
  (85.1 s wall / 70.4 s user CPU / 204 484 syscr / 65.3 MB rchar per request)
- **Verified item:** spec `plans/03-active/cloud-apps-inventory-speedup-spec-v1.md`
  — D1 (one-pass library `ScanIndex`) including its D4 side effect
  (pass-lifetime cache via `ObtainScanIndex()` stamp) and the
  stem-guard follow-up fix found during live verification.
- **Raw artifacts:** `test-results/timing-cloud-apps-inventory-d1/`
  (`resp_cold.json`, `resp_warm1.json`, `resp_warm2.json`, `deltas_*.txt`,
  `census_*.log`, `timing_*.txt`).

## Environment

| Item | Value |
|---|---|
| Steam client | local dev Steam, `ubuntu12_32/steam` **pid 103658** (probes 20:37–20:39; client subsequently shut down for the next deploy cycle) |
| Navi preload | `grep 'navi/libnavi' /proc/103658/maps` confirmed before probing |
| Deployed payload | `~/.local/share/navi/libnavi.so` sha256 `c7ad475f241a9117…` (= repo `bin/libnavi.so`, build 20:31; hardlink target `bin/libnavi-96e71e5e…so` is keyed by `FLAGSSHA`, not source hash — see Makefile:54) |
| Library shape | unchanged vs baseline: 63 `appmanifest_*.acf` (1 library folder), 114 `stplug-in/*.lua`, 457 `depotcache/*.manifest`, 2 storage accounts, **169 apps** returned |
| Provider | `gdrive`, accountId `1254383366` active |

## Methodology (mirrors baseline)

- One instrumented `curl` per pass against `http://127.0.0.1:9001/cloud/apps?provider=gdrive&accountId=0`
  (no `/api/` prefix — `/api/cloud/apps` answers a 0-byte 200 instantly and is NOT the inventory).
- While the request ran, 50 ms `ps` sampling recorded every child of the engine
  (ppid = 103658) — the `cloud_redirect_cli` census, same technique as the baseline report
  (`strace` remains unavailable on this host, `kernel.yama.ptrace_scope=1`).
- CPU delta from `/proc/103658/stat` (utime/stime ticks), IO delta from `/proc/103658/io`
  (rchar/syscr). **Probe-script note:** the script's awk `io_delta` lines are broken and
  print `0`; all IO deltas below were parsed from the raw `io_before[...] after[...]`
  strings instead — keep that convention for the next report.
- Three passes: one cold (first inventory after client boot + payload swap) and two warm.

## Results — wall, CPU, IO per pass

| Run | Window (HH:MM:SS) | Wall | utime Δ | stime Δ | syscr Δ | rchar Δ | Response size |
|---|---|---|---|---|---|---|---|
| cold | 20:37:15 → 20:37:33 | **17.49 s** | **1.39 s** | 0.68 s | 18 539 | 2.85 MB | 43 521 B |
| warm1 | 20:37:36 → 20:37:54 | **17.52 s** | **1.43 s** | 0.66 s | 19 297 | 2.92 MB | 43 521 B |
| warm2 | 20:38:37 → 20:38:55 | **17.98 s** | **1.42 s** | 0.63 s | 18 632 | 2.86 MB | 43 521 B |

Warm steady state ≈ **17.5–18.0 s per request** (baseline: 80–86 s). Warm ≈ cold here because
the client was fully booted before the first probe (the earlier "cold includes Steam-boot IO
noise" convention no longer applies on this boot).

### Collapse vs baseline

| Metric | Baseline (pre-D1) | After D1 | Δ |
|---|---|---|---|
| Wall per request | 85.1 s | **17.5 s (warm)** | **−79 % (4.9×)** |
| Engine user CPU | 70.4 s | **1.4 s** | **−98 % (~50×)** |
| read() syscalls / request | 204 484 | **~18.5–19.3 k** | **−91 % (~10.8×)** |
| rchar / request | 65.3 MB | **~2.9 MB** | **−96 % (~22×)** |
| Serial CLI spawns | 22 | 22 (+1, see Findings) | ≈ unchanged (D2 scope) |

Attribution inversion: the request cost moved from **in-process rescans (≈70 s CPU)** to
**the serialized `cloud_redirect_cli` chain (≈16.5 s wall of the 17.5 s request)** —
census child lifespans chain back-to-back from first sample (request start) to last,
`list-remote-app-files` per app + `list-remote-app-ids`, exactly D2's target.

## Response equivalence vs baseline

- **All three passes byte-identical** (43 521 B each; cold == warm1 == warm2). The index
  cache serving the second/third pass is transparent (`D4`).
- **vs baseline JSON: 168/169 apps exact** (title, fileCount, totalSizeBytes, childAppIds)
  and identical app set/ordering shape.
- **1 app differs:** `3856460` fileCount 6 → **8**, totalSizeBytes 327 606 → **641 130**
  (+313 375 +149), title/children unchanged.
  - **Root cause = GitHub-style data drift, not engine behavior.** Between the baseline
    capture (**17:37**, `test-results/timing-cloud-apps-inventory/*.json` mtimes) and the
    mirror reorganization (**20:24**), CloudRedirect's sync materialized two hash-suffixed
    blobs into `~/.config/CloudRedirect/storage/1254383366/3856460/`:
    `depotcache/3856461_…​.manifest/ceeb876b…` (313 375 B) and
    `stplug-in/3856460.lua/59d87fb5…` (149 B). Both are distinct `fileName`s alongside the
    plain `depotcache/…​.manifest` and `stplug-in/…​.lua` entries (which the listing also
    produces via its unchanged 3d block).
  - `ListAppCloudFilesDirect` / the child-merge dedup in `ListAppCloudFiles` are
    byte-untouched by D1 (`git diff`: hunk `@@ -2035,12 +2204,19 @@` shows only the
    wrapper split and `GetChildDepotIdsForApp(appId, index)`); legacy code on today's
    disk lists the same 8 entries. GSE probe details: `/files` dump reproduced the disk
    tree exactly (8 entries, sum 641 130), stable across repeat queries.
  - **Follow-up consideration (out of D1 scope):** `<path>/<sha-hash>` blob names being
    listed *in addition to* `<path>` double-counts mirrored metadata. Worth a later
    ticket; spec-equivalence for D1 targets routing/grouping, which holds.

## Grouping inversion regression found + fixed live

First live profile (previous deploy `3a2eacc5…`, engine pid 95805) showed **12 apps grouped
inverted**: Raft 648800/648801, Train45 3711590/3711591, Cursed Words 3856460/3856461,
How Many Dudes? 3934270/4815311, Innocent Assault SS 4265410/4265411, Big Tiddy Goth
4997080/4997081.

- **Root cause:** legacy `FindDepotOwnerInLuaPlugins` has TWO per-file/depot guards —
  (1) `stem == depotId` (a file named after the depot never owns it) and (2)
  `resolvedParent == depotId` (self-listing). The first D1 index builder mirrored only
  guard 2, so a real-library lua file shaped
  `addappid(<childKey>, 0, "…")` → parser `appId = <child>`; `addappid(<self>)`
  inverted parent/child pairs.
- **Fix:** added guard 1 to the index builder's `trackOwner` (`src/presenter/cloud_manager.cpp`).
- **Regression gate:** `CloudManagerTest.InventoryLuaSelflistedStemAppDoesNotInvertOwnership`
  (RED first: reproduced the live-data shape, expected `GetParentAppIdForDepot(self) == 0`,
  got inverted; then GREEN after the fix) in `tests/unit/test_cloud_manager.cpp`.

## Scan-once evidence (unit level)

`CloudManagerTest.InventoryScanIndexReadsLibraryOncePerPass`
(`tests/unit/test_cloud_manager.cpp`) builds a full mock library (63 manifests / 114 lua
equivalent) and asserts the counters across two `ListCloudApps` passes:

| Pass | Manifest reads | Lua parses | Note |
|---|---|---|---|
| pre-D1 (two passes) | 360 | 308 | per-candidate rescans |
| post-D1 pass 1 | 63 | 114 | one read per library file |
| post-D1 pass 2 (cached) | 0 | 0 | `ObtainScanIndex()` stamp hit (D4) |

Invalidation is still guarded inside the same test (library edit → next pass rebuilds).

## Findings / notes for D2+D3

1. **Remaining 17.5 s ≈ serial CLI chain** (22 spawns ≈ baseline's 22; sub-mix unchanged).
   D2 (single `list-remote-apps` batch) must cut this to hit the ≤3 s warm / ≤7 s cold
   spec target.
2. **Census count was 23, not 22**: an extra
   `cloud_redirect_cli sync-remote-app gdrive 1254383366 3856460` child appears in all
   three windows (first sample at request start), tied to the 20:24 mirror re-layout of
   that one app (its repair path runs per listing) — excluding it, spawn count matches
   the baseline's 22.
3. **Probe script io_delta lines print 0** (awk bug) — parse raw `io_before/after`
   strings as done here; fix the script for the D2/D3 reports.
4. Windows convention from the baseline holds only when the client is mid-boot; here both
   cold and warm windows are clean (client fully booted pre-probe).
5. `bin/libnavi-96e71e5e…so` naming is **not source-keyed** — it is the `FLAGSSHA`
   hardlink target of the latest build; the earlier 12:59 pre-D1 binary under that name
   is gone (overwritten at 20:31). Payload provenance: pre-D1 engine per baseline report =
   `96e71e5e…` (no longer on disk), first-D1 deploy = `3a2eacc5…` (overwritten by the
   stem-guard deploy, not preserved), current = `c7ad475f…` (also kept as
   `~/.local/share/navi/libnavi.c7ad475f.bak` during the control-copy check).

## Verification set status (D1 close-out)

| Gate | Result |
|---|---|
| `./install.sh build` | ✅ (new engine built and deployed; `update -y` while steam down; hash parity verified) |
| `make test` | ✅ 94/94 unit + 91 passed / 1 skipped integration (pytest) + coverage |
| `make test-gate07` | ✅ all 7 checkpoints |
| Live profile | ✅ this report (warm 17.5 s vs 85.1 s baseline) |
| Response equivalence | ✅ 169/169 app set identical; 168 exact + 1 explained data-drift delta |
