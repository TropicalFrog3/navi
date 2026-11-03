# CloudRedirect Cloud Backends & Configuration

Provider catalog and every configuration knob for the Navi CloudRedirect integration.
For storage internals see [cloud-redirect-architecture.md](./cloud-redirect-architecture.md),
for pending-ops/troubleshooting see [cloud-redirect-runbook.md](./cloud-redirect-runbook.md).

---

## 1. Supported Cloud Providers

Navi CloudRedirect supports five storage targets:

| Provider ID | Provider Name | Type | Description |
| :--- | :--- | :--- | :--- |
| `local-only` | Offline Diversion | Local Storage | Diverts saves locally without cloud syncing. Default fallback mode. |
| `gdrive` | Google Drive | OAuth2 (PKCE) | Authenticates via loopback port `53692` with `tokens_gdrive.json`. |
| `onedrive` | Microsoft OneDrive | OAuth2 (PKCE) | Authenticates via loopback port `53682` with `tokens_onedrive.json`. |
| `r2` | Cloudflare R2 | S3 Compatible | Custom endpoint with access key / secret credentials. |
| `s3` | Amazon AWS S3 | Object Storage | AWS S3 bucket storage with AWS credentials and region config. |

OAuth tokens are stored in `~/.config/CloudRedirect/tokens_<provider>.json` with restricted user-only permissions (`0600`). Provider status/sign-in endpoints: [REST API reference](../api/rest.md).

---

## 2. Configuration

### 2.1 Global Cloud Saves (`~/.config/navi/config.yaml`, or via CEF UI)

```yaml
CloudSavesEnabled: true
CloudSavesProvider: "local-only" # options: local-only, gdrive, onedrive, r2, s3
CloudSavesManagedApps:
  - 480
```

The Steam CEF UI (Steam Settings → Cloud Saves) exposes the same toggles: global redirection,
one-click provider OAuth, and per-game redirection in the Steam Library interface.

### 2.2 Proton Prefix Sync (`PrefixBucketSync`)

```yaml
CloudSaves:
  PrefixSync:
    Enabled: yes
    SyncAtStartup: yes
    SyncOnLaunch: yes
    SyncOnExit: yes
    MaxFileMb: 64
```

Behavior of this block: [cloud-redirect-architecture.md → PrefixBucketSync](./cloud-redirect-architecture.md#11-proton-prefix-emulator-save-synchronization-prefixbucketsync).
