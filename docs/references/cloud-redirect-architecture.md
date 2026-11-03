# CloudRedirect Architecture Reference

Deep architectural layers of the Navi CloudRedirect integration. Start with the
[CloudRedirect integration guide](../cloud-saves.md) for the system overview, then read:

- **Storage, metadata, and cross-device sync** — this document
- **Cloud backends & configuration** — [cloud-redirect-backends.md](./cloud-redirect-backends.md)
- **Operations, pending ops, troubleshooting** — [cloud-redirect-runbook.md](./cloud-redirect-runbook.md)

---

## 1. Storage Layout & Metadata

Diverted game saves and sync states are stored hierarchically inside the user's Steam installation directory:

```
<SteamRoot>/cloud_redirect/
└── storage/
    └── <accountId>/
        └── <appId>/
            ├── cn.dat            # Cloud metadata and sync status
            ├── manifest.dat      # File manifest and checksum ledger
            └── <SaveFiles...>    # Diverted save games and configuration data
```

- **`cn.dat`**: Records sync state, revision timestamps, remote object IDs, and provider sync markers.
- **`manifest.dat`**: Contains cryptographic hashes (SHA-256) and directory trees for the game's save files to detect modified, deleted, or conflicted files.
- **Steam Userdata Mirror**: When redirected, `<SteamRoot>/userdata/<accountId>/<appId>/remote/` and `remotecache.vdf` are linked or synchronized into the redirected storage hierarchy.

### 1.1 Proton Prefix Emulator Save Synchronization (`PrefixBucketSync`)

Games running with Goldberg or GSE (`steam_api(64).dll` / `libsteam_api.so` replacements) handle `ISteamRemoteStorage` inside the emulated DLL rather than calling `steamclient.so`'s `CUserRemoteStorage`. Consequently, their save files are written inside the Proton prefix under:

```
<library>/steamapps/compatdata/<appId>/pfx/drive_c/users/<user>/AppData/{Roaming,Local,LocalLow}/<emulatorDir>/<appId>/{remote/,...}
```

`PrefixBucketSync` (`src/presenter/prefix_bucket_sync.cpp`) bridges these Proton prefix buckets with CloudRedirect's remote backend (`gdrive`, `onedrive`, `r2`, `s3`) automatically across devices without any hardcoded AppIDs, usernames, or emulator folder names:

- **Structural Discovery**: Scans all Steam library `compatdata/<appId>/pfx/drive_c/users/<user>/AppData/{Roaming,Local,LocalLow}` trees for `<appId>` save buckets (`GSE Saves/<appId>/remote`, `Goldberg SteamEmu Saves/<appId>/remote`, or custom `<dir>/<appId>`).
- **Pull on Startup & Game Launch (`sync-remote-app`)**: Before or upon launching a game, `PrefixBucketSync` invokes `cloud_redirect_cli sync-remote-app <provider> <accountId> <appId> <cloudRoot>`, materializes remote `<cleanPath>/<sha1>` blobs into the Proton prefix bucket (synthesizing the bucket directory on a fresh second device if needed), and backs up any conflicting local files under `~/.config/navi/cloud_backups/<appId>/prefix-conflict-<timestamp>/`.
- **Push on Game Exit & Startup (`publish-full-manifest`)**: When a game exits, `PrefixBucketSync` mirrors updated save files into `~/.config/CloudRedirect/storage/<accountId>/<appId>/<cleanPath>`, seeds `root_token.cloudredirect` and `file_tokens.cloudredirect`, increments `cn.cloudredirect`, and runs `cloud_redirect_cli publish-full-manifest <provider> <accountId> <appId> <cloudRoot>`.
- **Configuration**: see [cloud-redirect-backends.md → Configuration](./cloud-redirect-backends.md) (`CloudSaves.PrefixSync`).

---

## 2. Non-Destructive SLSsteam YAML Synchronization

Navi coordinates seamlessly with existing SLSsteam installations (`~/.config/SLSsteam/config.yaml`). To ensure user-defined configurations are never lost or corrupted, Navi uses a strict sentinel block and set union merge strategy.

### Sentinel Block

Navi inserts and updates its managed configurations strictly inside designated sentinel markers:

```yaml
# Existing user-defined SLSsteam settings
DisableFamilySharing: yes

# >>> NAVI CLOUD SHIM (DO NOT EDIT MANUALLY)
DisableCloud: no
AdditionalApps:
  - 480
  - 730
# <<< NAVI CLOUD SHIM
```

### Set Union Algorithm

1. **Read & Parse**: Navi parses `config.yaml`, extracting both user-defined `AdditionalApps` outside the block and any existing entries.
2. **Merge ($S_{merged} = S_{user} \cup S_{navi}$)**:
   The list of active apps is merged using set union semantics:
   $$S_{merged} = S_{user} \cup S_{navi}$$
   User-configured AppIDs are always preserved.
3. **Atomic Write**: The updated configuration is rendered and atomically written using `AtomicFile::write` (temporary file write followed by `rename`) to avoid partial writes or race conditions.
4. **Safe Removal / Revert**:
   When an app is removed from cloud management, Navi performs safe set subtraction:
   $$S_{remaining} = S_{merged} \setminus S_{removed}$$
   If no other keys remain and the list is empty, Navi cleans up gracefully without leaving orphaned files.

---

## 3. Security Guardrails

Navi implements strict security measures to protect game saves and the local filesystem:

1. **Origin Allowlisting**:
   The internal HTTP REST daemon accepts requests only from verified origins:
   - `https://steamloopback.host` (Steam CEF internal UI)
   - `http://127.0.0.1:9001` / `http://localhost:9001`
   Cross-origin requests from arbitrary browser tabs are rejected with HTTP 403 Forbidden.

2. **Constant-Time Authentication (`X-Navi-Auth`)**:
   State-altering endpoints (`POST /cloud/*`) require the `X-Navi-Auth` header. Tokens are validated via `CRYPTO_memcmp` constant-time comparison to prevent timing attacks. Full endpoint contract: [REST API reference](../api/rest.md).

3. **Typed Confirmation for Destructive Actions**:
   Destructive resets (`POST /cloud/apps/<appId>/reset`) require explicit JSON confirmation:
   ```json
   {
     "confirm": "RESET"
   }
   ```
   Any request without this exact string is rejected with HTTP 400 Bad Request.

4. **Automated Backups**:
   All manual backups and pre-reset snapshots are saved under:
   `~/.config/navi/cloud_backups/<appId>/`
   Manual backups use the naming convention `manual-<timestamp>.tar.gz`.
