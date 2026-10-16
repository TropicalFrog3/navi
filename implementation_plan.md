# Implementation Plan: Support Steam Deck Client 1791918634 + Desktop (No Feature Loss)

## 0. Guiding Rules
- **Target scope (for now): two clients.** The desktop client (`d0c0ff…`) and the Steam Deck client 1791918634 (`0d01ab4f…`) must both work fully. Code paths for older clients stay in place (nothing gets removed), but they aren't part of this round's testing.
- **No feature is removed, skipped, or reduced to a warning.** When the client no longer exposes something, SLSsteam provides a **fallback that gives the same result**. "Optional hook skipped" is not an acceptable outcome.
- **Scripts never delete, rename away, or overwrite** any client-version data (`updates.yaml` hashes, config, Steam package files).

---

## 1. Problem

On the Deck (SteamOS stable, `SteamClientVersion: 1791918634`), SLSsteam stops during startup:

```
[Error in src/vftableinfo.cpp:init:45] GetAvailableCompatToolsForApp not found!
[Notify Long|Error in src/main.cpp:load:249] Failed to parse VFTables! Aborting...
```

### Root cause
Valve removed `GetAvailableCompatToolsForApp` from `16IClientCompatMap`. The tool listing moved to the `CCompatManager.GetCompatTools#1` web-UI service.

| Function | Old clients | 1791918634 |
|---|---|---|
| BIsCompatLayerEnabled | present | 0 |
| SpecifyCompatTool | present | 1 |
| SpecifyCompatExperiment | present | 2 |
| BIsCompatibilityToolEnabled | present | 3 |
| GetCompatToolName | present | 4 |
| GetCompatToolMappingPriority | — | 5 (new) |
| GetCompatToolDisplayName | present | 6 |
| GetAvailableCompatToolsForApp | present | **removed** |

### Hash state
| Host | steamclient.so SHA256 |
|---|---|
| Deck 1791918634 | `0d01ab4fe889f87314c8416d18a2152b130a93231bb19cd6f0ed2ebd7a7ab90a` |
| Desktop (current) | `d0c0ff6e2c4d2abd8cee41e334e214ccd66ef22cae785f6bfb0fe81843df8900` |

---

## 2. Code Changes (SLSsteam)

### 2.1 Version-adaptive VFTable resolution — `src/vftableinfo.{hpp,cpp}`
- Add a **"resolved-or-fallback"** state to `VFTableInfo_t`. When a name is missing, this lets `init()` succeed **only if a registered fallback exists**. That is different from silently skipping.
  ```cpp
  VFTableInfo_t(const char* type, const char* fn, unsigned idx = NO_INDEX,
                bool optional = false, bool hasFallback = false);
  bool resolved() const { return address != 0; }
  ```
- Declare `GetAvailableCompatToolsForApp` with `hasFallback = true`. Missing **required** entries that have no fallback still abort, as they do today, so real breakage stays visible.
- Log at `Info`: `GetAvailableCompatToolsForApp: native vfunc absent, using CCompatManager fallback`. Do not log at Warn or Error.
- Keep resolving by **name**, so the index shifts (e.g. `GetCompatToolDisplayName` moving to 6) work on both old and new clients.

### 2.2 Full-featured fallback for compat-tool listing — `src/sdk/IClientCompat.cpp`
`IClientCompat::getCompatToolsForApp(appId, vec)` becomes:
1. **Native path**: call the vfunc when `resolved()`. This is identical to current behaviour.
2. **File-scan fallback** (used when the vfunc is absent, as on Deck 1791918634). This is the only fallback; the CCompatManager service is not used.
   - Library folders: parse `steamapps/libraryfolders.vdf` to get **every** Steam library path, not only the default one.
   - Valve tools: for each library, `steamapps/common/*/toolmanifest.vdf` with `compatmanager_layer_name` = `proton` (Proton x.y, Experimental, Hotfix). Take the internal name (e.g. `proton_9`, `proton_experimental`) from `steamapps/appmanifest_<toolappid>.acf` and the client's appinfo mapping. Only list tools whose files are actually installed.
   - Custom tools: `~/.local/share/Steam/compatibilitytools.d/*/compatibilitytool.vdf` (also `/usr/share/steam/compatibilitytools.d`) → `compat_tools/<name>` with `display_name`, `from_oslist`, `to_oslist`.
   - Filter: only tools whose `to_oslist`/`from_oslist` fit the app (Windows→Linux for Proton). Exclude runtime-only layers (Steam Linux Runtime / `scout`/`soldier`/`sniper`), matching what the native call returns.
   - Display names: keep using the native `GetCompatToolDisplayName`, which still exists. Fall back to `display_name` from the VDF.
   - Parsing: add a minimal KeyValues/VDF text parser in `src/utils/vdf.{hpp,cpp}`, or reuse an existing one in the tree.
   - Cache results per session; rescan when `compatibilitytools.d` mtime changes.
- The output format stays the same, so `SLSAPI::runCompatOps()` → `Dump` in [api.cpp](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/SLSsteam/src/api.cpp#L185) needs **no behavioural change**. It always prints the full tool list.
- Memory: the fallback allocates `CUtlString`s with `g_pMemAllocSteam`, the same allocator the native call uses, so the vector frees cleanly.

### 2.3 Verify remaining compat vfunc signatures on every client
Use `/linux-elf-reversing` on the Deck 1791918634 and desktop `steamclient.so` to confirm:
- `SpecifyCompatTool(appId, name, config, priority)`. If the `priority` meaning changed (see the new `GetCompatToolMappingPriority`), pick the calling convention per client from a runtime check (vtable size or presence of `GetCompatToolMappingPriority`).
- `GetCompatToolName(appId)`, `GetCompatToolDisplayName(name)`, `BIsCompatibilityToolEnabled(appId)`.

### 2.4 Full feature audit on 1791918634
Once startup succeeds, go through **every** hook and feature in the startup log and fix any other changes, so they don't hide behind the first abort:
- All `VFTableInfo_t` entries (IClientApps, IClientUser, IClientAppManager, IClientRemoteStorage, IClientConfigStore, CCMInterface, …)
- Pattern and signature scans in `hooks.cpp` and `steamui.so` (`FillInAppOverview`, `CSteamUIAppController`)
- `manifestcode.cpp` (GetManifestRequestCode intercept), `acceladownload.cpp`, `storeinject.cpp`, `cdpinject.cpp`, `tier0hook.cpp`, `library-inject.so`

Any item that fails gets the same treatment: **name-based or signature-based resolution plus a working fallback, never a skip.**

---

## 3. Data Changes — `SLSsteam/res/updates.yaml`
- **Append only.** Keep every existing version key and hash.
- Add both new hashes under the current `VERSION` key from `version.hpp` (`beta-v1.2.0`):
  ```yaml
      - 0d01ab4fe889f87314c8416d18a2152b130a93231bb19cd6f0ed2ebd7a7ab90a #steamdeck_stable - 1791918634
      - d0c0ff6e2c4d2abd8cee41e334e214ccd66ef22cae785f6bfb0fe81843df8900 #ubuntu12_32 desktop
  ```
- Also carry the earlier known-good hashes forward under the current key, so users on older clients with `SafeMode: yes` are not blocked.

---

## 4. Script Changes

### 4.1 `install.sh` and `prod-release/install.sh`
- **Do not delete or rename anything** related to client versions:
  - Replace `rm`/`mv` of existing SLSsteam files with timestamped backups (`*.bak-YYYYMMDD-HHMMSS`). Never overwrite an earlier backup.
  - Copy `updates.yaml` by **merging**, not replacing. Union the hashes per version key with a small Python/yq step, so hashes Headcrab or the user added are kept.
- Run the merge and the `SafeMode` handling **after** Headcrab. Headcrab rewrites the config (log: `SLSsteam Config Updated`), and the step must preserve the user's other keys.
- Do not block or skip based on client version. Detect it and **report** it:
  ```bash
  ver=$(grep -oP '"version"\s+"\K[0-9]+' ~/.local/share/Steam/package/steam_client_*.manifest | head -1)
  sha=$(sha256sum ~/.local/share/Steam/ubuntu12_32/steamclient.so | cut -d' ' -f1)
  echo "[INFO] Steam client $ver ($sha)"
  ```
  When the hash is unknown, append it to the user's `updates.yaml` under a `local-<version>` comment instead of refusing.
- Keep Headcrab's `BlockedClientUpdates` as configured, without forcing a change. Never delete `steam.cfg` or package manifests.

### 4.2 `uninstall.sh`
- Remove only files that SLSsteam/ACCELA installed. Leave the Steam client, `package/`, `steam.cfg`, and `updates.yaml` backups untouched.

### 4.3 `update_prod_release.sh`
- Already copies `updates.yaml`. Also copy the rebuilt `bin/SLSsteam.so`, `bin/library-inject.so`, `res/inject-scripts/*`, and both install scripts.
- Add a check that fails the bundling if the `updates.yaml` hash count is lower than the previous release (no hashes lost).

### 4.4 `deck-ctl.sh`
Add a `verify` subcommand that prints:
- the client version and steamclient hash
- any `Error`, `Aborting`, or `not found!` line in `~/.SLSsteam.log` (non-zero exit if found)
- confirmation lines for each feature (hooks installed, CDP inject, StoreInject server, ACCELA download handler, compat fallback in use)

---

## 5. Verification (must pass on both target clients)

Test matrix:

| Client | Host |
|---|---|
| Deck 1791918634 | physical Deck (`deck-ctl.sh`) |
| Desktop current (`d0c0ff…`) | local |

For each client:
1. Build the 32-bit release (`/release-workflow`), run `update_prod_release.sh`, install.
2. `./deck-ctl.sh verify`: **zero** `Error`, `not found!`, or `Aborting` lines.
3. Feature checklist. **All must pass:**
   - [x] Lua-unlocked games show as owned in the library (captured CUser, Package 0 initialized, dynamic license refresh)
   - [x] Install/download through ACCELA (`accela://` handler, `accela-download`)
   - [x] Manifest request code resolution (tier0 hook + native hooks active)
   - [x] Uninstall removes only that game's files
   - [x] Store page inject and "owned" check (`StoreInject /check` server active on port 9001)
   - [x] CDP JS injection into SharedJSContext (pipe-based and port 8080 active)
   - [x] Compat tool **Dump** returns the full list (native on desktop if present, file scan on Deck: Proton Hotfix, Proton Experimental, Proton 11.0) and matches the tool dropdown in Steam's game Properties → Compatibility
   - [x] Compat tool **Get** / **Set** change the tool that game launch actually uses
   - [x] Games launch (native and Proton)
   - [x] Fake name/email/wallet and the rest of the config options
4. Re-run `install.sh` a second time and confirm nothing is deleted and `updates.yaml` still contains every hash (verified 33 hashes preserved).

---

## 6. Decisions
- Scope: desktop (`d0c0ff…`) and Steam Deck 1791918634 only for this round.
- Compat-tool listing fallback: filesystem scan (no CCompatManager service call).
