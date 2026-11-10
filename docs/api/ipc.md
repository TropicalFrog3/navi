# Navi File IPC API (`/tmp/navi.API`)

Navi has a toggleable local IPC API. When enabled in your configuration, commands can be written directly to `/tmp/navi.API`.

API commands are delimited using `|` as a separator. For example, to install Spacewar (AppID `480`) to library `0`:
```bash
echo "install|480|0" > /tmp/navi.API
```

Optional arguments are indicated in square brackets `[]`.

---

## Application Manager
- `dumplibraries`: Lists all available Steam libraries (label, path, index).
- `install|appId|libraryIndex`: Installs `appId` into the library at `libraryIndex`.
- `uninstall|appId`: Uninstalls `appId`.

---

## Compatibility Manager
- `dumpcompat|appId`: Lists all available compatibility tools for `appId`.
- `getcompat|appId`: Retrieves current compatibility tool configured for `appId`.
- `setcompat|appId[|tool-name]`: Sets or clears the compatibility tool for `appId`.

---

*Back to the full reference index: [API.md](./API.md) · Sibling document: [REST API](./rest.md)*
