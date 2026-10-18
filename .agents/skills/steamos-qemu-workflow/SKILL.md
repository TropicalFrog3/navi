---
name: steamos-qemu-workflow
description: End-to-end workflow for running, controlling, and debugging SteamOS inside QEMU, including SSH remote execution, 9p file sharing, and Steam CEF remote debugging for Navi.
---

# SteamOS QEMU Automation & Debugging Workflow

## 1. Overview & Architecture
This skill guides agents in orchestrating, controlling, and testing against a SteamOS guest virtual machine running under QEMU.

```
Host (Agent / Host OS)
  |-- Port 2222  ---> SteamOS Guest:22 (SSH via id_steamos)
  |-- Port 8080  ---> SteamOS Guest:8080 (Steam CEF Remote Debugging)
  |-- Port 8081  ---> SteamOS Guest:8081 (Auxiliary services)
  |-- VirtFS 9p  ---> Guest /var/mnt (symlinked to ~/navi)
  |-- monitor.sock -> QEMU Monitor (screendump, sendkey)
  |-- qmp.sock   ---> QEMU QMP Socket
```

---

## 2. Launching the Virtual Machine
Run from the repository root:
```bash
./steam-os/start.sh
# OR for Gaming Mode with 3D VirtIO-GPU acceleration:
./steam-os/start-gaming-mode.sh
```

---

## 3. Host Controller CLI (`steam-os/steamos-ctl.sh`)
Always use `./steam-os/steamos-ctl.sh` for interacting with the VM:

| Action | Command | Purpose |
| :--- | :--- | :--- |
| **Status Check** | `./steam-os/steamos-ctl.sh status` | Verify QEMU, SSH, 9p share, and CEF 8080 |
| **Run as deck** | `./steam-os/steamos-ctl.sh exec "<cmd>"` | Execute shell command inside guest |
| **Run as root** | `./steam-os/steamos-ctl.sh sudo "<cmd>"` | Execute passwordless root command |
| **Interactive Shell** | `./steam-os/steamos-ctl.sh ssh` | Drop into guest interactive terminal |
| **Enable Sudo** | `./steam-os/steamos-ctl.sh enable-nopasswd` | Configure passwordless sudo for user `deck` |
| **Restart Steam** | `./steam-os/steamos-ctl.sh restart-steam` | Launch Steam with `-cef-enable-debugging` |
| **Send Steam URL** | `./steam-os/steamos-ctl.sh steam-url "<url>"` | Trigger `steam 'steam://...'` |
| **Desktop Notify** | `./steam-os/steamos-ctl.sh notify "<title>" "<msg>"` | Send in-VM notification |
| **List Targets** | `./steam-os/steamos-ctl.sh cdp-targets` | List active Steam CEF windows |
| **CDP Evaluate** | `./steam-os/steamos-ctl.sh cdp-eval "<target>" "<expr>"` | Run JavaScript via CDP WebSocket |
| **Stream Navi Log** | `./steam-os/steamos-ctl.sh tail-sls` | Tail `~/.navi.log` in real time |
| **Stream Content Log**| `./steam-os/steamos-ctl.sh tail-content` | Tail `content_log.txt` in real time |
| **Capture Screenshot**| `./steam-os/steamos-ctl.sh screenshot [out.ppm]` | Capture VM framebuffer via monitor |
| **Send Keystroke**| `./steam-os/steamos-ctl.sh send-key <key>` | Send key to VM (e.g. `ctrl-alt-f3`) |

---

## 4. Steam Environment Invariants (Wayland / Plasma)
SteamOS desktop mode runs under KDE Plasma Wayland (`startplasma-wayland`).
When executing GUI commands in the guest over SSH, always set the session environment variables:
```bash
export DISPLAY=:0
export WAYLAND_DISPLAY=wayland-0
export XDG_RUNTIME_DIR=/run/user/1000
```

To launch Steam properly within the KDE session without detaching or hanging:
```bash
kstart5 -- /usr/bin/steam -cef-enable-debugging
# OR detached via user systemd slice:
systemd-run --user --slice=app.slice /usr/bin/steam -cef-enable-debugging
```

---

## 5. File Sharing (VirtFS 9p)
- The repository is mounted via QEMU `-virtfs` tag `hostshare`.
- In SteamOS, it is mounted at `/var/mnt` (with `/mnt -> var/mnt` symlink).
- The user home directory has a symlink:
  ```bash
  ~/navi -> /mnt
  ```
- **Live Sync:** Any build output (e.g., `prod-release/` or `bin/`) generated on the host is **instantly accessible** inside `~/navi` in the guest without file copying.

---

## 6. CEF Remote Debugging on Port 8080
- Steam running with `-cef-enable-debugging` listens on `127.0.0.1:8080`.
- The guest kernel has `net.ipv4.conf.all.route_localnet=1` and an iptables DNAT rule routing incoming packets on port 8080 to `127.0.0.1:8080`.
- The host forwards `127.0.0.1:8080 -> Guest:8080`.
- As a result, all Python test scripts run directly from the host machine against `http://127.0.0.1:8080/json`.

### Port 8080 Conflict Arbitration (Host vs. Guest)
- **Host Port Claim:** QEMU `-netdev ... hostfwd=tcp::8080-:8080` binds `0.0.0.0:8080` on the host machine.
- **Rule 1 (Starting QEMU / Running in SteamOS):**
  You **MUST kill Steam on the local hardware** before launching QEMU or running tests in SteamOS. If local Steam (`steamwebhelper`) is running, QEMU will abort on startup with `Could not set up host forwarding rule 'tcp::8080-:8080': Address already in use`.
  ```bash
  # Pre-flight host Steam cleanup:
  pkill -9 -f steamwebhelper 2>/dev/null || true
  pkill -9 steam 2>/dev/null || true
  ```
- **Rule 2 (Testing on Local Hardware):**
  Conversely, when switching back to testing on the local hardware, the QEMU VM must be stopped or terminated before launching local Steam with `-cef-enable-debugging`:
  ```bash
  pkill -f "qemu-system.*steamOSDrive" 2>/dev/null || true
  ```
- **Verification:** Always verify port 8080 ownership before starting either environment:
  ```bash
  ss -tulpn | grep 8080
  ```
- **Guest Background Daemons:** Because systemd SSH sessions enable `KillUserProcesses=yes`, background proxy scripts or services in SteamOS must be spawned via `systemd-run --user --unit=<name>` to persist after SSH session disconnect.

---

## 7. Integrating with `.agents` Skills

### A. `cef-cdp-injection`
Query `http://127.0.0.1:8080/json` directly from Python on the host. Common targets:
- `"SharedJSContext"`: MobX state (`window.appStore.allApps`, `window.appStore.m_mapApps`).
- `"Steam Big Picture Mode"`: Gamepad UI.
- `"Steam"`: Desktop client window.

### B. `steam-native-hooking`
- **Build on Host:**
  ```bash
  ./install.sh build
  ```
- **Install in Guest:**
  ```bash
  ./steam-os/steamos-ctl.sh exec "cd ~/navi && ./install.sh install"
  ```
- **Restart Steam:**
  ```bash
  ./steam-os/steamos-ctl.sh restart-steam
  ```
- **Verify Live Logs:**
  ```bash
  ./steam-os/steamos-ctl.sh tail-sls
  ./steam-os/steamos-ctl.sh tail-content
  ```
