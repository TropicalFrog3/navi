---
name: vibe-server-setup
description: Initialize, start, health-check, and configure the Vibe Annotations MCP server and workspace integration for visual browser feedback. Use when setting up Vibe Annotations, verifying daemon health, checking port 3846, or configuring Antigravity MCP integration.
---

# Vibe Annotations Server Setup & Management

## Purpose
Manage the local Vibe Annotations background daemon (`vibe-annotations-server`), configure the Model Context Protocol (MCP) endpoint for Antigravity, and verify end-to-end connectivity between the Chrome extension and coding agents.

---

## Architecture & Endpoints

| Component | Path / Address | Description |
| :--- | :--- | :--- |
| **Daemon Port** | `127.0.0.1:3846` | Background Express daemon |
| **Health API** | `http://127.0.0.1:3846/health` | Status and version probe (`GET`) |
| **MCP SSE Endpoint** | `http://127.0.0.1:3846/mcp` | SSE MCP protocol endpoint |
| **SSE Stream** | `http://127.0.0.1:3846/sse` | Event stream for MCP transport |
| **REST API** | `http://127.0.0.1:3846/api/annotations` | Direct HTTP annotations access |
| **Storage Path** | `~/.vibe-annotations/annotations.json` | Persistent local JSON store |

---

## Step 1: Health & Status Check

Before launching a new instance, always verify whether the daemon is already running:

```bash
# Using CLI
npx vibe-annotations-server status

# Or direct HTTP probe
curl -s http://127.0.0.1:3846/health
```

Expected output when running:
```json
{"status":"ok","version":"0.5.4","minExtensionVersion":"1.0.0","timestamp":"..."}
```

---

## Step 2: Server Lifecycle Commands

| Action | Command | Notes |
| :--- | :--- | :--- |
| **Start Server** | `npx vibe-annotations-server start` | Spawns background daemon on port 3846 |
| **Stop Server** | `npx vibe-annotations-server stop` | Stops background process cleanly |
| **Restart Server** | `npx vibe-annotations-server restart` | Restarts daemon and reloads data store |
| **Check Logs** | `npx vibe-annotations-server logs -f` | Tail server output and MCP events |
| **Reset Config** | `npx vibe-annotations-server init --reset` | Re-run interactive setup wizard |

---

## Step 3: Workspace MCP Configuration

Antigravity loads workspace-level MCP servers from `.agents/mcp_config.json`.

Ensure [`.agents/mcp_config.json`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/mcp_config.json) contains the `vibe-annotations` configuration under `mcpServers`:

```json
{
  "mcpServers": {
    "vibe-annotations": {
      "serverUrl": "http://127.0.0.1:3846/mcp"
    }
  }
}
```

> [!NOTE]
> If `.agents/mcp_config.json` already defines other servers, merge the `"vibe-annotations"` key into the existing `"mcpServers"` object rather than replacing the file.

---

## Step 4: Verification Checklist

Execute this verification sequence whenever testing or resetting the environment:

1. **Verify daemon process**:
   ```bash
   npx vibe-annotations-server status
   ```
2. **Verify HTTP health endpoint**:
   ```bash
   curl -s http://127.0.0.1:3846/health
   ```
3. **Verify MCP SSE endpoint handshake**:
   ```bash
   curl -i -s -H "Accept: text/event-stream" http://127.0.0.1:3846/mcp
   ```
   *Should respond with HTTP 200 and `: keepalive` stream.*
4. **Confirm MCP tool availability**:
   Verify that `read_annotations`, `watch_annotations`, `delete_annotation`, `get_project_context`, and `get_annotation_screenshot` are registered under the `vibe-annotations` MCP server.

---

## Troubleshooting

- **Port 3846 in use**:
  ```bash
  lsof -i :3846
  # or
  npx vibe-annotations-server stop && npx vibe-annotations-server start
  ```
- **Extension disconnected**:
  Ensure the Chrome extension is installed from the Chrome Web Store and that the background daemon on `http://127.0.0.1:3846` is running.
- **Empty annotations list**:
  Ensure annotations are created on the active `http://localhost:<PORT>` page with the extension overlay enabled.
