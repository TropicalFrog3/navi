---
name: steam-native-hooking
description: Cheatsheet for Steam Linux client hooking, packet interception (CNetPacket), depot dependency manipulation, and debugging logs for Navi.
---

# Navi Native Hooking & Internal Debugging Guide

## 1. Diagnostic Log Routing & Level Gotchas
- **Navi Logs (`~/.navi.log`):**
  - Navi writes to `~/.navi.log` (NOT `console-linux.txt` or stdout).
  - In release builds (`-O3`), `LOG_DEBUG` and `LOG_TRACE` macros are compiled out as no-ops. **Use `LOG_INFO`** for any diagnostics that must appear in production/release runs.
- **Steam Content Log (`~/.local/share/Steam/logs/content_log.txt`):**
  - Tracks download engine state machine transitions: `Update Required` -> `Update Queued` -> `Update Running` -> `Staging` -> `Committing` -> `Fully Installed`.
  - Directly reveals download failure reasons (e.g., `(Missing decryption key)`, `0 mounted depots`, `Shader Priority (Suspended)`).
- **Steam App State (`~/.local/share/Steam/steamapps/appmanifest_<appid>.acf`):**
  - If this file exists, `steam://install/<appid>` navigates directly to the library without opening the install wizard. Cleanly uninstall (`steam://uninstall/<appid>`) or remove the file prior to testing install hooks.

## 2. Connection Manager Packet Interception (`CCMInterface::RecvPkt`)
All incoming protobuf messages from Valve's Connection Managers (CM) arrive as `CNetPacket*` in `CCMInterface::RecvPkt`:
- **Do NOT use legacy `CProtoBufMsgBase*`**: This struct was deprecated in the 2026 steamclient refactor.
- **Key EMsg Types:**
  - `k_EMsgClientGetDepotDecryptionKeyResponse` (`5439`)
  - `k_EMsgClientGetAppOwnershipTicketResponse` (`858`)
  - `k_EMsgClientPICSProductInfoResponse` (`5425`)

### Interception & Mutation Pattern
```cpp
void DepotKeys::recvMsg(CNetPacket* pkt)
{
    if (!pkt || !pkt->isValid() || !pkt->isProtoBuf()) return;

    if (pkt->getProtoBufType() == k_EMsgClientGetDepotDecryptionKeyResponse)
    {
        auto body = pkt->deserializeBody<CMsgClientGetDepotDecryptionKeyResponse>();
        const uint32_t depotId = body.depot_id();

        if (body.eresult() != k_EResultOK)
        {
            const std::string hexKey = getDepotKey(depotId);
            if (!hexKey.empty())
            {
                const std::string keyBytes = hexToBytes(hexKey);
                
                // IMPORTANT: Must update eresult on BOTH the header and the body
                CMsgProtoBufHeader hdr = pkt->deserializeHeader();
                hdr.set_eresult(static_cast<int32_t>(k_EResultOK));
                
                body.set_eresult(k_EResultOK);
                body.set_depot_encryption_key(keyBytes.data(), keyBytes.size());

                // Re-serializes allocating via Steam::Plat_Alloc and freeing the old body
                pkt->serialize(body, &hdr);
                LOG_INFO("DepotKeys: Injected cached key for depot %u\n", depotId);
            }
        }
    }
}
```

## 3. Depot Dependency Injection (`BuildDepotDependency`)
- **Root Problem:** For unowned apps (`AdditionalApps`), Steam's `CUserAppManager::BuildDepotDependency` returns `success = true` but with an empty depot list (`depots->size == 0`). Steam commits 0 files and mounts 0 depots.
- **Solution:** When a native install is active, inject missing user-selected depots into `depots`:
```cpp
static bool appendDepotInfo(CUtlVector<DepotInfo_t>* vec, const DepotInfo_t& info)
{
    // Refuse external stack buffers
    if (static_cast<int32_t>(vec->mem.growSize) < 0) return false;

    if (vec->size >= vec->mem.alloc)
    {
        const uint32_t newAlloc = vec->mem.alloc ? vec->mem.alloc * 2 : 4;
        const size_t bytes = static_cast<size_t>(newAlloc) * sizeof(DepotInfo_t);
        void* mem = vec->mem.base 
            ? Steam::Plat_Realloc(vec->mem.base, bytes) 
            : Steam::Plat_Alloc(bytes);
        if (!mem) return false;
        vec->mem.base = reinterpret_cast<DepotInfo_t*>(mem);
        vec->mem.alloc = newAlloc;
    }

    vec->mem.base[vec->size++] = info;
    return true;
}
```

### 3.1 The Dual Allocator Namespace Hazard (`g_pMemAllocSteam`)
- **The Architecture:** The Steam process loads TWO distinct copies of `libtier0_s.so`:
  - **Mapping A:** In the global namespace (`dlopen("libtier0_s.so")`, used by default in `libnavi.so`).
  - **Mapping B:** In `steamclient.so`'s private `dlmopen` namespace.
- **The Failure Mode:** When `BuildDepotDependency` returns, the caller inside `steamclient.so` frees the `CUtlVector` buffer using `g_pMemAllocSteam->Free()` from **Mapping B**. If `Navi` grew the buffer using `Plat_Realloc` from **Mapping A**, `Free()` operates on an unrecognized small-block pool, triggering `double free or corruption (out)` via glibc abort.
- **The Solution:** Call through `steamclient.so`'s own `IMemAlloc` instance:
  - 32-bit `IMemAlloc` vtable indices: `vtable[2] = Alloc(size)`, `vtable[3] = Realloc(ptr, size)`, `vtable[4] = Free(ptr)`.
  - All methods use standard 32-bit x86 `cdecl` calling convention (`(this, ...)`).

### 3.2 Dynamic ELF GOT Relocation Resolution
Hardcoding GOT offsets is fragile (e.g., Debian host is `0x02f0ee38`, while SteamOS Neptune is `0x02f4ac18`). Resolve the GOT slot dynamically at runtime:
1. Parse `PT_DYNAMIC` from `g_modSteamClient.base`.
2. Locate `DT_REL` and `DT_STRTAB`.
3. Search for relocation entry where `ELF32_R_TYPE(rel[i].r_info) == R_386_GLOB_DAT` and symbol name is `"g_pMemAllocSteam"`.
4. The relocation's `r_offset` provides the exact GOT slot address across any Steam client build.

### 3.3 Relocation Timing Caveat in `LD_AUDIT` (`la_objopen`)
When using `LD_AUDIT`, `la_objopen` fires immediately after a library is mapped, **before `ld.so` applies dynamic relocations**. At this moment, the GOT slot contains `0x0`.
- **Must use thread-safe lazy resolution:** Register the GOT slot address during initialization, but dereference `*gotSlot` lazily on first invocation of `Plat_Alloc`/`Plat_Realloc`/`Plat_Free`, after Steam has initialized and `ld.so` has resolved symbols.

### 3.4 Allocation & Buffer Safety Rules in `BuildDepotDependency`
1. **Never return collections by value:** `NativeInstall::getSelection()` must return `std::shared_ptr<const Selection>` or a const pointer. Constructing/destroying `std::unordered_map` and `std::unordered_set` instances on the stack inside the hook triggers stack destructor heap churn on a potentially corrupted heap.
2. **Cross-Platform Depot Guard:** Only inject missing depots when Steam returned `depots->size == 0` (unowned apps). When Steam already returns native depots for the running platform, do not inject Windows/macOS depots from Lua configs.

## 4. Thread Safety & Engine Frame Execution
- Never call Steam client virtual methods (`InstallApp`, etc.) directly from asynchronous background threads or CEF fetch handler threads; doing so causes segfaults.
- Queue requested actions into a thread-safe batch queue and dispatch them synchronously on the engine thread inside `CSteamEngine::ProcessIPCFrame` (`NativeInstall::runFrame`).

## 5. Dynamic Library Hot-Reload Architecture (Package 0 & License Dispatch)
- **Package 0 Memory Layout (`steamclient.so`):**
  - Managed by `CPackageInfoCache` via `GetPackageInfo(0, token)` (token `10660652434190618804ull`).
  - Struct layout for `PackageInfo`:
    - `+0x00`: `packageId` (`uint32_t`, 0 for free/base licenses)
    - `+0x18`: `status` (`uint32_t`, 3 = Available)
    - `+0x38`: `appIdVec` (`CUtlVector<AppId_t>`):
      - `+0x38`: `mem.base` (`AppId_t*`)
      - `+0x3C`: `mem.alloc` (`int32_t` capacity)
      - `+0x44`: `size` (`int32_t` element count)
    - `+0x48`: `depotIdVec` (`CUtlVector<AppId_t>`):
      - `+0x48`: `mem.base`
      - `+0x4C`: `mem.alloc`
      - `+0x54`: `size`
- **Native Memory Reallocation (`CUtlMemory::Grow`):**
  - Pattern: `55 89 E5 57 56 53 83 EC 1C 8B 75 08 8B 7D 0C` (`0x103cb0` / `0xbe694cb0`).
  - When expanding `appIdVec`, invoke `CUtlMemory::Grow` or `Steam::Plat_Realloc`.
- **License Queue Coordinator Functions:**
  - **`CUser::MarkLicenseAsChanged` (`0x186bdc0` / `0xc0be6dc0`):**
    - Pattern: `55 57 56 53 E8 ? ? ? ? 81 C3 ? ? ? ? 83 EC 2C 8B 74 24 40 8B 44 24 48 8B BE 14 1B 00 00`
    - Calling convention: 32-bit cdecl (`CUser* pUser, uint32_t packageId, bool bChanged`).
    - **CRITICAL WARNING:** Do NOT use `0x186cbc0`. `0x186cbc0` is merely an internal helper of `0x186d2f0` that updates an RB tree but **never** pushes to `pUser->m_vecPendingLicenses` (`pUser + 0x1b14`).
    - Invoking `0x186bdc0(pUser, 0, true)` pushes Package 0 into the pending list and increments `pUser + 0x1b14`.
  - **`CUser::ProcessPendingLicenseUpdates` (`0x186c800` / `0xc0be7800`):**
    - Pattern: `55 57 56 53 E8 ? ? ? ? 81 C3 ? ? ? ? 83 EC 2C 8B 44 24 40 8B 88 7C 1C 00 00`
    - Calling convention: 32-bit cdecl (`CUser* pUser`).
    - Checks `pUser + 0x1b14 > 0`. If pending licenses exist, it invokes `0x186c010`, which posts `k_iCallback_LicensesUpdated` (callback 125) via `CUser::PostCallback` (`0x10174a0`) and activates changes with `0x18a2f70`.

## 6. UI Ownership & Bidirectional CEF Synchronization
- **SteamUI AppOverview Flags (`hkCSteamUIAppController_FillInAppOverview`):**
  - Lives in `steamui.so`, hooked dynamically via `regparm(3)` (`eax=pThis`, `edx=pAppOverview`, `ecx=pApp`).
  - **Hot-Addition:**
    ```cpp
    pApp->OwnershipFlags |= k_EAppOwnershipFlags_OwnsLicense;
    if (pApp->PurchasedTime == 0) pApp->PurchasedTime = 1600000000;
    ```
  - **Hot-Removal:**
    ```cpp
    pApp->OwnershipFlags &= ~k_EAppOwnershipFlags_OwnsLicense;
    pApp->PurchasedTime = 0;
    ```
- **Live CEF Pruning via WebSocket CDP (`CDPInject::notifyAppRemoved`):**
  - Native protobuf updates for `RegisterForAppOverviewChanges` only push delta overviews. Steam CEF's React/MobX store does not drop removed entries from `window.appStore.m_mapApps`.
  - Navi connects to `SharedJSContext` on port 8080 and evaluates:
    ```js
    if (window.appStore && window.appStore.m_mapApps) {
        window.appStore.m_mapApps.delete(appId);
    }
    ```
  - `appStore.allApps` re-evaluates its MobX filter (`visible_in_game_list`), removing the app from the library view instantaneously.

## 7. Hot-Reload Code Map & Testing Playbook
- **Code Modification Map (Where to update what):**
  | Task | Source File | Exact Function / Symbol |
  | :--- | :--- | :--- |
  | **Package 0 Init & Vector Growth** | `src/presenter/licenses.cpp` | `Licenses::tryInitPackage0`, `Licenses::appendAppId` |
  | **License Dispatch Trigger** | `src/presenter/licenses.cpp` | `Licenses::drainPendingRefresh` |
  | **CEF Store Pruning via CDP** | `src/view/cdpinject.cpp` | `CDPInject::notifyAppRemoved` |
  | **Signatures & Addresses** | `src/core/patterns.cpp` | `g_pCUser_MarkLicenseAsChanged`, `g_pCUser_ProcessPendingLicenseUpdates` |
  | **Instance Pointer Capture** | `src/presenter/hooks.cpp` | `g_pCUser` in `RecvPkt`/`GetIClientUser`, `g_pPackageInfoCache` in `GetPackageInfo` |
  | **AppOverview Flags (steamui.so)** | `src/presenter/hooks.cpp` | `hkCSteamUIAppController_FillInAppOverview` |
  | **Memory Struct Layout** | `src/model/sdk/CUser.hpp` | `struct PackageInfo` (`+0x38 appIdVec`) |
  | **Plugin File Event Trigger** | `src/model/config.cpp` | `Config::onFileChange` |
- **Build & Install Workflow:**
  ```bash
  ./install.sh build && ./install.sh install
  ```
- **Automated Regression Testing (Mandatory before live runs):**
  ```bash
  ./install.sh test
  ```
- **Live Verification Protocol:**
  - Launch Steam with CDP debugging:
    ```bash
    DISPLAY=:1 ~/.local/share/navi/path/steam -cef-enable-debugging
    ```
  - Inspect `appStore.allApps` via Python:
    ```python
    import json, urllib.request, websocket
    req = urllib.request.Request("http://127.0.0.1:8080/json")
    with urllib.request.urlopen(req) as r:
        pages = json.loads(r.read().decode())
    for p in pages:
        if p.get("title") == "SharedJSContext":
            ws = websocket.create_connection(p["webSocketDebuggerUrl"], timeout=5)
            ws.send(json.dumps({
                "id": 1,
                "method": "Runtime.evaluate",
                "params": {
                    "expression": "(() => { const a = appStore.allApps.find(x => x.appid == 1086940); return { total: appStore.allApps.length, found: !!a }; })()",
                    "returnByValue": True
                }
            }))
            print(json.loads(ws.recv())["result"]["result"]["value"])
            ws.close()
    ```

## 8. New Steam Version Updates
For the complete 5-gate pipeline, RTTI VFT auditing, client hash verification, cloud job detours, and DOM injection resilience when updating to a new Steam build, see:
- [steam-version-update](../steam-version-update/SKILL.md)

## 9. CloudRedirect `steamclient.so` Vtable Detours & Coordination

### 9.1 `IClientRemoteStorage` Vtable Slots Detoured by CloudRedirect
CloudRedirect (`cloud_redirect.so`) intercepts file I/O within `CUserRemoteStorage` (`IClientRemoteStorage`) in `steamclient.so`:
- **Slot 5 (`FileWrite`)**: Intercepts file write operations to mirror or redirect save payloads into redirected directories or cloud provider staging before committing.
- **Slot 7 (`FileRead`)**: Intercepts file reads to serve save data transparently from redirected paths or cloud provider caches.
- **Slot 8 (`FileForget` / `IsCloudEnabledForApp`)**: Intercepts remote file deletion and queries to `CUserRemoteStorage::IsCloudEnabledForApp`, ensuring redirected games report proper local storage states.

### 9.2 Coordination with Navi Cloud Interception
Navi and CloudRedirect operate in tandem to prevent cloud conflicts and manage local storage:
1. **Navi Cloud Sync Job Suppression**:
   - `Apps::shouldDisableCloud(appId)` in `src/presenter/apps.cpp` identifies apps that should not sync to Valve's cloud (unowned, native installs, auto-crack, DLC-managed).
   - Navi detours `CClientJobRemoteStorageSync::BYieldingRunJob` (virtual method #18 in `27CClientJobRemoteStorageSync`) in `src/presenter/hooks.cpp` to return `true` immediately without network sync for intercepted apps.
   - Whitelists critical apps: `AppID 7` (`Roaming Config`) and `AppID 2371090` (`Steam Game Notes`), as well as legitimate user-owned purchases.
2. **CloudRedirect Local File I/O Redirection**:
   - While Navi suppresses Valve's remote sync jobs to eliminate "Out of Sync" or "Cloud Error" badges, `cloud_redirect.so` detours the low-level file I/O layer (`FileWrite`, `FileRead`) to redirect save data to structured local directories (`~/.local/share/navi/cloudredirect/storage/`) or remote cloud providers (Google Drive, OneDrive, Cloudflare R2, AWS S3, or Local-Only).

### 9.3 SLSsteam YAML Sentinel Shim Synchronization
Apps managed by CloudRedirect are synchronized non-destructively into `~/.config/SLSsteam/config.yaml`:
- **Sentinel Region**:
  ```yaml
  # >>> NAVI CLOUD SHIM (DO NOT EDIT MANUALLY)
  DisableCloud: no
  AdditionalApps:
    - 480
    - 730
  # <<< NAVI CLOUD SHIM
  ```
- **Set Union Merging ($S_{merged} = S_{user} \cup S_{navi}$)**:
  The managed app list is updated via single-key set-union:
  $$S_{merged} = S_{user} \cup S_{navi}$$
  Existing user entries in `AdditionalApps` outside the shim block are preserved intact.
- **Safe Set Subtraction on Removal ($S_{remaining} = S_{merged} \setminus S_{removed}$)**:
  When an app is removed from CloudRedirect management, Navi performs safe set subtraction:
  $$S_{remaining} = S_{merged} \setminus S_{removed}$$
  If no apps remain under Navi management, the sentinel block is cleaned up without corrupting user configuration.
