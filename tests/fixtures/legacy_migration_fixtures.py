"""
Fixtures and helpers for legacy ACCELA and SLSsteam migration integration tests.
"""

from pathlib import Path


def populate_legacy_accela(home: Path):
    """
    Sets up legacy ACCELA directories, manifests, binaries, desktop files, and cache.
    """
    manifest_dir = home / ".local" / "share" / "ACCELA" / "morrenus_manifests"
    manifest_dir.mkdir(parents=True, exist_ok=True)
    (manifest_dir / "1001.manifest").write_text('"AppState" { "appid" "1001" "name" "Legacy App 1" }\n')
    (manifest_dir / "1002.manifest").write_text('"AppState" { "appid" "1002" "name" "Legacy App 2" }\n')

    depots_dir = home / ".local" / "share" / "ACCELA" / "depots"
    depots_dir.mkdir(parents=True, exist_ok=True)
    (depots_dir / "depot_1001.bin").write_text("depot payload data")

    accela_bin = home / ".local" / "share" / "ACCELA" / "accela"
    accela_bin.write_text("#!/bin/sh\necho accela\n")
    accela_bin.chmod(0o755)

    venv_bin = home / ".local" / "share" / "ACCELA" / ".venv" / "bin"
    venv_bin.mkdir(parents=True, exist_ok=True)
    (venv_bin / "python").write_text("#!/bin/sh\n")

    apps_dir = home / ".local" / "share" / "applications"
    apps_dir.mkdir(parents=True, exist_ok=True)
    (apps_dir / "accela.desktop").write_text("[Desktop Entry]\nName=ACCELA\nExec=accela\n")

    pixmaps_dir = home / ".local" / "share" / "pixmaps"
    pixmaps_dir.mkdir(parents=True, exist_ok=True)
    (pixmaps_dir / "accela.png").write_text("png dummy")

    icon_dir = home / ".local" / "share" / "icons" / "hicolor" / "256x256" / "apps"
    icon_dir.mkdir(parents=True, exist_ok=True)
    (icon_dir / "accela.png").write_text("png dummy")

    helper_bin = home / ".local" / "bin"
    helper_bin.mkdir(parents=True, exist_ok=True)
    (helper_bin / "accela-download").write_text("#!/bin/sh\n")

    cache_dir = home / ".cache" / "ACCELA"
    cache_dir.mkdir(parents=True, exist_ok=True)
    (cache_dir / "cache_file.dat").write_text("accela cache")


def populate_legacy_slssteam(home: Path):
    """
    Sets up legacy SLSsteam directories, configuration, tracking files, and wrappers.
    """
    cfg_dir = home / ".config" / "SLSsteam"
    cfg_dir.mkdir(parents=True, exist_ok=True)
    (cfg_dir / "config.yaml").write_text("LegacySetting: sls_original_value\nSafeMode: yes\n")
    (cfg_dir / "updates.yaml.bak-20250101-120000").write_text("legacy backup yaml\n")

    sls_share = home / ".local" / "share" / "SLSsteam"
    sls_share.mkdir(parents=True, exist_ok=True)
    (sls_share / "SLSsteam.so").write_text("dummy sls binary")

    local_bin = home / ".local" / "bin"
    local_bin.mkdir(parents=True, exist_ok=True)
    (local_bin / "steam.pre-slssteam.bak").write_text("#!/bin/sh\nexec /usr/bin/steam \"$@\"\n")
    (local_bin / "steam").write_text(
        f'#!/bin/sh\nexec env LD_AUDIT="{home}/.local/share/SLSsteam/SLSsteam.so" /usr/bin/steam "$@"\n'
    )
    (local_bin / "steam").chmod(0o755)

    # Tracking files
    (home / ".SLSsteam.log").write_text("legacy log")
    (home / ".SLSsteam.json").write_text("{}")
    (home / ".SLSsteam.installed").write_text("1")
    (home / ".SLSsteam.onlinefix").write_text("1")
    (home / ".SLSsteam.autocrack").write_text("1")

    # Shell startup contamination
    bashrc = home / ".bashrc"
    bashrc.write_text(bashrc.read_text() + '\nexport PATH="$HOME/.local/share/SLSsteam/path:$PATH"\n')
    profile = home / ".profile"
    profile.write_text(profile.read_text() + '\nexport PATH="$HOME/.local/share/SLSsteam/path:$PATH"\n')

    # Contaminated desktop file
    app_dir = home / ".local" / "share" / "applications"
    app_dir.mkdir(parents=True, exist_ok=True)
    (app_dir / "steam.desktop").write_text(
        '[Desktop Entry]\nName=Steam\nExec=env LD_AUDIT=".../SLSsteam.so" /usr/bin/steam %U\n'
    )


def populate_legacy_steam_sh_audit(home: Path):
    """
    Sets up a legacy LD_AUDIT injection directly inside steam.sh with an intact steam.sh.bak.
    """
    steam_dir = home / ".local" / "share" / "Steam"
    steam_dir.mkdir(parents=True, exist_ok=True)
    launcher = steam_dir / "steam.sh"
    launcher_bak = steam_dir / "steam.sh.bak"

    launcher_bak.write_text("#!/bin/sh\n# Native original launcher\nexec /usr/bin/true \"$@\"\n")
    launcher_bak.chmod(0o755)

    launcher.write_text(
        f'#!/bin/sh\nexport LD_AUDIT="{home}/.local/share/SLSsteam/SLSsteam.so"\nexec {launcher_bak} "$@"\n'
    )
    launcher.chmod(0o755)


def populate_legacy_headcrab_client_sh(home: Path):
    """
    Sets up a legacy Headcrab/SLSsteam wrapper steam.sh that exports INJECT_SLS
    and sources client.sh (the original Valve launcher), with NO steam.sh.bak.
    """
    steam_dir = home / ".local" / "share" / "Steam"
    steam_dir.mkdir(parents=True, exist_ok=True)
    client_sh = steam_dir / "client.sh"
    launcher = steam_dir / "steam.sh"

    client_sh.write_text(
        "#!/usr/bin/env bash\n# Native original launcher from Valve\necho 'Native Valve steam.sh'\nexec /usr/bin/true \"$@\"\n"
    )
    client_sh.chmod(0o755)

    launcher.write_text(
        f"#!/usr/bin/env bash\n"
        f'STEAM_CLIENT="{client_sh}"\n'
        f'INJECT_SLS="LD_AUDIT={home}/.local/share/SLSsteam/library-inject.so:{home}/.local/share/SLSsteam/SLSsteam.so"\n'
        f"export $INJECT_SLS\n"
        f'. "$STEAM_CLIENT" "$@"\n'
    )
    launcher.chmod(0o555)


def populate_legacy_steam_sh_inline_audit(home: Path):
    """
    Sets up a steam.sh containing an inline SLSsteam LD_AUDIT line without any client.sh or .bak file.
    """
    steam_dir = home / ".local" / "share" / "Steam"
    steam_dir.mkdir(parents=True, exist_ok=True)
    launcher = steam_dir / "steam.sh"

    launcher.write_text(
        f"#!/usr/bin/env bash\n"
        f"# Native steam launcher with inline legacy injection\n"
        f'export LD_AUDIT="{home}/.local/share/SLSsteam/SLSsteam.so"\n'
        f'echo "Starting steam natively"\n'
        f'exec /usr/bin/true "$@"\n'
    )
    launcher.chmod(0o755)

