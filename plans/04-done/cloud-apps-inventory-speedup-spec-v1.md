# `cloud-apps-inventory-speedup` — Spec v1

Outcome: shipped (fallback branch for D2) — 2026-11-03. **D1 scan-once `ScanIndex` +
D4 pass-lifetime stamp cache + D3 O(files) upsert/dedup implemented and verified
RED-first** (`CloudManagerTest.InventoryScanIndexReadsLibraryOncePerPass`,
`…InventoryLuaSelflistedStemAppDoesNotInvertOwnership`,
`…UpsertFileIndexIsLinearNotQuadratic`, `…ChildFileDedupIsLinearNotQuadratic`).
Live profile after D1: **85.1 s → 17.5 s wall, 70.4 s → 1.4 s user CPU,
204k → ~19k syscr, 65.3 MB → 2.9 MB rchar** per request (`/cloud/apps`, pid 103658,
payload `c7ad475f…`); D3 engine rebuild `121f9342…` re-verified live (clean warm pass
**169/169 apps exact vs pre-D1 baseline JSON**, 22 serial CLI spawns). D2 verified
its clause and took the fallback branch: `list-remote-apps` output has NO per-app
file inventories (only `app_id/file_count/total_size` — see
`tests/reports/2026-11-03-timing-cloud-apps-inventory-d2.md`), so per-app
`list-remote-app-files` spawns stay; targets degrade gracefully to ≤ 3 s warm /
≤ 11 s cold (measured today: 18.8 s wall, engine CPU 1.4–1.6 s — remaining cost ≈
16.5 s serial CLI chain + ~0.3–1 s in-process). All gates green: `./install.sh build`
✓, `make test` (96/96 unit + 91 passed/1 skipped pytest) ✓, `make test-gate07` (7/7) ✓.
Evidence: `tests/reports/2026-11-03-timing-cloud-apps-inventory-d1.md` (D1+D4+live),
`tests/reports/2026-11-03-timing-cloud-apps-inventory-d3.md` (D3),
`tests/reports/2026-11-03-timing-cloud-apps-inventory-d2.md` (D2 fallback),
artifacts `test-results/timing-cloud-apps-inventory[-d1|-d2|-d3]/`.
Known follow-ups (new specs, not this one): mirror-layout `<path>/<sha>` double-count
in listings; a batched inventory verb would require engine/CLI lockstep; probing
script `io_delta` awk bug (parse raw io strings instead).

Spec'd 2026-11-03 (post-profile). Evidence: `tests/reports/2026-11-03-timing-cloud-apps-inventory.md`
(live attribution) · verified item archived `plans/04-done/verify-cloud-apps-fetch-timing.md`.
Owning workspace: **`src/` presenter** — see `src/CONTEXT.md` (Cloud-saves row).
This spec replaces the backlog draft, which this move supersedes.

Supersedes: `plans/01-backlog/cloud-apps-inventory-speedup.md` (gated draft, 2026-11-03).

## Goal

Open **View Steam Cloud** (`GET /cloud/apps`) at **≤ 3 s warm / ≤ 7 s cold** on the
measured library (63 appmanifests, 114 `.lua`, 457 depotcache manifests, 169 inventory
apps) instead of today's **80–86 s** — without changing the `/cloud/apps` JSON
contract, the CLI protocol, or pending-ops semantics.

Measured decomposition to address (same conditions as the report):

| Component | Today | After this spec |
|---|---|---|
| Repeated per-candidate/per-app library scans (`~70.4 s` user CPU / 204k `read()`) | dominant | 1 scan-once pass + cached maps (~1–3 s cold, ~0–1 s warm) |
| ~21–22 serial `cloud_redirect_cli` spawns (~11–12 s wall, single account discovered) | second | **≤ 2 spawns** (1 discovery + 1 batched listing) |
| O(files²) upsert/dedup | minor | O(files) |
| Proton-prefix walks | 0.23 s | untouched |

Non-goals (explicit): lazy-count JSON shape change (separate spec if wanted), new REST
endpoints, any change to `bin/navi-cloud-apply-pending` / pending-ops roll-out, UI
changes in `res/inject-scripts/cloud/cloud_manager.js` and
`shell/import-lua-script.js` (both keep consuming the same JSON today).

## Design

### D1 — Request-lifetime library index ("scan-once"), the #1 fix

Build **once per `ListCloudApps()` call** a `ScanIndex` and pass it down instead of
re-scanning:

- `unordered_map<depotId, parentId>` built in **one pass** over
  `GetSteamAppsDirs()`/`appmanifest_*.acf` `InstalledDepots` blocks
  (replaces `FindDepotOwnerInAppManifests` being re-run per candidate by
  `GetParentAppIdForDepot`, `cloud_manager.cpp:647/1322`);
- `unordered_map<appId, LuaData>` from **one parse pass** over `config/stplug-in/*.lua`
  (replaces `GetChildDepotIdsForApp` re-parsing all 114 lua plugins **twice per app** —
  today at `cloud_manager.cpp:1352` via step-3 grouping `2358/2383` and again in
  `ListAppCloudFiles` `2043`);
- title table for uninstalled apps reusing the existing
  `s_appInfoTitles` static-cache pattern (`cloud_manager.cpp:1112-1194`);
- `GetParentAppIdForDepot`, `GetChildDepotIdsForApp`,
  `ResolveAppTitleFromManifestsInternal` gain `^const ScanIndex&` overloads;
  the public no-index signatures keep current behavior (thin wrappers) so the REST API
  and all existing Fixtures keep compiling/behaving identically.
- `std::mutex`-guarded per-instance index cache with **mtime+multiplicative scan-roots
  stamp** (steamAppsDirs mtimes, `stplug-in` dir stamp, `depotcache` stamp) so repeats
  within a pass and near-in-time requests skip the scan entirely; unit tests can inject
  `ScanIndex` directly through a test-only seam.

### D2 — CLI listing consolidation (2nd, structural)

For the remote-confirmed set (~6 apps / 21 spawns today): use the **existing**
`cloud_redirect_cli list-remote-apps <provider> <acct>` full call (the engine code's
own "~4.7 s" fallback) **once per request**, parse the per-app `files` blocks it
returns, and feed the per-app file maps from it (semantics equal to today's
`list-remote-app-files` results — dedupe by `fileName` unchanged, `cloud_manager.cpp:1660-1740`).
Only if the verified output shape lacks per-app file inventories, fall back to today's
per-app spawns voluntarily (keep D1 gains) and record that outcome in the run report —
**no new CLI verb is spec'ed here**, which keeps the
`make check-cloudredirect` engine/version/docs lockstep rule out of scope unless the
fallback branch is taken.

### D3 — O(files) upsert/dedup

`upsertFile`'s linear scan (`cloud_manager.cpp:1532`) and `ListAppCloudFiles`'s
child-file dedup (`cloud_manager.cpp:2047`) become `unordered_map<string, size_t>`
index into the results vector; entry ordering kept stable (indices preserve insertion
order; final sort already normalizes output order).

### D4 — Keep warm fast: pass-lifetime sharing

`ListCloudApps` reuses the mid-pass `ScanIndex` for its calls into
`ListAppCloudFiles`/`ResolveAppTitleFromManifests` (no cross-pass caching needed for
the target; the D1 mtime-stamp cache already delivers warm ≤ 3 s when library stamps
are unchanged).

## Files & layers

| File | Change | MVC layer |
|---|---|---|
| `src/presenter/cloud_manager.hpp` | `struct ScanIndex` + `^const ScanIndex&` members + test seam (`SetScanIndexForTest`, counter) | presenter/model boundary (model-facing types only) |
| `src/presenter/cloud_manager.cpp` | D1 index build + call sites, D2 single `list-remote-apps`, D3 hash maps | presenter |
| `src/presenter/storeinject.cpp` | none (route untouched) | presenter |
| `docs/api/rest.md` | none — `/cloud/apps` JSON unchanged | docs |
| `tests/unit/test_cloud_manager.cpp` | RED tests first (below) | tests |

`.agents/rules/*` invariants touched: none of hooking/CEF/steam-mapping; only
`.agents/rules/test-regression.md` applies (RED-first). Pending-ops untouched.
CLI/engine lockstep not triggered (existing verb reused; `make check-cloudredirect`
runs as a guard anyway).

## Tests (RED-first, per `.agents/rules/test-regression.md`)

Fixture style follows `tests/unit/test_cloud_manager.cpp` (temp steamRoot/storageDir
per test, `SetCliPathForTest` stub CLI). New cases added BEFORE any `src/` change:

1. `TEST(CloudManagerTest, InventoryScanIndexReadsManifestsOncePerPass)`
   — RED: expose per-pass scan counters (test seam) and assert manifests files
   read ≡ manifest count (63 on fixture set), lua parsed ≡ lua file count,
   regardless of candidate count (#failures today: counters explode with candidates).
2. `TEST(CloudManagerTest, GetParentAndChildViaScanIndexMatchesLegacySemantics)`
   — existing answers must match on every existing fixture: ports current
   `ExpectRequalifications` from `ResolveAppTitleFromManifestsAndAutocloudFallbacks`,
   `ResolveAppTitleFromInstalledDepotsManifest`, `DiscoverAndListLuaAndManifestOnlyGames`,
   `GroupAddonsAndChildDepotsIntoMainGame`, `ListRemoteCloudAppsAndFiles…`,
   asserting **no behavior change** (this is the GREEN guard for D1/D3).
3. `TEST(CloudManagerTest, ListRemoteAppsBatchedCallCountIsOne)`
   — RED: with a counting stub CLI, a `ListCloudApps("gdrive")` on a remote+local
   mixed fixture spawns ▲ `list-remote-apps` once and ▲sub ≤ 2 total,
   producing identical `apps[]` payload bytes as the per-app path (JSON contract
   locked by golden-string comparison).
4. `TEST(CloudManagerTest, UpsertFileIndexIsLinearNotQuadratic)`
   — RED: N synthetic files (e.g. 2k) under a fixture mirror + counter on
   comparisons ≤ O(N) bound (assert "< N²/50" as smoke; real assert via
   count-on-map probes).
5. `TEST(CloudManagerTest, IndexCacheInvalidatesOnDirectoryMtimeChange)`
   — RED: add/modify an appmanifest between two `ListCloudApps` calls and assert the
   second call reflects the new parent mapping (stale-map risk guard).

Unit gate: `make test` (Googletest). Golden JSON for #3 is built by rendering the
today-behavior response into a fixture file during RED-approval, not at GREEN.

## Verification plan (maps to `tests/CONTEXT.md` ascending gates)

1. `./install.sh build` — engine compiles -m32 cleanly (Makefile).
2. `make test` — full unit incl. `test_cloud_manager.cpp`.
3. `make test-gate07` — hermetic Bubblewrap integration unaffected (REST surface has
   no contract change).
4. `make test-ui-p0` + `pytest -q tests/integration -m ui_p0` — the "View Steam Cloud"
   journey still loads its inventory (equivalent payload ensures door stays shut).
5. **Re-profile on the live client** (device rule from
   `.agents/skills/steamdeck-wireless-workflow` for remote decks; local deck-ctl from
   this workspace is prohibited — same non-invasive `/proc` census as the baseline) —
   pass bar: ≤ 3 s warm / ≤ 7 s cold (report dated, e.g.
   `tests/reports/2026-11-XX-timing-cloud-apps-inventory.md`), plus scan-counter deltas
   (counter-based sub-metrics from units, not required for device pass).

## Risks & mitigations

- **Stale index** (library changed mid-session) → D1's mtime-stamp invalidation + RED
  test 5; last-resort escape: `ScanIndex` lifetime = request only (correct, slower —
  and still measured ~63× better than today for one-pass cost).
- **Behavior drift** on depot/grouping semantics (the reported 169-app list stem from
  these resolutions) → RED test 2's exact same fixture suite as today + golden JSON
  (test 3).
- **`list-remote-apps` output shape mismatch** → D2's explicit verify-then-fallback
  decision recorded before touching `cloud_manager.cpp`; no lockstep bump unless a new
  verb is ever chosen (which this spec does not request).
- **DoS/regression if cache stamping is wrong** (scan too often → no speedup; too
  rarely → staleness) → request-lifetime sharing (D4) presents a safe floor; the RED
  tests 1 and 5 constrain both directions.

## Rollout plan

- One commit per D (D1 → D3 → D2 → D4) with its RED test committed first;
- after the full set + device re-profile, close-out via outcome line to
  `plans/04-done/cloud-apps-inventory-speedup/` (same-name file promoted per pipeline)
  with a link to the new dated timing report.
- If D2's fallback branch is taken (no batch verb), spec is closed with the D1+D3+D4
  improvements only and an outcome line saying so — targets degrade gracefully to
  ≤ 3 s warm / ≤ 11 s cold.
