"""
UJ-05: Config Editor & Real-Time Validation Integration Tests.

Scenarios:
  1. Loading #sls-config-top-tab populates fields from GET /config.
  2. Editing booleans, UTF-8 international strings, and arrays with RequestBarrier
     holding in-flight save to assert intermediate saving state and single-submit lock.
  3. Boundary & invalid input handling: malformed DlcDataYaml returns HTTP 400 error
     without wiping user's form state.
  4. BackupRetentionCount clamping [1, 100] and tab switching state behavior (#sls-top-tab <-> #sls-config-top-tab).
"""

from __future__ import annotations

import json
from pathlib import Path
import pytest

from tests.integration.ui.harness.browser_driver import BrowserDriver
from tests.integration.ui.harness.callback_server_stub import CallbackServerStub
from tests.integration.ui.pages.import_config_page import ConfigEditorPage, ImportConfigPage


@pytest.fixture
def config_page(driver: BrowserDriver) -> ConfigEditorPage:
    return ConfigEditorPage(driver)


@pytest.fixture
def import_page(driver: BrowserDriver) -> ImportConfigPage:
    return ImportConfigPage(driver)


@pytest.mark.ui_p1
def test_uj05_config_loading_and_field_population(
    config_page: ConfigEditorPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Loading #sls-config-top-tab populates settings fields from GET /config."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)
    driver.inject_script(repo_paths["scripts"] / "import-lua-script.js")

    # Click config top tab
    config_page.open()
    assert config_page.is_open() is True

    # Assert populated fields match CallbackServerStub defaults
    driver.wait_for_condition(
        lambda: config_page.get_field_value("FakeName") == "Gordon Freeman",
        timeout=5.0,
        error_message="FakeName field did not populate with stub data",
    )
    assert config_page.get_field_value("FakeEmail") == "gordon@blackmesa.gov"
    assert config_page.is_toggle_checked("DisableFamilyShareLock") is False
    assert config_page.is_toggle_checked("SafeMode") is True
    assert config_page.get_field_value("CloudRetention") == "5"

    # Dirty pill is clean
    assert "saved" in config_page.get_dirty_pill_text().lower()
    assert config_page.is_dirty() is False
    assert config_page.is_discard_visible() is False


@pytest.mark.ui_p1
def test_uj05_config_editing_barrier_saving_state_and_lock(
    config_page: ConfigEditorPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Editing booleans, UTF-8 strings, and arrays with RequestBarrier asserting saving lock."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)
    driver.inject_script(repo_paths["scripts"] / "import-lua-script.js")
    config_page.open()

    driver.wait_for_condition(lambda: bool(config_page.get_field_value("FakeName")))

    # 1. Edit boolean, international UTF-8 name, and AppIds
    config_page.switch_tab("sls-cfg-sec-general")
    config_page.set_toggle("DisableFamilyShareLock", True)

    config_page.switch_tab("sls-cfg-sec-profile")
    unicode_name = "Гордон Фримен 日本語 🎮"
    config_page.set_field_value("FakeName", unicode_name)

    config_page.switch_tab("sls-cfg-sec-overrides")
    config_page.set_field_value("AppIds", "440, 730, 1443430")

    # Form is marked dirty
    assert config_page.is_dirty() is True
    assert config_page.is_discard_visible() is True

    # 2. Hold in-flight save request using RequestBarrier
    barrier = callback_server.hold_endpoint("/config/full")

    config_page.click_save()
    barrier.wait_until_reached(timeout=5.0)

    # 3. Assert intermediate saving state: single-submit lock is active
    assert config_page.is_save_disabled() is True
    assert "saving" in config_page.get_save_btn_text().lower()

    # 4. Release barrier to finish save
    barrier.release()

    # 5. Assert save completion
    config_page.wait_for_status_banner("saved", timeout=5.0)
    assert config_page.is_save_disabled() is True
    assert not config_page.is_dirty()
    assert config_page.is_discard_visible() is False

    # 6. Verify payload received by stub
    req = callback_server.last_request("/config/full", method="POST")
    assert req is not None
    body_data = json.loads(req["body"].decode("utf-8"))
    assert body_data.get("DisableFamilyShareLock") is True
    assert body_data.get("FakeName") == unicode_name
    assert 1443430 in body_data.get("AppIds", [])


@pytest.mark.ui_p1
def test_uj05_invalid_input_dlc_yaml_error_without_wiping_form(
    config_page: ConfigEditorPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Malformed DlcDataYaml returns HTTP 400 error without wiping user form edits."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)
    driver.inject_script(repo_paths["scripts"] / "import-lua-script.js")
    config_page.open()

    driver.wait_for_condition(lambda: bool(config_page.get_field_value("FakeName")))

    # Edit FakeName and enter invalid YAML
    config_page.switch_tab("sls-cfg-sec-profile")
    config_page.set_field_value("FakeName", "Custom Name Preserved")

    config_page.switch_tab("sls-cfg-sec-overrides")
    malformed_yaml = ": - malformed ::: [invalid {"
    config_page.set_field_value("DlcDataYaml", malformed_yaml)

    # Attempt to save
    config_page.click_save()

    # Stub rejects with 400 Bad Request
    config_page.wait_for_status_banner("Failed to parse DlcDataYaml", timeout=5.0)

    # Assert form state is preserved and NOT wiped out
    assert config_page.get_field_value("DlcDataYaml") == malformed_yaml
    config_page.switch_tab("sls-cfg-sec-profile")
    assert config_page.get_field_value("FakeName") == "Custom Name Preserved"
    assert config_page.is_dirty() is True


@pytest.mark.ui_p1
def test_uj05_backup_retention_clamping_and_tab_switching(
    config_page: ConfigEditorPage,
    import_page: ImportConfigPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """BackupRetentionCount clamping [1, 100] and tab switching state behavior."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)
    driver.inject_script(repo_paths["scripts"] / "import-lua-script.js")
    config_page.open()

    driver.wait_for_condition(lambda: bool(config_page.get_field_value("FakeName")))

    # 1. Test lower bound clamping (0 -> 1)
    config_page.switch_tab("sls-cfg-sec-cloudsaves")
    config_page.set_field_value("CloudRetention", "0")
    config_page.click_save()
    config_page.wait_for_status_banner("saved", timeout=5.0)

    req = callback_server.last_request("/config/full", method="POST")
    assert req is not None
    body_data = json.loads(req["body"].decode("utf-8"))
    assert body_data["CloudSaves"]["BackupRetentionCount"] == 0  # input was 0
    # But server stub clamped it:
    assert callback_server.config_data["CloudSaves"]["BackupRetentionCount"] == 1

    # 2. Test upper bound clamping (999 -> 100)
    config_page.set_field_value("CloudRetention", "999")
    config_page.click_save()
    config_page.wait_for_status_banner("saved", timeout=5.0)
    assert callback_server.config_data["CloudSaves"]["BackupRetentionCount"] == 100

    # 3. Tab switching state behavior (#sls-top-tab <-> #sls-config-top-tab)
    # Switch to Import Lua tab
    driver.click("#sls-top-tab")
    import_page.wait_until_open()
    assert config_page.is_open() is False

    # Switch back to Navi Config tab
    driver.click("#sls-config-top-tab")
    config_page.wait_until_open()
    assert import_page.is_open() is False


@pytest.mark.ui_p1
def test_uj05_cloudredirect_ux_parity_controls(
    config_page: ConfigEditorPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Verify CloudRedirect v2.6.5 UX parity cards (Notifications, Stats Sync, R2/S3 Creds, Migration, Orphans, Backups)."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    driver.navigate_to_file(shell_path)
    driver.inject_script(repo_paths["scripts"] / "cloud_manager.js")
    driver.inject_script(repo_paths["scripts"] / "import-lua-script.js")
    config_page.open()

    driver.wait_for_condition(lambda: bool(config_page.get_field_value("FakeName")))
    config_page.switch_tab("sls-cfg-sec-cloudsaves")

    # 1. Verify Launch Notifications & Stats Sync checkboxes exist and round-trip through /config/full
    assert config_page.is_toggle_checked("CloudSaves-NotificationsEnabled") is True
    assert config_page.is_toggle_checked("StatsSync-Enabled") is False

    config_page.set_toggle("CloudSaves-NotificationsEnabled", False)
    config_page.set_toggle("StatsSync-Enabled", True)
    config_page.set_toggle("StatsSync-Playtime", False)
    config_page.click_save()
    config_page.wait_for_status_banner("saved", timeout=5.0)

    req = callback_server.last_request("/config/full", method="POST")
    assert req is not None
    body_data = json.loads(req["body"].decode("utf-8"))
    assert body_data["CloudSaves"]["NotificationsEnabled"] is False
    assert body_data["CloudSaves"]["StatsSync"]["Enabled"] is True
    assert body_data["CloudSaves"]["StatsSync"]["SyncPlaytime"] is False

    # 2. Verify R2 / S3 Credentials form visibility when selecting r2 vs s3
    driver.evaluate(
        """
        (() => {
            const sel = document.getElementById('sls-cfg-cs-provider');
            sel.value = 'r2';
            sel.dispatchEvent(new Event('change', { bubbles: true }));
        })()
        """
    )
    driver.wait_for_condition(
        lambda: driver.evaluate("document.getElementById('sls-cfg-cs-creds-form').style.display !== 'none'"),
        timeout=5.0,
    )
    assert driver.evaluate("document.getElementById('sls-cfg-cs-cred-account-id-wrap').style.display !== 'none'") is True

    # 3. Verify Migration Scan button updates status
    driver.click("#sls-cfg-cs-migrate-scan-btn")
    driver.wait_for_condition(
        lambda: "ready to migrate" in (driver.evaluate("document.getElementById('sls-cfg-cs-migrate-status').textContent") or ""),
        timeout=5.0,
    )

    # 4. Verify Scan Orphans button renders orphan panel
    driver.click("#sls-cfg-cs-scan-orphans-btn")
    driver.wait_for_condition(
        lambda: "480" in (driver.evaluate("document.getElementById('sls-cfg-cs-orphans-panel').textContent") or ""),
        timeout=5.0,
    )

    # 5. Verify Global Backups table renders stub backup entry
    driver.wait_for_condition(
        lambda: "backup_107410_auto_1.tar.gz" in (driver.evaluate("document.getElementById('sls-cfg-cs-backups-list').textContent") or ""),
        timeout=5.0,
    )

