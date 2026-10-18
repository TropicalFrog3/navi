---
name: steamdeck-wireless-workflow
description: End-to-end workflow for controlling, syncing, inspecting, and debugging physical Steam Deck hardware wirelessly over LAN/Wi-Fi via deck-ctl.sh and CEF CDP for Navi.
---

# Physical Steam Deck Wireless Workflow

## 1. Overview & Architecture
This skill guides agents in orchestrating, controlling, and testing against a physical Steam Deck (SteamOS 3) over local Wi-Fi / LAN without physical cables.

```
Host (Agent / Host PC)
  |-- Port 22    ---> Steam Deck:22 (SSH via ~/.ssh/id_ed25519)
  |-- Port 8080  ---> Local SSH Tunnel to Deck 127.0.0.1:8080 (Steam CEF Remote Debugging)
  |-- Port 8081  ---> Direct Wi-Fi CEF endpoint (0.0.0.0:8081 proxy to 127.0.0.1:8080)
  |-- Rsync/SSH  ---> Remote ~/navi
```

---

## 2. Master Wireless Controller CLI (`scripts/dev/deck-ctl.sh`)
Always use `scripts/dev/deck-ctl.sh` for interacting with the physical Steam Deck:

| Action | Command | Purpose |
| :--- | :--- | :--- |
| **Status Check** | `scripts/dev/deck-ctl.sh status` | Verify Wi-Fi latency, SSH, battery %, mode (Gaming vs Desktop), Steam process, and CEF ports |
| **Interactive Shell**| `scripts/dev/deck-ctl.sh ssh` | Open interactive shell into Steam Deck (`deck@steamdeck.local`) |
| **Run as deck** | `scripts/dev/deck-ctl.sh exec "<cmd>"` | Execute shell command inside Steam Deck |
| **Run as root** | `scripts/dev/deck-ctl.sh sudo "<cmd>"` | Execute command with sudo on Steam Deck |
| **Sync Workspace** | `scripts/dev/deck-ctl.sh sync` | Delta-compressed rsync from host repo to `~/navi` |
| **Deploy & Install**| `scripts/dev/deck-ctl.sh deploy` | Sync workspace and run `./install.sh install` on the Deck |
| **Run Tests** | `scripts/dev/deck-ctl.sh test [all\|flow]` | Execute automated test suite (`./install.sh test`) or UI flow test on Steam Deck |
| **Restart Steam** | `scripts/dev/deck-ctl.sh restart-steam` | Cleanly restart Steam (auto-detects Gaming Mode vs Desktop Mode) |
| **Start Tunnel** | `scripts/dev/deck-ctl.sh tunnel` | Establish background `127.0.0.1:8080` SSH tunnel for local host Python scripts |
| **Tunnel Status** | `scripts/dev/deck-ctl.sh tunnel-status` | Check if local port 8080 tunnel is active |
| **Stop Tunnel** | `scripts/dev/deck-ctl.sh tunnel-stop` | Terminate background port 8080 tunnel |
| **Send Steam URL** | `scripts/dev/deck-ctl.sh steam-url "<url>"` | Trigger `steam 'steam://...'` on Deck |
| **Deck Notify** | `scripts/dev/deck-ctl.sh notify "<title>" "<msg>"` | Show notification popup on Steam Deck screen |
| **List Targets** | `scripts/dev/deck-ctl.sh cdp-targets` | List active Steam CEF windows (SharedJSContext, Steam, menus) |
| **CDP Evaluate** | `scripts/dev/deck-ctl.sh cdp-eval "<target>" "<expr>"` | Run JavaScript via CDP WebSocket directly |
| **Stream Navi Log** | `scripts/dev/deck-ctl.sh tail-sls` | Tail `~/.navi.log` in real time |
| **Stream Content Log**| `scripts/dev/deck-ctl.sh tail-content` | Tail `content_log.txt` in real time |
| **Stream Gamescope**| `scripts/dev/deck-ctl.sh tail-gamescope` | Tail Gaming Mode compositor journal in real time |

---

## 3. Remote Debugging & CDP Target Arbitration
- Steam on SteamOS natively binds `-cef-enable-debugging` strictly to loopback `127.0.0.1:8080`.
- The Deck runs a forwarder service routing incoming Wi-Fi requests on port `8081` to `127.0.0.1:8080`.
- For host-side testing scripts (`python3 list_targets.py`, `pytest tests/integration/`, etc.):
  - Ensure the SSH tunnel is active via `scripts/dev/deck-ctl.sh tunnel`.
  - All host scripts querying `http://127.0.0.1:8080/json` work natively with zero modifications.
  - Alternatively, target `http://steamdeck.local:8081/json` directly over Wi-Fi.

---

## 4. Deploying Navi
```bash
# 1. Sync workspace files to Steam Deck
scripts/dev/deck-ctl.sh sync

# 2. Run installation inside Deck user environment
scripts/dev/deck-ctl.sh exec "cd ~/navi && ./install.sh install"

# 3. Reload Steam to inject hook libraries
scripts/dev/deck-ctl.sh restart-steam

# 4. Stream and verify logs
scripts/dev/deck-ctl.sh tail-sls
```
