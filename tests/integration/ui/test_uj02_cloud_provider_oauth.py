"""
@file  test_uj02_cloud_provider_oauth.py
@brief User journey test for cloud provider OAuth login and feedback


UJ-02: Cloud Provider Configuration and OAuth Lifecycle Integration Tests.

Scenarios:
  1. Initial load transitions: #sls-master-toggle-btn and #sls-auth-badge transition from "Checking..." to live state.
  2. Master toggle: POST /cloud/toggle real-time state flip, barrier intermediate assertion, and error recovery.
  3. Switching providers: local-only, gdrive, onedrive, r2, s3 update auth block and badge.
  4. OAuth lifecycle: Sign-in happy path (200 with authUrl), 409 Conflict error handling, and Sign-out.
"""

from __future__ import annotations

from pathlib import Path
import pytest

from tests.integration.ui.harness.browser_driver import BrowserDriver
from tests.integration.ui.harness.callback_server_stub import CallbackServerStub
from tests.integration.ui.pages.cloud_settings_page import CloudSettingsModal, CloudSettingsPage


@pytest.fixture
def setup_library_shell(driver: BrowserDriver, repo_paths: dict[str, Path]) -> CloudSettingsPage:
    """Navigate to library fixture shell and inject cloud_manager.js."""
    driver.navigate_to_file(repo_paths["fixtures"] / "steam_library_sp.html")
    page = CloudSettingsPage(driver)
    page.inject_cloud_manager(repo_paths["scripts"] / "cloud_manager.js")
    return page


@pytest.mark.ui_p0
@pytest.mark.a11y
def test_uj02_initial_load_transitions(
    setup_library_shell: CloudSettingsPage,
    callback_server: CallbackServerStub,
) -> None:
    """Initial modal load transitions master toggle and auth badge from 'Checking...' to live state."""
    page = setup_library_shell
    modal = page.settings_modal
    modal.open()

    # Assert ARIA attributes on modal and live status region
    assert modal.get_role() == "dialog"
    assert modal.get_aria_modal() == "true"
    assert modal.get_data_testid() == "sls-cloud-settings-modal"
    assert modal.get_role("#sls-auth-badge") == "status"
    assert modal.get_attribute("aria-live", "#sls-auth-badge") == "polite"

    # Verify transition from initial state to live server values
    modal.wait_for_master_toggle_text("Enabled")
    assert modal.is_master_toggle_enabled() is True

    modal.wait_for_auth_badge_text("Connected")

    modal.click_close()
    modal.wait_until_closed()


@pytest.mark.ui_p0
def test_uj02_master_toggle_state_flip_barrier_and_error(
    setup_library_shell: CloudSettingsPage,
    callback_server: CallbackServerStub,
) -> None:
    """Master toggle state flip with deterministic barrier and error recovery on HTTP 500."""
    page = setup_library_shell
    modal = page.settings_modal
    modal.open()
    modal.wait_for_master_toggle_text("Enabled")

    # Step 1: Hold POST /cloud/toggle on barrier
    callback_server.clear_requests()
    barrier = callback_server.hold_endpoint("/cloud/toggle")

    modal.click_master_toggle()
    barrier.wait_until_reached(timeout=5.0)

    # Release and verify toggle flipped to Disabled
    barrier.release()
    modal.wait_for_master_toggle_text("Disabled")
    assert modal.is_master_toggle_enabled() is False

    # Step 2: Toggle back to Enabled
    modal.click_master_toggle()
    modal.wait_for_master_toggle_text("Enabled")
    assert modal.is_master_toggle_enabled() is True

    # Step 3: Error recovery on HTTP 500 fault
    callback_server.set_fault("/cloud/toggle", status_code=500, body="Internal Server Error: 500")
    modal.click_master_toggle()

    # State must remain Enabled despite network error
    modal.wait_for_master_toggle_text("Enabled")
    assert modal.is_master_toggle_enabled() is True

    modal.click_close()
    modal.wait_until_closed()


@pytest.mark.ui_p0
def test_uj02_switching_providers_updates_ui(
    setup_library_shell: CloudSettingsPage,
    callback_server: CallbackServerStub,
) -> None:
    """Switching providers (local-only, gdrive, onedrive, r2, s3) updates auth block and badge."""
    page = setup_library_shell
    modal = page.settings_modal
    modal.open()

    providers = ["local-only", "gdrive", "onedrive", "r2", "s3"]

    for provider in providers:
        modal.select_provider(provider)
        assert modal.get_selected_provider() == provider

        # Local and currently active provider report Connected
        modal.wait_for_auth_badge_text("Connected")

        if provider == "gdrive":
            # Connected gdrive shows account email and Disconnect button
            assert "deck@local" in modal.get_auth_email_text()
            assert modal.has_disconnect_button() is True
        elif provider in ["r2", "s3", "local-only"]:
            # Non-OAuth providers do not have OAuth action buttons
            assert modal.has_connect_button() is False
            assert modal.has_disconnect_button() is False

    modal.click_close()
    modal.wait_until_closed()


@pytest.mark.ui_p0
def test_uj02_oauth_signin_and_signout(
    setup_library_shell: CloudSettingsPage,
    callback_server: CallbackServerStub,
    driver: BrowserDriver,
) -> None:
    """OAuth lifecycle: 409 Conflict handling, happy path sign-in with authUrl, and sign-out."""
    page = setup_library_shell
    modal = page.settings_modal

    # Configure server with unauthenticated state
    callback_server.cloud_authenticated = False
    callback_server.cloud_provider = "gdrive"

    modal.open()
    modal.select_provider("gdrive")
    modal.wait_for_auth_badge_text("Not Connected")
    assert modal.has_connect_button() is True

    # Step 1: 409 Conflict error handling on sign-in
    callback_server.set_fault(
        "/cloud/provider/signin",
        status_code=409,
        body="Conflict: Another signin process is already active",
    )
    modal.click_connect()
    # Ensure error is handled without crashing modal
    assert modal.is_open() is True

    # Step 2: Happy path OAuth sign-in with authUrl
    callback_server.clear_requests()
    driver.clear_opened_urls()

    modal.click_connect()

    # Verify request was received
    driver.wait_for_condition(
        lambda: len(callback_server.get_requests("/cloud/provider/signin", method="POST")) > 0,
        timeout=5.0,
        error_message="POST /cloud/provider/signin was not dispatched",
    )

    # Verify window.open was triggered with the returned authUrl
    driver.wait_for_condition(
        lambda: any("accounts.google.com" in url for url in driver.get_opened_urls()),
        timeout=5.0,
        error_message="window.open was not called with Google OAuth URL",
    )

    # Force a refresh to simulate background poll completing auth
    modal.select_provider("local-only")
    modal.select_provider("gdrive")
    modal.wait_for_disconnect_button()
    assert modal.has_disconnect_button() is True
    assert "deck@local" in modal.get_auth_email_text()

    # Step 3: OAuth Sign-Out
    callback_server.clear_requests()
    modal.click_disconnect()

    # Verify signout request reached server
    driver.wait_for_condition(
        lambda: len(callback_server.get_requests("/cloud/provider/signout", method="POST")) > 0,
        timeout=5.0,
        error_message="POST /cloud/provider/signout was not dispatched",
    )

    modal.wait_for_connect_button()
    modal.wait_for_auth_badge_text("Not Connected")
    assert modal.has_connect_button() is True

    modal.click_close()
    modal.wait_until_closed()
