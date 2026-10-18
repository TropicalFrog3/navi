"""
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
