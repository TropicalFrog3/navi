# **Navi - Steamclient Modification for Linux**

## Index

1. [Getting started](#getting-started)
2. [Cloud Saves (CloudRedirect Integration)](#cloud-saves-cloudredirect-integration)
3. [Hall of Fame 👑](#hall-of-fame-aka-credits)
4. [Hall of Shame 🚨](#hall-of-shame-aka-scammers-leechers-etc)
5. [Support](#support)
6. [Related Projects](#related-projects)

## Getting started

Check out the [Installation](https://github.com/AceSLS/navi/wiki/Installation) or the [Building from Source](https://github.com/AceSLS/navi/wiki/Building-from-Source) section in our Wiki!

## Cloud Saves (CloudRedirect Integration)

Navi natively integrates **CloudRedirect** (v2.6.5) to provide seamless cloud save diversion and synchronization for unowned, family-shared, and sideloaded games on Linux and SteamOS.

### Key Features
- **Transparent Save Redirection**: Intercepts Steam Cloud I/O via a lightweight 32-bit preload engine (`cloud_redirect.so`) and redirects save games into structured local directories.
- **Multiple Cloud Backends**:
  - `local-only`: High-performance local diversion without remote syncing (default).
  - `gdrive`: Google Drive with secure local PKCE OAuth2 flow.
  - `onedrive`: Microsoft OneDrive with local PKCE OAuth2 flow.
  - `r2`: Cloudflare R2 object storage.
  - `s3`: Amazon AWS S3 object storage.
- **Backup & Rollback Safety**:
  - Out-of-process operation queue (`bin/navi-cloud-apply-pending`) ensuring file wipes or restores run exclusively when Steam is closed.
  - Automated compressed `.tar.gz` snapshots stored in `~/.config/navi/cloud_backups/` before any reset.
  - Typed `"RESET"` confirmation required for destructive actions.
  - Fail-closed rollback mechanism in case of errors.
- **Non-Destructive SLSsteam Integration**: Uses a managed sentinel block with set-union merging ($S_{merged} = S_{user} \cup S_{navi}$) so existing user configurations are never overwritten.

### Enabling & Configuration
- **Via CEF UI**: Navigate to Steam Settings &gt; Cloud Saves to toggle redirection globally, link your cloud provider via one-click OAuth, or toggle cloud redirection per-game in the Steam Library interface.
- **Via Configuration**: In `~/.config/SLSsteam/config.yaml` (or Navi settings):
  ```yaml
  CloudSavesEnabled: true
  CloudSavesProvider: "local-only" # options: local-only, gdrive, onedrive, r2, s3
  CloudSavesManagedApps:
    - 480
  ```

For detailed architectural specifications, API details, and troubleshooting commands, see [docs/cloud-saves.md](docs/cloud-saves.md).



## Hall of Fame aka Credits

Contributors:
- [Parasitic-Hollow](https://github.com/Parasitic-Hollow/): Fixing gamepad issues caused by FakeAppIds & maintaining Navi in my absence
- [exefer](https://github.com/exefer): Fixing playtime getting deleted on Steamclient restart for refunded games
- [dankrr](https://github.com/dankrr): Fixing non-steam game's rich presence
- [amione](https://github.com/xamionex/): Creating the Navi banner & logo the instant he found out I was looking around for one <3
- [DeveloperMikey](https://github.com/DeveloperMikey): Added Nix support
- [skrimix](https://github.com/skrimix): Added flatpak support
- thismanq: Informing me that DisableFamilyShareLockForOthers is possible

Others:
- All the staff members of the Anti Denuvo Sanctuary for all their hard work they do. They also found a way to use Navi I didn't even intend to, so shoutout to them
- Riku_Wayfinder: Being extremely supportive and lightening my workload by a lot. So show him some love my guys <3
- Gnanf: Helping me test the Family Sharing bypass
- rdbo: For his great libmem library, which saved me a lot of development and learning time
- jbeder: For the awesome yaml-cpp library which allowed me to easily add a configuration file
- oleavr and all the other awesome people working on Frida for easy instrumentation which helps a lot in analyzing, testing and debugging
- All the folks working on Ghidra, this was my first project using it and I'm in love with it!
- And many more I can't possibly list here for reporting bugs and giving feedback! Thank you guys <3


## Hall of Shame aka Scammers, Leechers, etc

🚨This list exists purely for educational and entertainment purposes!
Please do not seek out Projects listed here!
If you decide to ignore the aforementioned warning you do so on your own risk!🚨

OnetapBeta & Hammer Decky by Hammer Steam: Resells Steamless & Navi for a bogus price, completely breaking licensing agreements and leeching off the communities hard work while putting in 0 effort themself.

## Support

Please do not treat the issue tracker like a private support hotline!
Feel free to join our [Discord](https://discord.gg/j3ZzjeV4eQ) instead.

## Related Projects

[h3adcr-b](https://github.com/Deadboy666/h3adcr-b) & [h3adcr-b-wiki](https://github.com/Deadboy666/h3adcr-b/wiki/): Universal Navi installer & steamclient downgrader

[steamnetsock-patch](https://github.com/yesyes0649/steamnetsock-patch) & [eos-proxy](https://github.com/yesyes0649/eos-proxy): Makes FakeAppIds work in some games where it otherwise wouldn't

[CloudRedirect](https://github.com/Selectively11/CloudRedirect): Enables Steam Cloud, playtime & achievement sync on unowned games

[SteamDB App Parser](https://greasyfork.org/en/scripts/543010-steamdb-app-parser): Vibecoded script that adds a button on SteamDB Game's pages to generate AdditionalApps & DlcData
