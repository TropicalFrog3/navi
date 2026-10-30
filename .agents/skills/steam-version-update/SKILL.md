---
name: steam-version-update
description: End-to-end engineering playbook for updating Navi for new Steam client versions, including binary hash whitelisting, RTTI VFT auditing, GOT dynamic resolution, cloud interception detours, CEF DOM resilience, and 5-gate staged verification.
---

# Steam Version Update Playbook

## Overview
When Valve updates the Steam client (Stable or Beta), internal Linux binaries (`steamclient.so`, `steamui.so`), virtual method tables (VFT), internal jobs, and CEF WebViews may change. This skill provides the definitive, battle-tested playbook for updating, reverse-engineering, building, and certifying Navi against new Steam builds.

---

## 1. The 5-Gate Verification Pipeline
Never deploy directly to physical Steam Decks or tag releases without passing each gate sequentially:

```
[Gate 1: Client Hash & updates.yaml]
              │
              ▼
[Gate 2: Binary & VFT Shift Audit]
              │
              ▼
[Gate 3: Build & Automated Regression Suite]
              │
              ▼
[Gate 4: Live Local Hardware CEF Flow Test (15/15 Pass)]
              │
              ▼
[Gate 5: Physical Steam Deck / QEMU Deployment]
```

---

## 2. Gate 1: Client Hash Whitelisting (`updates.yaml`)

### Automated Audit Tool
Run the bundled audit script:
```bash
.agents/skills/steam-version-update/scripts/audit_client_hash.py
```
This script computes the SHA-256 of `steamclient.so`, extracts the version timestamp (e.g., `1791918634`), and verifies that `res/updates.yaml` preserves all existing hashes.

### Rules & Invariants
- **NEVER delete or decrease existing hashes**: `updates.yaml` must maintain backwards compatibility with both Desktop Steam and SteamOS Neptune (current minimum: >= 33 hashes). Both `./install.sh install` and `./install.sh release` enforce this invariant and abort if breached.
- Add the new client hash under the active release block in `res/updates.yaml`:
  ```yaml
  beta-v1.2.0:
    - <NEW_CLIENT_SHA256> # steamclient - <VERSION_NUMBER>
  ```

### CloudRedirect Prebuilt Binary Integrity Checks
In addition to `steamclient.so` whitelisting, verify the prebuilt CloudRedirect binaries:
```bash
make check-cloudredirect
```
This validation checks:
- Existence of `res/cloud_redirect.so` and `res/cloud_redirect_cli`.
- ELF 32-bit architecture (`readelf -h` verifying `ELF32` and `Intel 80386`).
- Exact SHA-256 checksums and file size match against tracked constants.
- File accessibility and executable permissions.

---

## 3. Gate 2: Virtual Function Table (VFT) Auditing

Steam interfaces (`IClientRemoteStorage`, `IClientApps`, `IClientUser`, `IClientAppManager`, `IClientCompat`) are abstract C++ classes whose virtual method order can change when Valve adds or removes APIs.

### Automated RTTI VFT Scanner
Even when binaries are stripped of symbol tables, dynamic relocations (`.rel.dyn`) retain RTTI typeinfo descriptors.
Run the bundled VFT audit script:
```bash
.agents/skills/steam-version-update/scripts/audit_vftable.py ~/.local/share/Steam/ubuntu12_32/steamclient.so CUserRemoteStorage
```

### Critical VFT Indices Map
Check and update indices in `src/core/vftableinfo.cpp`:

| Interface | Method | Known Index | Expected Behavior |
| :--- | :--- | :--- | :--- |
| **IClientRemoteStorage** | `FileWrite` | **5** | Detoured by CloudRedirect to mirror/redirect writes |
| **IClientRemoteStorage** | `FileRead` | **7** | Detoured by CloudRedirect to serve redirected reads |
| **IClientRemoteStorage** | `FileForget` / `IsCloudEnabledForApp` | **8** | Detoured by CloudRedirect for app cloud status |
| **IClientRemoteStorage** | `InternalGetSyncState` | **74** | Return `k_ERemoteStorageSyncStateDisabled` (0) for cloud-disabled games |
| **IClientRemoteStorage** | `GetConflictingFileTimestamps`| **75** | Set `*pbIsConflict = false` and return `true` |
| **IClientRemoteStorage** | `GetPendingRemoteOperationInfo` | **76** | Zero out operation info and return `false` |
| **IClientRemoteStorage** | `ResetFileRequestState` | **82** | Clear state and return `true` |
| **IClientAppManager** | `GetAppInstallState` | Dynamic | Track state transitions |
| **IClientCompat** | `GetValidToolList` | Dynamic | Enforce compatibility tool lists |

---

## 4. Gate 3: Memory Patterns, Job Detours & GOT Resolution

### 4.1 Multi-Layer Cloud Sync Interception
To eliminate "Out of Sync" or "Cloud Error" badges across all games without breaking internal Steam services:
1. **App Classification (`src/presenter/apps.cpp`)**:
   `Apps::shouldDisableCloud(appId)` must check:
   ```cpp
   if (g_config.isAddedAppId(appId) ||
       isInstalled(appId) ||
       isOnlineFixInstalled(appId) ||
       isAutoCrackInstalled(appId) ||
       NativeInstall::isNative(appId) ||
       DepotKeys::isKeyManagedApp(appId))
   ```
2. **Background Sync Job Detour (`src/presenter/hooks.cpp`)**:
   Steam runs background sync jobs via `CClientJobRemoteStorageSync::BYieldingRunJob` (virtual method #18 in `27CClientJobRemoteStorageSync`).
   - Hook this function detour.
   - If `Apps::shouldDisableCloud(appId)` is true: Log suppression via `LOG_INFO` and return `true`.
   - Returning `true` signals clean job completion to Steam's internal scheduler, preventing retry loops and error badges.
3. **Whitelist Preservation**:
   - `AppID 7` (`Roaming Config`): Must bypass interception to allow Steam settings sync.
   - `AppID 2371090` (`Steam Game Notes`): Must bypass interception to avoid breaking the Steam notes editor.
   - Legitimately purchased user-owned games: Must bypass interception.

### 4.2 Dynamic ELF GOT Relocation (No Hardcoded Addresses)
Steam uses two distinct `libtier0_s.so` memory namespaces. Hardcoding GOT offsets for `g_pMemAllocSteam` causes heap corruption crashes.
- Dynamically parse `PT_DYNAMIC` -> `DT_REL` / `DT_STRTAB` -> locate `R_386_GLOB_DAT` for `"g_pMemAllocSteam"`.
- Under `LD_AUDIT` (`la_objopen`), `ld.so` relocations are applied lazily after library mapping. Always dereference GOT slots lazily on first call.

### 4.3 Steam User Account ID Arithmetic & Save Path Resolution
When resolving save paths for CloudRedirect or the idle helper (`bin/navi-cloud-apply-pending`):
- **User Discovery via `loginusers.vdf`**:
  Steam tracks configured user accounts in `~/.local/share/Steam/config/loginusers.vdf` identified by their 64-bit SteamID (`7656119xxxxxxxxxx`).
- **Account ID Conversion Arithmetic**:
  Convert 64-bit SteamID to 32-bit AccountID:
  $$\text{AccountID} = \text{SteamID} \ \& \ \text{0xFFFFFFFF}$$
  $$\text{or } \text{AccountID} = \text{SteamID} - 76561197960265728$$
- **Save Path Hierarchy**:
  Cloud save files reside within the Steam userdata hierarchy:
  `~/.local/share/Steam/userdata/<account_id>/<app_id>/`
- **Idle Helper Targeting**:
  `bin/navi-cloud-apply-pending` uses the 32-bit AccountID to target save reset, backup, and restore paths across `userdata/<account_id>/<app_id>/remote/` and `remotecache.vdf`.

---

## 5. Gate 4: CEF DOM Injection & Store Page Resilience

Steam's Chromium Embedded Framework (CEF) store pages and Big Picture views render asynchronously.

### 5.1 DOM Timing Protection
In `res/inject-scripts/navi_inject.js` and `res/inject-scripts/store-page-script.js`:
- Never call `observer.observe(document.body)` directly at script top-level. CEF can execute scripts before `document.body` is created, throwing a fatal `TypeError`.
- Always guard with:
  ```javascript
  var targetNode = document.body || document.documentElement;
  if (targetNode) {
      observer.observe(targetNode, { childList: true, subtree: true });
  } else {
      document.addEventListener('DOMContentLoaded', function() {
          var el = document.body || document.documentElement;
          if (el) observer.observe(el, { childList: true, subtree: true });
      });
  }
  ```

### 5.2 Decoupled Layout Detection
- Do **not** use broad negative class exclusions (e.g., `if (document.querySelector('.gamepadui')) doNotMountDesktop()`). If Big Picture elements are partially present or loading, this suppresses both buttons.
- Check actual container connectivity:
  ```javascript
  var bpBtnContainer = curProductId ? document.getElementById('sls-bp-lua-btn-container-' + curProductId) : null;
  if (bpBtnContainer && bpBtnContainer.isConnected) {
      // Remove any duplicate desktop button
  }
  ```
- Use a tiered fallback insertion chain for Desktop:
  1. `header.insertBefore(luaBtn, insertionPoint)`
  2. `cartBtn.parentNode.insertBefore(luaBtn, cartBtn)`
  3. `purchaseArea.prepend(luaBtn)`

---

## 6. Gate 5: Staged Testing & Verification Protocol

### Step 1: Build & Package
```bash
./install.sh build
./install.sh release
./install.sh install
```

### Step 2: Automated Test Suite
```bash
./install.sh test
```
*(or `make test`)*
Ensure all unit tests (`bin/unit_tests`), integration tests (`tests/integration`), and coverage checks pass.

### Step 3: Live Local Hardware CEF Flow Test
Launch Steam with debugging:
```bash
DISPLAY=:1 ~/.local/bin/steam -cef-enable-debugging
```
Run the automated flow test suite:
```bash
make test-flow
```
*(or `pytest -q --live tests/integration/test_steam_ui_flow.py`)*

**Verification Checklist:**
- [x] Test 3: "Download Lua" / "Remove Lua" button mounts on store page.
- [x] Test 4: Download modal opens with 0% progress bars.
- [x] Test 5: Auto Download installs `.lua` file and reports 100%.
- [x] Test 9: Native hook telemetry reports zero memory corruptions.
- [x] Test 14: Process remains healthy with zero minidumps in `/tmp/dumps/`.

### Step 4: Log Diagnostics Audit
1. `~/.navi.log`: Verify `Disabled cloud for <appid> (InternalGetSyncState -> Disabled)`.
2. `~/.local/share/Steam/logs/cloud_log.txt`: Verify 0 sync attempts for unlocked apps; verify Roaming Config & Notes sync normally.
3. `/tmp/dumps/`: Confirm no crash dumps were generated.

---

## 7. Cross-References
- [steam-native-hooking](../steam-native-hooking/SKILL.md): Details on `CNetPacket` serialization, `CCMInterface::RecvPkt`, and Package 0 license dispatch.
- [release-workflow](../release-workflow/SKILL.md): Procedures for version tagging, packaging, and publishing.
- [steamdeck-wireless-workflow](../steamdeck-wireless-workflow/SKILL.md): Wi-Fi LAN deployment to physical Steam Deck after Gate 4 completion.
