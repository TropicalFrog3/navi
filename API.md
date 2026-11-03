# Navi API Reference

Navi provides two distinct API interfaces, documented in modular reference files:

1. **File IPC API (`/tmp/navi.API`)**: Command-based text pipe for client and library management.
2. **HTTP REST API (`http://127.0.0.1:9001`)**: Authenticated REST daemon for CloudRedirect cloud saves, backups, and provider integration.

---

## Reference Documents

| Document | Scope |
| :--- | :--- |
| [`docs/api/ipc.md`](docs/api/ipc.md) | File IPC API `/tmp/navi.API` — pipe-delimited commands (`dumplibraries`, `install`, `setcompat`, …) |
| [`docs/api/rest.md`](docs/api/rest.md) | HTTP REST API `127.0.0.1:9001` — security model (`X-Navi-Auth`, origin allowlist) and all `/cloud/*` endpoints |

Operational behavior (pending-ops queue, rollback safety, troubleshooting) is covered by the
[CloudRedirect Runbook](docs/references/cloud-redirect-runbook.md), included in the
[CloudRedirect integration guide](docs/cloud-saves.md).
