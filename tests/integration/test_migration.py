"""
Integration tests for legacy ACCELA and SLSsteam migration and cleanup.
"""

from pathlib import Path
from tests.fixtures.legacy_migration_fixtures import (
    populate_legacy_accela,
    populate_legacy_slssteam,
    populate_legacy_steam_sh_audit,
    populate_legacy_headcrab_client_sh,
    populate_legacy_steam_sh_inline_audit,
)



def test_accela_manifest_migration_and_cleanup(fake_home, run_installer):
    """
    Test legacy ACCELA cleanup and morrenus_manifests migration:
    - ~/.local/share/ACCELA/morrenus_manifests/*.manifest is migrated to ~/.local/share/navi/manifests
    - Manifests are preserved across install, update, and uninstall
    - Original morrenus_manifests directory is preserved
    - Legacy ACCELA binaries, desktop files, helper binaries, icons, depots, and cache are cleaned up
    """
    populate_legacy_accela(fake_home)

    legacy_manifests = fake_home / ".local" / "share" / "ACCELA" / "morrenus_manifests"
    navi_manifests = fake_home / ".local" / "share" / "navi" / "manifests"

    # 1. Install & migrate
    res_inst = run_installer("install", "--yes")
    assert res_inst.returncode == 0, f"install failed during ACCELA migration: {res_inst.stderr}"

    # Manifests migrated to navi
    assert (navi_manifests / "1001.manifest").is_file()
    assert (navi_manifests / "1002.manifest").is_file()
    assert "AppState" in (navi_manifests / "1001.manifest").read_text()

    # Original manifests preserved
    assert (legacy_manifests / "1001.manifest").is_file()
    assert (legacy_manifests / "1002.manifest").is_file()

    # Legacy binaries and integration removed
    assert not (fake_home / ".local" / "share" / "ACCELA" / "accela").exists()
    assert not (fake_home / ".local" / "share" / "ACCELA" / "depots").exists()
    assert not (fake_home / ".local" / "share" / "ACCELA" / ".venv").exists()
    assert not (fake_home / ".local" / "share" / "applications" / "accela.desktop").exists()
    assert not (fake_home / ".local" / "share" / "pixmaps" / "accela.png").exists()
    assert not (fake_home / ".local" / "share" / "icons" / "hicolor" / "256x256" / "apps" / "accela.png").exists()
    assert not (fake_home / ".local" / "bin" / "accela-download").exists()
    assert not (fake_home / ".cache" / "ACCELA").exists()

    # 2. Update preserves manifests
    res_upd = run_installer("update", "--yes")
    assert res_upd.returncode == 0, f"update failed: {res_upd.stderr}"
    assert (navi_manifests / "1001.manifest").is_file()
    assert (legacy_manifests / "1001.manifest").is_file()

    # 3. Uninstall preserves manifests
    res_uninst = run_installer("uninstall", "--yes")
    assert res_uninst.returncode == 0, f"uninstall failed: {res_uninst.stderr}"
    assert (navi_manifests / "1001.manifest").is_file(), "Navi manifests must be preserved on uninstall"
    assert (legacy_manifests / "1001.manifest").is_file(), "Legacy morrenus_manifests must be preserved on uninstall"


def test_slssteam_config_migration(fake_home, run_installer):
    """
    Test legacy SLSsteam -> navi configuration migration:
    - ~/.config/SLSsteam/config.yaml is copied to ~/.config/navi/config.yaml
    - Old ~/.config/SLSsteam/config.yaml is not deleted
    - Legacy SLSsteam binaries, tracking files, and shell hooks are cleaned up
    """
    populate_legacy_slssteam(fake_home)

    res_inst = run_installer("install", "--yes")
    assert res_inst.returncode == 0, f"install failed during SLSsteam migration: {res_inst.stderr}"

    navi_cfg = fake_home / ".config" / "navi" / "config.yaml"
    old_cfg = fake_home / ".config" / "SLSsteam" / "config.yaml"

    assert navi_cfg.is_file(), "Migrated config.yaml missing in ~/.config/navi"
    assert "LegacySetting: sls_original_value" in navi_cfg.read_text()

    assert old_cfg.is_file(), "Original ~/.config/SLSsteam/config.yaml should not be deleted on install"
    assert "LegacySetting: sls_original_value" in old_cfg.read_text()

    # Verify legacy artifacts removed
    assert not (fake_home / ".local" / "share" / "SLSsteam").exists()
    assert not (fake_home / ".SLSsteam.log").exists()
    assert not (fake_home / ".SLSsteam.json").exists()
    assert not (fake_home / ".SLSsteam.installed").exists()
    assert not (fake_home / ".SLSsteam.onlinefix").exists()
    assert not (fake_home / ".SLSsteam.autocrack").exists()

    # Verify shell profile and bashrc cleaned of SLSsteam references
    bashrc_text = (fake_home / ".bashrc").read_text()
    assert "SLSsteam" not in bashrc_text

    profile_text = (fake_home / ".profile").read_text()
    assert "SLSsteam" not in profile_text


def test_slssteam_config_conflict_navi_wins(fake_home, run_installer):
    """
    Test that if both ~/.config/SLSsteam/config.yaml and ~/.config/navi/config.yaml exist,
    the navi configuration wins and is preserved.
    """
    legacy_cfg_dir = fake_home / ".config" / "SLSsteam"
    legacy_cfg_dir.mkdir(parents=True, exist_ok=True)
    (legacy_cfg_dir / "config.yaml").write_text("PriorityWinner: legacy_sls\nSafeMode: yes\n")

    navi_cfg_dir = fake_home / ".config" / "navi"
    navi_cfg_dir.mkdir(parents=True, exist_ok=True)
    (navi_cfg_dir / "config.yaml").write_text("PriorityWinner: navi_native\nSafeMode: no\n")

    res_inst = run_installer("install", "--yes")
    assert res_inst.returncode == 0, f"install failed: {res_inst.stderr}"

    navi_content = (navi_cfg_dir / "config.yaml").read_text()
    assert "PriorityWinner: navi_native" in navi_content, "Existing navi config.yaml should win"
    assert "PriorityWinner: legacy_sls" not in navi_content


def test_slssteam_config_retention_on_uninstall(fake_home, run_installer):
    """
    Test that uninstalling without --remove-configs keeps ~/.config/SLSsteam,
    while uninstalling with SLS_UNINSTALL_REMOVE_CONFIGS=1 removes it while preserving backups.
    """
    populate_legacy_slssteam(fake_home)

    run_installer("install", "--yes")

    # Uninstall without config removal
    res_uninst = run_installer("uninstall", "--yes")
    assert res_uninst.returncode == 0
    assert (fake_home / ".config" / "SLSsteam" / "config.yaml").is_file()

    # Reinstall and uninstall with config removal
    run_installer("install", "--yes")
    res_clean = run_installer("uninstall", "--yes", env={"SLS_UNINSTALL_REMOVE_CONFIGS": "1"})
    assert res_clean.returncode == 0

    assert not (fake_home / ".config" / "SLSsteam" / "config.yaml").exists()
    # Backup preserved in ~/.config/SLSsteam_backups
    backup_file = fake_home / ".config" / "SLSsteam_backups" / "updates.yaml.bak-20250101-120000"
    assert backup_file.is_file(), "updates.yaml backup was not preserved in ~/.config/SLSsteam_backups"


def test_legacy_slssteam_audit_lines_replaced(fake_home, run_installer):
    """
    Test that legacy LD_AUDIT references (e.g., SLSsteam.so in steam.sh or wrapper)
    are replaced cleanly with libnavi.so and library-inject.so.
    """
    populate_legacy_steam_sh_audit(fake_home)
    populate_legacy_slssteam(fake_home)

    res_inst = run_installer("install", "--yes")
    assert res_inst.returncode == 0, f"install failed: {res_inst.stderr}"

    # steam.sh should be restored from steam.sh.bak, eliminating legacy SLSsteam.so injection
    launcher = fake_home / ".local" / "share" / "Steam" / "steam.sh"
    launcher_text = launcher.read_text()
    assert "SLSsteam" not in launcher_text
    assert "LD_AUDIT" not in launcher_text

    # Wrapper at ~/.local/bin/steam should point to navi libraries
    wrapper = fake_home / ".local" / "bin" / "steam"
    assert wrapper.is_file()
    wrapper_text = wrapper.read_text()
    assert "SLSsteam" not in wrapper_text
    assert "libnavi.so" in wrapper_text
    assert "library-inject.so" in wrapper_text

    # Wrapper backup should have been migrated to .pre-navi.bak
    assert (fake_home / ".local" / "bin" / "steam.pre-navi.bak").is_file()
    assert not (fake_home / ".local" / "bin" / "steam.pre-slssteam.bak").exists()


def test_headcrab_steam_sh_and_client_sh_restoration(fake_home, run_installer):
    """
    Test legacy Headcrab steam.sh wrapper restoration from client.sh:
    - Headcrab renamed steam.sh to client.sh and installed a wrapper steam.sh
      defining INJECT_SLS with SLSsteam.so and sourcing client.sh.
    - Migration must restore client.sh over steam.sh, remove client.sh,
      ensure steam.sh is executable and free of SLSsteam / INJECT_SLS references,
      and not leave dual-load artifacts across install, update, and uninstall.
    """
    populate_legacy_headcrab_client_sh(fake_home)
    populate_legacy_slssteam(fake_home)

    res_inst = run_installer("install", "--yes")
    assert res_inst.returncode == 0, f"install failed: {res_inst.stderr}"

    launcher = fake_home / ".local" / "share" / "Steam" / "steam.sh"
    client_sh = fake_home / ".local" / "share" / "Steam" / "client.sh"

    assert launcher.is_file(), "steam.sh should exist"
    assert not client_sh.exists(), "client.sh should be removed after being restored over steam.sh"

    launcher_text = launcher.read_text()
    assert "Native Valve steam.sh" in launcher_text, "Original Valve content from client.sh should be restored"
    assert "SLSsteam" not in launcher_text, "steam.sh should not contain SLSsteam references"
    assert "INJECT_SLS" not in launcher_text, "steam.sh should not contain INJECT_SLS references"
    assert (launcher.stat().st_mode & 0o111) != 0, "steam.sh must be executable"

    # Verify update maintains clean steam.sh
    res_upd = run_installer("update", "--yes")
    assert res_upd.returncode == 0, f"update failed: {res_upd.stderr}"
    launcher_text_upd = launcher.read_text()
    assert "Native Valve steam.sh" in launcher_text_upd
    assert "SLSsteam" not in launcher_text_upd

    # Verify uninstall maintains clean steam.sh and doesn't re-introduce wrappers
    res_uninst = run_installer("uninstall", "--yes")
    assert res_uninst.returncode == 0, f"uninstall failed: {res_uninst.stderr}"
    launcher_text_uninst = launcher.read_text()
    assert "Native Valve steam.sh" in launcher_text_uninst
    assert "SLSsteam" not in launcher_text_uninst


def test_legacy_steam_sh_inline_audit_stripped(fake_home, run_installer):
    """
    Test legacy steam.sh inline audit stripping:
    - If steam.sh has inline SLSsteam references but no client.sh or .bak exists,
      the SLSsteam / INJECT_SLS lines should be stripped in-place and steam.sh preserved.
    """
    populate_legacy_steam_sh_inline_audit(fake_home)

    res_inst = run_installer("install", "--yes")
    assert res_inst.returncode == 0, f"install failed: {res_inst.stderr}"

    launcher = fake_home / ".local" / "share" / "Steam" / "steam.sh"
    assert launcher.is_file()
    launcher_text = launcher.read_text()
    assert "SLSsteam" not in launcher_text, "In-place stripping failed to remove SLSsteam lines"
    assert "Starting steam natively" in launcher_text, "Surrounding native code must be preserved"
    assert (launcher.stat().st_mode & 0o111) != 0, "steam.sh must remain executable"

