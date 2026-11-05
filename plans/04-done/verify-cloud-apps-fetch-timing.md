Outcome: shipped — live profile of `GET /cloud/apps` (2026-11-03): **H2 confirmed** (~70.4s user-CPU of ~85s request from repeated per-candidate/per-app library scans; 204k read syscalls, 0 B disk), H1-style CLI spawns measured only ~22 spawns/~11–12s serial, H3/H4/H5/H6 falsified. Fix-ranking revision applied to `cloud-apps-inventory-speedup` (scan-once maps now #1). Evidence: `tests/reports/2026-11-03-timing-cloud-apps-inventory.md` + `test-results/timing-cloud-apps-inventory/`. — 2026-11-03

# Verify: View Steam Cloud inventory fetch — where the wall-clock goes

Stage: `01-backlog` (idea captured from analysis session, 2026-11-03)
Requires: real library on a live Steam Deck / client instance (device-level verification)

## Problem

Opening **View Steam Cloud** fires one blocking request
(`GET /cloud/apps?provider=<p>`, `src/presenter/storeinject.cpp:1047`) that composes the
whole inventory synchronously in `CCloudManager::ListCloudApps()` (`src/presenter/cloud_manager.cpp:2078`)
before any response byte. On real libraries this visibly stalls the page on
"Scanning cloud storage (<provider>)...".

## Why this item exists first

A candidate fix list exists (see sibling item
`cloud-apps-inventory-speedup.md`), but its ranking is inferred from code
structure, **not profiled**. Per the analysis, the leading suspicion (call it H1) is:

> **H1 (expected result):** most wall-clock is spent in serialized
> `fork+exec` of `cloud_redirect_cli list-remote-app-files` — one spawn + provider
> API round-trip per game not materialized in the local mirror, each with a 15s
> timeout (`src/presenter/cloud_manager.cpp:1667`, loop `2395-2410`), plus extra
> per-managed-app spawns from the route handler merge
> (`src/presenter/storeinject.cpp:1060-1072`).

Competing hypotheses to either rule out or confirm:

- **H2** depot/title resolution dominates: `GetParentAppIdForDepot` →
  `FindDepotOwnerInAppManifests` re-reads every `appmanifest_*.acf` per candidate
  (`cloud_manager.cpp:647, 1322`), `GetChildDepotIdsForApp` re-parses every
  `stplug-in/*.lua` per top-level app (`cloud_manager.cpp:1352`).
- **H3** Proton-prefix recursive walks dominate (`cloud_manager.cpp:1792-1850`).
- **H4** the discovery-phase CLI spawns dominate (`list-remote-app-ids` /
  `list-remote-apps`, `cloud_manager.cpp:2103-2108`, code-comment timing
  ~0.7s / ~4.7s per invocation).
- **H5** the one-time `appinfo.vdf` full read/parse (~100–300MB,
  `cloud_manager.cpp:1125-1194`) dominates on a cold session.
- **H6** the provider itself is slow (network/credential stalls), independent of
  our loop structure.

## Verification plan

All measurement from a live client session, non-invasive first (no `src/`
changes needed):

1. **Request-level baseline** (repeat twice, hot vs cold session):
   `curl -sS -w '%{time_total}\n' 'http://127.0.0.1:9001/cloud/apps?provider=<p>'`
2. **Subprocess census**: `strace -f -e trace=execve -qq -p <steam-client-pid>` while
   triggering one refresh; count `cloud_redirect_cli` spawns per request, and which
   subcommands (`list-remote-app-ids`, `list-remote-apps`, `list-remote-app-files`)
   fire how many times, serial vs parallel.
3. **Per-command cost**: manually time each subcommand on the same provider:
   `time cloud_redirect_cli list-remote-app-files <provider> <acct> <appId>`
   for a handful of representative apps; compare manual sum vs observed request time.
4. **FS-walk cost**: time synthetic
   `find compatdata/<appId>/pfx/drive_c/users/steamuser -type f | wc -l` for the same
   appIds, to bound the walk contribution vs CLI contribution.
5. Record library shape that explains the numbers: number of remote-discovered apps,
   number with a local mirror (`cn.cloudredirect`/`manifest.cloudredirect` markers),
   number of installed `appmanifest_*.acf`, number of `.lua` plugins.

## Exit condition

A timing report at
`tests/reports/<date>/2026-11-<D>-timing-cloud-apps-inventory.md`
(actual date of the run) that names the dominant cost with profile counts/evidence:

- **H1 confirmed** → promote `cloud-apps-inventory-speedup.md` to `02-specs/` as written.
- **Any other outcome** → revise that item's fix ranking before promoting.

## Pointers

- Analysis context: session analysis 2026-11-03 (View Steam Cloud slowness);
  chain `cloud_manager.js:869` → `storeinject.cpp:1047` → `cloud_manager.cpp:2078`.
- Read first: `src/CONTEXT.md` (Cloud-saves row), `tests/CONTEXT.md` (gates),
  `.agents/rules/test-regression.md` if instrumentation ends up in `src/`.
