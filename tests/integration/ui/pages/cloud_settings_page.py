"""
@file  cloud_settings_page.py
@brief Page object representing the CloudRedirect settings view


Page Object Model for Cloud Manager modals.

Encapsulates:
  - CloudSettingsModal (#sls-cloud-settings-modal)
  - CloudResetModal (#sls-reset-modal)
  - CloudRestoreModal (#sls-restore-modal)
From res/inject-scripts/cloud_manager.js.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import TYPE_CHECKING, Any, Dict, List, Optional

from .base_modal_page import BaseModalPage

if TYPE_CHECKING:
    from tests.integration.ui.harness.browser_driver import BrowserDriver


class CloudSettingsModal(BaseModalPage):
    """Encapsulates the global Cloud Saves Manager modal (#sls-cloud-settings-modal)."""

    def __init__(self, driver: "BrowserDriver"):
        super().__init__(
            driver=driver,
            root_selector="#sls-cloud-settings-modal",
            close_selector="#sls-settings-close",
            cancel_selector="#sls-cloud-settings-modal #sls-modal-cancel",
        )

    def open(self) -> "CloudSettingsModal":
        self.driver.evaluate("window.NaviCloudManager.openSettingsModal();")
        self.wait_until_open()
        return self

    # --- Master Toggle ---

    def get_master_toggle_text(self) -> str:
        expr = "document.getElementById('sls-master-toggle-btn') ? document.getElementById('sls-master-toggle-btn').innerText.trim() : ''"
        return str(self.driver.evaluate(expr, await_promise=False) or "")

    def is_master_toggle_enabled(self) -> bool:
        return self.get_master_toggle_text().lower() == "enabled"

    def click_master_toggle(self) -> None:
        self.driver.click("#sls-master-toggle-btn")

    def wait_for_master_toggle_text(self, expected_text: str, timeout: float = 5.0) -> None:
        self.driver.wait_for_condition(
            lambda: expected_text.lower() in self.get_master_toggle_text().lower(),
            timeout=timeout,
            error_message=f"Master toggle text did not become '{expected_text}' within {timeout}s (current: '{self.get_master_toggle_text()}')",
        )

    # --- Provider Selection ---

    def select_provider(self, provider_id: str) -> None:
        expr = (
            f"(function() {{"
            f"    var select = document.getElementById('sls-provider-select');"
            f"    if (!select) return false;"
            f"    select.value = {json.dumps(provider_id)};"
            f"    select.dispatchEvent(new Event('change', {{ bubbles: true }}));"
            f"    return true;"
            f"}})()"
        )
        self.driver.evaluate(expr, await_promise=False)

    def get_selected_provider(self) -> str:
        expr = "document.getElementById('sls-provider-select') ? document.getElementById('sls-provider-select').value : ''"
        return str(self.driver.evaluate(expr, await_promise=False) or "")

    # --- Auth Badge & Email ---

    def get_auth_badge_text(self) -> str:
        expr = "document.getElementById('sls-auth-badge') ? document.getElementById('sls-auth-badge').innerText.trim() : ''"
        return str(self.driver.evaluate(expr, await_promise=False) or "")

    def wait_for_auth_badge_text(self, expected_text: str, timeout: float = 5.0) -> None:
        def match() -> bool:
            curr = self.get_auth_badge_text().lower()
            exp = expected_text.lower()
            if exp == "connected" or exp == "connected ✓":
                return "connected" in curr and "not connected" not in curr
            return exp in curr

        self.driver.wait_for_condition(
            match,
            timeout=timeout,
            error_message=f"Auth badge text did not match '{expected_text}' within {timeout}s (current: '{self.get_auth_badge_text()}')",
        )

    def get_auth_email_text(self) -> str:
        expr = "document.getElementById('sls-auth-email') ? document.getElementById('sls-auth-email').innerText.trim() : ''"
        return str(self.driver.evaluate(expr, await_promise=False) or "")

    # --- Auth Actions (Connect / Disconnect) ---

    def has_connect_button(self) -> bool:
        expr = (
            "(function() {"
            "    var btn = document.querySelector('#sls-auth-actions button');"
            "    return btn && btn.innerText.indexOf('Connect') !== -1;"
            "})()"
        )
        return bool(self.driver.evaluate(expr, await_promise=False))

    def has_disconnect_button(self) -> bool:
        expr = (
            "(function() {"
            "    var btn = document.querySelector('#sls-auth-actions button');"
            "    return btn && btn.innerText.indexOf('Disconnect') !== -1;"
            "})()"
        )
        return bool(self.driver.evaluate(expr, await_promise=False))

    def wait_for_disconnect_button(self, timeout: float = 5.0) -> None:
        self.driver.wait_for_condition(
            self.has_disconnect_button,
            timeout=timeout,
            error_message=f"Disconnect button did not appear within {timeout}s",
        )

    def wait_for_connect_button(self, timeout: float = 5.0) -> None:
        self.driver.wait_for_condition(
            self.has_connect_button,
            timeout=timeout,
            error_message=f"Connect button did not appear within {timeout}s",
        )

    def click_connect(self) -> None:
        self.driver.evaluate(
            "(function() {"
            "    var btn = document.querySelector('#sls-auth-actions button');"
            "    if (btn) btn.click();"
            "})()"
        )

    def click_disconnect(self) -> None:
        self.driver.evaluate(
            "(function() {"
            "    var btn = document.querySelector('#sls-auth-actions button');"
            "    if (btn) btn.click();"
            "})()"
        )


class CloudResetModal(BaseModalPage):
    """Encapsulates the App Save Reset modal (#sls-reset-modal)."""

    def __init__(self, driver: "BrowserDriver"):
        super().__init__(
            driver=driver,
            root_selector="#sls-reset-modal",
            close_selector="#sls-reset-close",
            cancel_selector="#sls-reset-modal #sls-modal-cancel",
        )

    def open(self, appid: int = 1443430) -> "CloudResetModal":
        self.driver.evaluate(f"window.NaviCloudManager.resetAppSave({appid});")
        self.wait_until_open()
        return self

    # --- Input Typing ---

    def type_input(self, text: str) -> None:
        self.driver.type_text("#sls-reset-input", text)

    def clear_and_type_input(self, text: str) -> None:
        self.driver.clear_and_type("#sls-reset-input", text)

    def press_backspace(self) -> None:
        self.driver.press_key("Backspace")

    def get_input_value(self) -> str:
        expr = "document.getElementById('sls-reset-input') ? document.getElementById('sls-reset-input').value : ''"
        return str(self.driver.evaluate(expr, await_promise=False) or "")

    # --- Confirm Button State ---

    def is_confirm_disabled(self) -> bool:
        expr = "document.getElementById('sls-modal-confirm') ? !!document.getElementById('sls-modal-confirm').disabled : true"
        return bool(self.driver.evaluate(expr, await_promise=False))

    def get_confirm_text(self) -> str:
        expr = "document.getElementById('sls-modal-confirm') ? document.getElementById('sls-modal-confirm').innerText.trim() : ''"
        return str(self.driver.evaluate(expr, await_promise=False) or "")

    def get_confirm_opacity(self) -> float:
        expr = (
            "(function() {"
            "    var el = document.getElementById('sls-modal-confirm');"
            "    if (!el) return 0;"
            "    return parseFloat(window.getComputedStyle(el).opacity || el.style.opacity || '1');"
            "})()"
        )
        val = self.driver.evaluate(expr, await_promise=False)
        return float(val) if val is not None else 0.0

    def click_confirm(self) -> None:
        self.driver.click("#sls-modal-confirm")

    def wait_for_confirm_enabled(self, timeout: float = 5.0) -> None:
        self.driver.wait_for_condition(
            lambda: not self.is_confirm_disabled(),
            timeout=timeout,
            error_message=f"Confirm button did not enable within {timeout}s",
        )

    def wait_for_confirm_disabled(self, timeout: float = 5.0) -> None:
        self.driver.wait_for_condition(
            self.is_confirm_disabled,
            timeout=timeout,
            error_message=f"Confirm button did not disable within {timeout}s",
        )

    def wait_for_confirm_text(self, expected_text: str, timeout: float = 5.0) -> None:
        self.driver.wait_for_condition(
            lambda: expected_text.lower() in self.get_confirm_text().lower(),
            timeout=timeout,
            error_message=f"Confirm button text did not contain '{expected_text}' within {timeout}s",
        )


class CloudRestoreModal(BaseModalPage):
    """Encapsulates the App Save Restore modal (#sls-restore-modal)."""

    def __init__(self, driver: "BrowserDriver"):
        super().__init__(
            driver=driver,
            root_selector="#sls-restore-modal",
            close_selector="#sls-restore-close",
            cancel_selector="#sls-restore-modal #sls-modal-cancel",
        )

    def open(self, appid: int = 1443430) -> "CloudRestoreModal":
        self.driver.evaluate(f"window.NaviCloudManager.openRestoreModal({appid});")
        self.wait_until_open()
        return self

    def get_body_text(self) -> str:
        expr = "document.getElementById('sls-restore-body') ? document.getElementById('sls-restore-body').innerText.trim() : ''"
        return str(self.driver.evaluate(expr, await_promise=False) or "")


class CloudSettingsPage:
    """Unified entry point for Cloud Manager UI interactions."""

    def __init__(self, driver: "BrowserDriver"):
        self.driver = driver
        self.settings_modal = CloudSettingsModal(driver)
        self.reset_modal = CloudResetModal(driver)
        self.restore_modal = CloudRestoreModal(driver)

    def inject_cloud_manager(self, script_path: str | Path) -> None:
        self.driver.inject_script(script_path)
        self.driver.wait_for_condition(
            lambda: bool(self.driver.evaluate("typeof window.NaviCloudManager !== 'undefined'", await_promise=False)),
            timeout=5.0,
            error_message="NaviCloudManager failed to initialize after script injection",
        )
