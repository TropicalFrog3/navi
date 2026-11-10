"""
@file  test_uj06_library_app_actions.py
@brief User journey test verifying library action buttons trigger events


UJ-06: Library App Details Actions, Cloud Backups & Error Rollback Integration Tests.

Scenarios:
  1. Online-Fix (#sls-btn-fix) and Auto-Crack (#sls-btn-crack) real-time toggle transitions
     with RequestBarrier intermediate state assertions.
  2. Per-game Cloud Redirect toggle (/cloud/apps/1443430/toggle), manual backup
     (POST /cloud/apps/1443430/backup) updating #sls-restore-modal list in real time,
     and restore queuing (POST /cloud/apps/1443430/restore -> 202 Accepted).
  3. Error recovery / rollback when toggle or restore returns HTTP 401 or 500.
  4. Remove Lua modal (#sls-remove-modal) cancel vs confirm flow (/remove?id=1443430).
"""

from __future__ import annotations

import json
from pathlib import Path
import pytest

from tests.integration.ui.harness.browser_driver import BrowserDriver
from tests.integration.ui.harness.callback_server_stub import CallbackServerStub
from tests.integration.ui.pages.library_details_page import LibraryDetailsPage


@pytest.fixture
def library_page(driver: BrowserDriver) -> LibraryDetailsPage:
    return LibraryDetailsPage(driver)


@pytest.mark.ui_p1
def test_uj06_online_fix_and_autocrack_transitions_with_barriers(
    library_page: LibraryDetailsPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Online-Fix and Auto-Crack toggle transitions with RequestBarrier intermediate state assertions."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    library_page.navigate_library(shell_path, appid=1443430)
    library_page.enable_desktop_ui(appid=1443430)
    library_page.inject_app_details_script(repo_paths["scripts"] / "app-details-script.js")
    library_page.wait_for_injected_buttons()

    # Open SLS Game Manager modal
    modal = library_page.click_config_button()
    modal.wait_until_open()

    # 1. Initial status: neither Fix nor Crack installed
    modal.wait_for_fix_text("Install Online-Fix")
    modal.wait_for_crack_text("Install AutoCrack")

    # 2. Test Install Online-Fix with RequestBarrier
    barrier_install_fix = callback_server.hold_endpoint("/install-fix")
    modal.click_fix_button()
    barrier_install_fix.wait_until_reached(timeout=5.0)

    # Intermediate state assertion
    assert "installing" in modal.get_fix_button_text().lower()

    # Release barrier and assert final state
    barrier_install_fix.release()
    modal.wait_for_fix_text("Remove Online-Fix")

    # 3. Test Remove Online-Fix with RequestBarrier
    barrier_remove_fix = callback_server.hold_endpoint("/remove-fix")
    modal.click_fix_button()
    barrier_remove_fix.wait_until_reached(timeout=5.0)

    # Intermediate state assertion
    assert "removing" in modal.get_fix_button_text().lower()

    # Release barrier
    barrier_remove_fix.release()
    modal.wait_for_fix_text("Install Online-Fix")

    # 4. Test Install AutoCrack with RequestBarrier
    barrier_install_crack = callback_server.hold_endpoint("/install-crack")
    modal.click_crack_button()
    barrier_install_crack.wait_until_reached(timeout=5.0)

    # Intermediate state assertion
    assert "installing" in modal.get_crack_button_text().lower()

    # Release barrier
    barrier_install_crack.release()
    modal.wait_for_crack_text("Remove AutoCrack")

    # Clean up modal
    modal.click_close()
    modal.wait_until_closed()


@pytest.mark.ui_p1
def test_uj06_cloud_redirect_toggle_manual_backup_and_restore_queuing(
    library_page: LibraryDetailsPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Per-game Cloud Redirect toggle, manual backup updating restore modal, and restore queuing."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    library_page.navigate_library(shell_path, appid=1443430)
    library_page.enable_desktop_ui(appid=1443430)
    library_page.inject_app_details_script(repo_paths["scripts"] / "app-details-script.js")
    library_page.wait_for_injected_buttons()

    # 1. Initial Cloud Save is Active
    library_page.wait_for_cloud_toggle_text("Active")

    # 2. Toggle to Inactive
    library_page.click_cloud_toggle_button()
    library_page.wait_for_cloud_toggle_text("Inactive")
    assert callback_server.app_cloud_redirected.get(1443430) is False

    # Toggle back to Active
    library_page.click_cloud_toggle_button()
    library_page.wait_for_cloud_toggle_text("Active")
    assert callback_server.app_cloud_redirected.get(1443430) is True

    # 3. Trigger manual backup
    library_page.click_cloud_backup_button()
    library_page.wait_for_cloud_backup_text("Saved!")
    req_backup = callback_server.last_request("/cloud/apps/1443430/backup", method="POST")
    assert req_backup is not None

    # 4. Open Restore modal and verify real-time updated backup list
    restore_modal = library_page.click_cloud_restore_button()
    restore_modal.wait_until_open()
    restore_modal.wait_for_backups_count(min_count=2)
    assert restore_modal.get_backups_count() >= 2

    # 5. Restore queuing (POST /cloud/apps/1443430/restore -> 202 Accepted)
    restore_modal.click_restore_backup(0)

    # Modal closes upon successful restore queue
    restore_modal.wait_until_closed(timeout=5.0)

    req_restore = callback_server.last_request("/cloud/apps/1443430/restore", method="POST")
    assert req_restore is not None


@pytest.mark.ui_p1
def test_uj06_error_recovery_and_rollback_on_server_fault(
    library_page: LibraryDetailsPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Error recovery / rollback when toggle or restore returns HTTP 401 or 500."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    library_page.navigate_library(shell_path, appid=1443430)
    library_page.enable_desktop_ui(appid=1443430)
    driver.inject_script(repo_paths["scripts"] / "cloud_manager.js")
    library_page.inject_app_details_script(repo_paths["scripts"] / "app-details-script.js")
    library_page.wait_for_injected_buttons()

    library_page.wait_for_cloud_toggle_text("Active")

    # 1. Fault injection on toggle (HTTP 500)
    callback_server.set_fault("/cloud/apps/1443430/toggle", status_code=500, body="Internal Server Error", count=1)
    library_page.click_cloud_toggle_button()

    # The toggle attempts update and rolls back to previous Active state
    library_page.wait_for_cloud_toggle_text("Active")

    # 2. Fault injection on restore (HTTP 401 Unauthorized)
    callback_server.set_fault("/cloud/apps/1443430/restore", status_code=401, body="Unauthorized", count=1)
    restore_modal = library_page.click_cloud_restore_button()
    restore_modal.wait_until_open()
    restore_modal.wait_for_backups_count(min_count=1)

    # Click restore on backup 0
    restore_modal.click_restore_backup(0)

    # Error recovery: modal remains open and button is re-enabled with "Restore" text
    driver.wait_for_condition(
        lambda: driver.evaluate(
            "(function() {"
            "    var btn = document.querySelector('#sls-restore-body .sls-btn-restore-item');"
            "    return btn && !btn.disabled && btn.textContent.trim() === 'Restore';"
            "})()"
        ),
        timeout=5.0,
        error_message="Restore button did not re-enable after HTTP 401 fault",
    )
    assert restore_modal.is_open() is True

    # User can safely cancel out
    restore_modal.click_close()
    restore_modal.wait_until_closed()


@pytest.mark.ui_p1
def test_uj06_remove_lua_modal_cancel_vs_confirm(
    library_page: LibraryDetailsPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
    repo_paths: dict[str, Path],
) -> None:
    """Remove Lua modal cancel vs confirm flow (/remove?id=1443430)."""
    shell_path = repo_paths["fixtures"] / "steam_library_sp.html"
    library_page.navigate_library(shell_path, appid=1443430)
    library_page.enable_desktop_ui(appid=1443430)
    library_page.inject_app_details_script(repo_paths["scripts"] / "app-details-script.js")
    library_page.wait_for_injected_buttons()

    # 1. Cancel flow: opens modal and cancels without issuing /remove
    remove_modal = library_page.click_remove_lua_button()
    remove_modal.wait_until_open()
    remove_modal.click_cancel()
    remove_modal.wait_until_closed()

    assert callback_server.last_request("/remove") is None

    # 2. Confirm flow: opens modal and clicks Remove confirm
    remove_modal = library_page.click_remove_lua_button()
    remove_modal.wait_until_open()
    remove_modal.click_confirm()
    remove_modal.wait_until_closed()

    # /remove request was recorded
    driver.wait_for_condition(
        lambda: callback_server.last_request("/remove") is not None,
        timeout=5.0,
        error_message="/remove request was not recorded by server stub",
    )
    req = callback_server.last_request("/remove")
    assert req["query"].get("id") == ["1443430"]

    # In desktop mode, button transitions to "Restart Steam..."
    driver.wait_for_condition(
        lambda: "restart" in library_page.get_remove_lua_button_text().lower(),
        timeout=5.0,
        error_message="Remove button did not transition to Restart Steam...",
    )
