# Navi HTTP REST API (`http://127.0.0.1:9001`)

The embedded Navi REST controller listens locally on `127.0.0.1:9001` to serve the Steam CEF UI and automation tooling.

---

## Security & Authentication

- **Origin Allowlisting**: All incoming requests are validated against allowed origins:
  - `https://steamloopback.host` (Steam CEF internal context)
  - `http://127.0.0.1:9001` / `http://localhost:9001`
  Requests originating from non-allowlisted browser origins are rejected with `403 Forbidden`.
- **Authentication Header**: State-altering endpoints (`POST`) require the `X-Navi-Auth` header:
  ```http
  X-Navi-Auth: <token>
  ```
  Navi verifies the token using constant-time `CRYPTO_memcmp` to protect against timing attacks. Requests with invalid or missing tokens return `401 Unauthorized`.
- **Common Error Codes**:
  - `400 Bad Request`: Invalid payload syntax or missing required confirmation string.
  - `401 Unauthorized`: Missing or incorrect `X-Navi-Auth` token.
  - `403 Forbidden`: Request blocked by origin allowlist check.
  - `409 Conflict`: Operation conflict (e.g., OAuth sign-in flow already active).

---

## Endpoints

### `GET /cloud/status`
Returns the global status of the CloudRedirect subsystem.

- **Auth Required**: No
- **Response** (`200 OK`):
  ```json
  {
    "enabled": true,
    "provider": "local-only",
    "managedAppsCount": 3,
    "pendingOpsCount": 0
  }
  ```

---

### `POST /cloud/toggle`
Enables or disables CloudRedirect globally.

- **Auth Required**: Yes (`X-Navi-Auth`)
- **Request Body**:
  ```json
  {
    "enable": true
  }
  ```
- **Response** (`200 OK`):
  ```json
  {
    "success": true,
    "message": "Cloud saves enabled"
  }
  ```

---

### `GET /cloud/provider/status`
Retrieves authentication and configuration status for a specific cloud provider.

- **Auth Required**: No
- **Query Parameters**:
  - `provider` (optional): Provider identifier (`local-only`, `gdrive`, `onedrive`, `r2`, `s3`). Defaults to the configured provider if omitted.
- **Response** (`200 OK`):
  ```json
  {
    "provider": "gdrive",
    "authenticated": true,
    "accountEmail": "gamer@example.com",
    "raw": {}
  }
  ```

---

### `POST /cloud/provider/signin`
Initiates an OAuth2 PKCE login flow on a local loopback port for cloud authentication.

- **Auth Required**: Yes (`X-Navi-Auth`)
- **Request Body**:
  ```json
  {
    "provider": "gdrive"
  }
  ```
- **Response** (`200 OK`):
  ```json
  {
    "authUrl": "https://accounts.google.com/o/oauth2/v2/auth?client_id=...&response_type=code...",
    "provider": "gdrive"
  }
  ```
- **Response** (`409 Conflict`):
  ```json
  {
    "error": "Authentication flow already in progress for this provider"
  }
  ```

---

### `POST /cloud/provider/signout`
Revokes active credentials and clears cached tokens for the specified provider.

- **Auth Required**: Yes (`X-Navi-Auth`)
- **Request Body**:
  ```json
  {
    "provider": "gdrive"
  }
  ```
- **Response** (`200 OK`):
  ```json
  {
    "success": true,
    "message": "Signed out"
  }
  ```

---

### `GET /cloud/apps/<appId>/status`
Returns save redirection status, storage footprint, and available backups for a specific application.

- **Auth Required**: No
- **Path Parameters**:
  - `appId` (integer): Steam Application ID.
- **Response** (`200 OK`):
  ```json
  {
    "appId": 480,
    "redirected": true,
    "storageUsageBytes": 1048576,
    "backups": [
      "/home/user/.config/navi/cloud_backups/480/manual-1730000000.tar.gz"
    ]
  }
  ```

---

### `POST /cloud/apps/<appId>/toggle`
Enables or disables save redirection for an individual application and synchronizes SLSsteam YAML configuration.

- **Auth Required**: Yes (`X-Navi-Auth`)
- **Path Parameters**:
  - `appId` (integer): Steam Application ID.
- **Request Body**:
  ```json
  {
    "redirect": true
  }
  ```
- **Response** (`200 OK`):
  ```json
  {
    "appId": 480,
    "redirected": true
  }
  ```

---

### `POST /cloud/apps/<appId>/backup`
Generates a manual compressed archive (`.tar.gz`) of the game's redirected save storage and userdata remote files.

- **Auth Required**: Yes (`X-Navi-Auth`)
- **Path Parameters**:
  - `appId` (integer): Steam Application ID.
- **Response** (`200 OK`):
  ```json
  {
    "success": true,
    "appId": 480,
    "backupPath": "/home/user/.config/navi/cloud_backups/480/manual-1730000000.tar.gz"
  }
  ```

---

### `POST /cloud/apps/<appId>/restore`
Enqueues an out-of-process restore operation from a specified backup archive.

- **Auth Required**: Yes (`X-Navi-Auth`)
- **Path Parameters**:
  - `appId` (integer): Steam Application ID.
- **Request Body**:
  ```json
  {
    "backupFile": "/home/user/.config/navi/cloud_backups/480/manual-1730000000.tar.gz"
  }
  ```
- **Response** (`202 Accepted`):
  ```json
  {
    "status": "pending",
    "restartRequired": true,
    "operationId": "op_1730000000_480_restore"
  }
  ```

---

### `POST /cloud/apps/<appId>/reset`
Enqueues an out-of-process reset operation to clear cloud save storage and reset metadata caches. Requires typed confirmation.

- **Auth Required**: Yes (`X-Navi-Auth`)
- **Path Parameters**:
  - `appId` (integer): Steam Application ID.
- **Request Body**:
  ```json
  {
    "confirm": "RESET"
  }
  ```
- **Response** (`202 Accepted`):
  ```json
  {
    "status": "pending",
    "restartRequired": true,
    "operationId": "op_1730000000_480_reset"
  }
  ```
- **Response** (`400 Bad Request` if confirmation omitted or incorrect):
  ```json
  {
    "error": "Confirmation required: payload must contain {\"confirm\": \"RESET\"}"
  }
  ```

---

### `GET /cloud/pending-ops`
Queries all queued, in-progress, completed, and rolled-back out-of-process operations.

- **Auth Required**: No
- **Response** (`200 OK`):
  ```json
  {
    "operations": [
      {
        "id": "op_1730000000_480_reset",
        "appId": 480,
        "accountId": 0,
        "type": "RESET",
        "status": "PENDING",
        "timestamp": 1730000000,
        "backupPath": "/home/user/.config/navi/cloud_backups/480/pre_op_backup_1730000000.tar.gz",
        "restoreSourcePath": "",
        "retries": 0,
        "lastError": ""
      }
    ],
    "hasPending": true
  }
  ```

---

*Back to the full reference index: [API.md](../../API.md) · Sibling document: [File IPC API](./ipc.md) · Operational behavior: [CloudRedirect Runbook](../references/cloud-redirect-runbook.md)*
