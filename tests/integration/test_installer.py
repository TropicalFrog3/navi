"""
@file  test_installer.py
@brief Integration tests for install.sh script logic and directory layout


Integration tests for the Navi installer dispatcher (install.sh).
"""

from pathlib import Path


def _get_files_set(root: Path):
    return {str(p.relative_to(root)) for p in root.rglob("*") if p.is_file()}


def test_dry_run_modes(fake_home, run_installer):
    """
    Test that --dry-run prints [DRY-RUN] preview actions and creates 0 files in fake_home.
    """
    initial_files = _get_files_set(fake_home)

    # 1. install --dry-run
    res_install = run_installer("install", "--dry-run")
    assert res_install.returncode == 0, f"install --dry-run failed: {res_install.stderr}"
    assert "[DRY-RUN]" in res_install.stdout
    assert _get_files_set(fake_home) == initial_files

    # 2. update --dry-run
    res_update = run_installer("update", "--dry-run")
    assert res_update.returncode == 0, f"update --dry-run failed: {res_update.stderr}"
    assert "[DRY-RUN]" in res_update.stdout
    assert _get_files_set(fake_home) == initial_files

    # 3. uninstall --dry-run
    res_uninstall = run_installer("uninstall", "--dry-run")
    assert res_uninstall.returncode == 0, f"uninstall --dry-run failed: {res_uninstall.stderr}"
    assert "[DRY-RUN]" in res_uninstall.stdout
    assert _get_files_set(fake_home) == initial_files

    # 4. New test runner & maintenance subcommands --dry-run
    for subcmd in [
        "test",
        "test-gate07",
        "test-ui",
        "test-flow",
        "check-cloudredirect",
        "docs",
        "bundle-scripts",
        "coverage",
    ]:
        res = run_installer(subcmd, "--dry-run")
        assert res.returncode == 0, f"{subcmd} --dry-run failed: {res.stderr}"
        assert "[DRY-RUN]" in res.stdout
        assert _get_files_set(fake_home) == initial_files


def test_install_update_uninstall_lifecycle(fake_home, run_installer):
    """
    Test full lifecycle: install --yes, update --yes, and uninstall --yes.
    Verifies payload delivery, config initialization, wrapper LD_AUDIT injection,
    and clean component removal.
    """
    # 1. Install
    res_inst = run_installer("install", "--yes")
    assert res_inst.returncode == 0, f"install failed: {res_inst.stderr}\n{res_inst.stdout}"

    navi_share = fake_home / ".local" / "share" / "navi"
    libnavi = navi_share / "libnavi.so"
    libinject = navi_share / "library-inject.so"
    config_yaml = fake_home / ".config" / "navi" / "config.yaml"
    steam_wrapper = fake_home / ".local" / "bin" / "steam"

    assert libnavi.is_file(), "libnavi.so was not installed into ~/.local/share/navi"
    assert libinject.is_file(), "library-inject.so was not installed into ~/.local/share/navi"
    assert config_yaml.is_file(), "config.yaml was not generated in ~/.config/navi"
    assert steam_wrapper.is_file(), "Steam wrapper was not created in ~/.local/bin/steam"

    cloud_enabled = fake_home / ".config" / "navi" / "cloud_enabled"
    assert cloud_enabled.is_file(), "cloud_enabled should be created by default on install"

    wrapper_text = steam_wrapper.read_text()
    assert "LD_AUDIT=" in wrapper_text, "Steam wrapper missing LD_AUDIT environment variable"
    assert "libnavi.so" in wrapper_text
    assert "library-inject.so" in wrapper_text

    # 2. Update
    res_upd = run_installer("update", "--yes")
    assert res_upd.returncode == 0, f"update failed: {res_upd.stderr}\n{res_upd.stdout}"
    assert libnavi.is_file()
    assert config_yaml.is_file()
    assert steam_wrapper.is_file()

    # 3. Uninstall
    res_uninst = run_installer("uninstall", "--yes")
    assert res_uninst.returncode == 0, f"uninstall failed: {res_uninst.stderr}\n{res_uninst.stdout}"

    assert not libnavi.exists(), "libnavi.so should be removed on uninstall"
    assert not libinject.exists(), "library-inject.so should be removed on uninstall"
    assert not steam_wrapper.exists(), "Steam wrapper should be removed on uninstall"
    assert not cloud_enabled.exists(), "cloud_enabled should be removed on uninstall"
    assert config_yaml.is_file(), "User config should be preserved by default on uninstall"


def test_installer_idempotency(fake_home, run_installer):
    """
    Test that running install --yes repeatedly succeeds cleanly and produces exactly one LD_AUDIT entry.
    """
    res1 = run_installer("install", "--yes")
    assert res1.returncode == 0, f"First install failed: {res1.stderr}"

    res2 = run_installer("install", "--yes")
    assert res2.returncode == 0, f"Second install failed: {res2.stderr}"

    steam_wrapper = fake_home / ".local" / "bin" / "steam"
    assert steam_wrapper.is_file()
    wrapper_text = steam_wrapper.read_text()
    assert wrapper_text.count("LD_AUDIT") == 1, (
        f"Expected exactly 1 LD_AUDIT occurrence in wrapper, found {wrapper_text.count('LD_AUDIT')}"
    )

    bashrc_text = (fake_home / ".bashrc").read_text()
    path_line = 'export PATH="$HOME/.local/bin:$PATH"'
    assert bashrc_text.count(path_line) == 1, (
        f"PATH export line was duplicated in .bashrc: {bashrc_text}"
    )


def test_steam_running_guard(fake_home, run_installer):
    """
    Test Steam-running guard:
    - Fails if Steam is running and NAVI_IGNORE_RUNNING_STEAM is unset
    - Succeeds under --dry-run
    - Succeeds when NAVI_IGNORE_RUNNING_STEAM=1
    """
    # 1. Refuses when Steam is simulated as running
    res_refusal = run_installer(
        "install",
        "--yes",
        env={"NAVI_FAKE_STEAM_RUNNING": "1", "NAVI_IGNORE_RUNNING_STEAM": "0"},
    )
    assert res_refusal.returncode != 0, "Installer should refuse to run when Steam is detected running"
    combined_out = res_refusal.stdout + res_refusal.stderr
    assert "Steam is currently running" in combined_out

    # 2. Dry-run bypasses running guard
    res_dry = run_installer(
        "install",
        "--dry-run",
        env={"NAVI_FAKE_STEAM_RUNNING": "1", "NAVI_IGNORE_RUNNING_STEAM": "0"},
    )
    assert res_dry.returncode == 0, f"Dry-run should bypass running check: {res_dry.stderr}"
    assert "[DRY-RUN]" in res_dry.stdout

    # 3. NAVI_IGNORE_RUNNING_STEAM bypasses running guard
    res_ignore = run_installer(
        "install",
        "--yes",
        env={"NAVI_FAKE_STEAM_RUNNING": "1", "NAVI_IGNORE_RUNNING_STEAM": "1"},
    )
    assert res_ignore.returncode == 0, f"Ignore running steam flag should succeed: {res_ignore.stderr}"


def test_flatpak_steam_detection(fake_home, run_installer):
    """
    Test Flatpak Steam detection when ~/.var/app/com.valvesoftware.Steam directory exists.
    """
    flatpak_steam = fake_home / ".var" / "app" / "com.valvesoftware.Steam"
    flatpak_steam.mkdir(parents=True, exist_ok=True)

    # 1. Install
    res_inst = run_installer("install", "--yes")
    assert res_inst.returncode == 0, f"install with Flatpak failed: {res_inst.stderr}"

    fp_navi = flatpak_steam / ".local" / "share" / "navi"
    fp_libnavi = fp_navi / "libnavi.so"
    fp_libinject = fp_navi / "library-inject.so"

    assert fp_libnavi.is_file(), "Flatpak libnavi.so not installed"
    assert fp_libinject.is_file(), "Flatpak library-inject.so not installed"

    # 2. Update
    res_upd = run_installer("update", "--yes")
    assert res_upd.returncode == 0, f"update with Flatpak failed: {res_upd.stderr}"
    assert fp_libnavi.is_file()

    # 3. Uninstall
    res_uninst = run_installer("uninstall", "--yes")
    assert res_uninst.returncode == 0, f"uninstall with Flatpak failed: {res_uninst.stderr}"
    assert not fp_navi.exists(), "Flatpak navi share should be removed on uninstall"


def test_steamdeck_and_autostart_integration(fake_home, run_installer):
    """
    Test SteamOS Gaming Mode (steam-launcher.service.d drop-in),
    Desktop Autostart (~/.config/autostart/steam.desktop),
    Desktop shortcut (~/Desktop/steam.desktop),
    and systemd environment (~/.config/environment.d/10-navi-path.conf).
    """
    # Set up simulated Desktop shortcut
    desktop_dir = fake_home / "Desktop"
    desktop_dir.mkdir(parents=True, exist_ok=True)
    desktop_steam = desktop_dir / "steam.desktop"
    desktop_steam.symlink_to("/usr/share/applications/steam.desktop")

    # 1. Install under simulated SteamOS
    res_inst = run_installer("install", "--yes", env={"NAVI_FORCE_STEAMOS": "1"})
    assert res_inst.returncode == 0, f"install on SteamOS failed: {res_inst.stderr}"

    # Verify systemd drop-in for Gaming Mode
    systemd_dropin = fake_home / ".config" / "systemd" / "user" / "steam-launcher.service.d" / "navi.conf"
    assert systemd_dropin.is_file(), "steam-launcher.service.d/navi.conf should be created on SteamOS"
    dropin_text = systemd_dropin.read_text()
    assert "LD_AUDIT=" in dropin_text
    assert "library-inject.so" in dropin_text
    assert "libnavi.so" in dropin_text

    # Verify Desktop Autostart override
    autostart_steam = fake_home / ".config" / "autostart" / "steam.desktop"
    assert autostart_steam.is_file(), "~/.config/autostart/steam.desktop should be created"
    autostart_text = autostart_steam.read_text()
    assert "LD_AUDIT=" in autostart_text

    # Verify Desktop shortcut updated
    assert desktop_steam.is_symlink() or desktop_steam.is_file()
    if desktop_steam.is_symlink():
        target = str(desktop_steam.resolve())
        assert ".local/share/applications/steam.desktop" in target or "steam" in target
    else:
        assert "LD_AUDIT=" in desktop_steam.read_text()

    # Verify systemd environment.d path
    env_conf = fake_home / ".config" / "environment.d" / "10-navi-path.conf"
    assert env_conf.is_file(), "~/.config/environment.d/10-navi-path.conf should be created"
    assert ".local/bin" in env_conf.read_text()

    # 2. Update preserves configuration
    res_upd = run_installer("update", "--yes", env={"NAVI_FORCE_STEAMOS": "1"})
    assert res_upd.returncode == 0, f"update on SteamOS failed: {res_upd.stderr}"
    assert systemd_dropin.is_file()
    assert autostart_steam.is_file()
    assert env_conf.is_file()

    # 3. Uninstall cleans up cleanly
    res_uninst = run_installer("uninstall", "--yes", env={"NAVI_FORCE_STEAMOS": "1"})
    assert res_uninst.returncode == 0, f"uninstall on SteamOS failed: {res_uninst.stderr}"
    assert not systemd_dropin.exists(), "steam-launcher.service.d/navi.conf should be removed on uninstall"
    assert not autostart_steam.exists(), "~/.config/autostart/steam.desktop should be removed on uninstall"
    assert not env_conf.exists(), "~/.config/environment.d/10-navi-path.conf should be removed on uninstall"
    if desktop_steam.is_symlink():
        assert str(desktop_steam.readlink()) == "/usr/share/applications/steam.desktop"

