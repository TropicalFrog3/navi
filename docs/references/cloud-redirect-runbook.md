# CloudRedirect Operations Runbook

Field procedures for the Navi CloudRedirect integration: the out-of-process pending-ops
helper and diagnostic tooling. For architecture see
[cloud-redirect-architecture.md](./cloud-redirect-architecture.md).

---

## 1. Idle Pending Operations Helper (`bin/navi-cloud-apply-pending`)

Directly modifying or resetting save files while Steam or `steamwebhelper` is running can cause SQLite lockups, in-memory cache overwrites, or Steam Cloud conflict errors. To prevent this, Navi enforces an **out-of-process safety model**.

### Architecture & Safety Protocol

```
CEF UI / API Client
       │
       ▼ (Request: Reset / Restore)
Navi REST Controller (:9001)
       │
       ▼ (Enqueue Operation)
~/.config/CloudRedirect/cloud_pending_ops.json
       │
       ▼ (UI displays: Restart Required)
User exits Steam
       │
       ▼
bin/navi-cloud-apply-pending --process-all
       │
       ├── 1. Verify Steam is dead (scan /proc for steam & steamwebhelper)
       ├── 2. Create pre-operation safety backup (.tar.gz)
       ├── 3. Execute atomic reset or restore
       └── 4. Mark status COMPLETED (or ROLLBACK on error)
```

1. **Queue File**: Pending tasks are serialized into `~/.config/CloudRedirect/cloud_pending_ops.json`.
2. **Process Idle Check**: Before executing any operation, `AssertSteamIdle()` scans `/proc` PID directories and validates that neither `steam` nor `steamwebhelper` processes are active. If any Steam process is alive, execution immediately halts (fail-safe).
3. **Pre-Reset Backup**: Prior to wiping any save directory, an automatic compressed tarball backup (`.tar.gz`) is written to `~/.config/navi/cloud_backups/<appId>/`.
4. **Fail-Closed Rollback**: If a restore or reset fails halfway through execution, the helper automatically restores from the pre-operation backup and records `status: "ROLLED_BACK"` with the error log.

---

## 2. Troubleshooting & Diagnostics

### Emergency Killswitch

If CloudRedirect prevents Steam from launching or introduces issues with game saves, activate the file-based killswitch:

```bash
# Disable CloudRedirect immediately
touch ~/.config/CloudRedirect/disable

# Re-enable CloudRedirect
rm -f ~/.config/CloudRedirect/disable
```

When this file exists, the dynamic loader wrapper and engine bypass all diversion hooks completely.

### CLI Status Inspection

Inspect provider authentication status, active tokens, and sync states via the bundled CLI:

```bash
# Check OAuth status
~/.local/share/navi/bin/cloud_redirect_cli auth-status

# Verify local token files
ls -la ~/.config/CloudRedirect/tokens_*.json

# View pending out-of-process operations
cat ~/.config/CloudRedirect/cloud_pending_ops.json
```

### Manual Processing of Pending Operations

If pending operations did not run automatically on exit:

```bash
# Ensure Steam is completely closed
killall -9 steam steamwebhelper 2>/dev/null

# Process all queued operations manually
~/.local/share/navi/bin/navi-cloud-apply-pending --process-all
```
