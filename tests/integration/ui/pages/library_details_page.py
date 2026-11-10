"""
@file  library_details_page.py
@brief Page object representing the Steam library game details view


Page Object Model for Steam Library App Details and Injected Modals.

Encapsulates:
  - Big Picture / Deck "Navi" button (.sls-navi-btn) and Navi modal (#sls-navi-modal)
  - Desktop UI buttons (.sls-config-btn, .sls-cloud-toggle-btn, .sls-cloud-backup-btn, .sls-cloud-restore-btn, .sls-remove-lua-btn)
  - Game Manager Modal (#sls-overlay-modal) with Online-Fix and AutoCrack actions
  - Restore Modal (#sls-restore-modal) with per-game backup snapshot selection
  - Remove Lua Modal (#sls-remove-modal)
  - Reset Save Modal (#sls-reset-modal)
From res/inject-scripts/app-details-script.js.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import TYPE_CHECKING, Any, Dict, List, Optional

from .base_modal_page import BaseModalPage

if TYPE_CHECKING:
    from tests.integration.ui.harness.browser_driver import BrowserDriver


class GameManagerModal(BaseModalPage):
    """Encapsulates the SLS Game Manager modal (#sls-overlay-modal)."""

    def __init__(self, driver: "BrowserDriver"):
        super().__init__(
            driver=driver,
            root_selector="#sls-overlay-modal",
            close_selector="#sls-close-x",
            cancel_selector="#sls-btn-cancel",
        )
        self.fix_btn_selector = "#sls-btn-fix"
        self.crack_btn_selector = "#sls-btn-crack"

    def get_fix_button_text(self) -> str:
        expr = (
            f"(function() {{"
            f"    var btn = document.querySelector({json.dumps(self.fix_btn_selector)});"
            f"    return btn ? btn.innerText.trim() : '';"
            f"}})()"
        )
        return str(self.driver.evaluate(expr, await_promise=False) or "")

    def click_fix_button(self) -> None:
        self.driver.click(self.fix_btn_selector)

    def wait_for_fix_text(self, text: str, timeout: float = 5.0) -> None:
        self.driver.wait_for_condition(
            lambda: text.lower() in self.get_fix_button_text().lower(),
            timeout=timeout,
            error_message=f"Fix button text did not contain '{text}' within {timeout}s (current: '{self.get_fix_button_text()}')",
        )

    def get_crack_button_text(self) -> str:
        expr = (
            f"(function() {{"
            f"    var btn = document.querySelector({json.dumps(self.crack_btn_selector)});"
            f"    return btn ? btn.innerText.trim() : '';"
            f"}})()"
        )
        return str(self.driver.evaluate(expr, await_promise=False) or "")

    def click_crack_button(self) -> None:
        self.driver.click(self.crack_btn_selector)

    def wait_for_crack_text(self, text: str, timeout: float = 5.0) -> None:
        self.driver.wait_for_condition(
            lambda: text.lower() in self.get_crack_button_text().lower(),
            timeout=timeout,
            error_message=f"Crack button text did not contain '{text}' within {timeout}s (current: '{self.get_crack_button_text()}')",
        )


class NaviModal(BaseModalPage):
    """Encapsulates the Big Picture Navi Menu Modal (#sls-navi-modal)."""

    def __init__(self, driver: "BrowserDriver"):
        super().__init__(
            driver=driver,
            root_selector="#sls-navi-modal",
            close_selector="#sls-navi-close",
            cancel_selector="#sls-navi-cancel",
        )
        self.provider_btn = "#sls-navi-btn-provider"
        self.config_btn = "#sls-navi-btn-config"
        self.cloud_toggle_btn = "#sls-navi-btn-cloud-toggle"
        self.cloud_backup_btn = "#sls-navi-btn-cloud-backup"
        self.cloud_restore_btn = "#sls-navi-btn-cloud-restore"
        self.cloud_reset_btn = "#sls-navi-btn-cloud-reset"
        self.remove_btn = "#sls-navi-btn-remove"
        self.cloud_badge = "#sls-navi-cloud-badge"

    def click_config(self) -> None:
        self.driver.click(self.config_btn)

    def click_cloud_toggle(self) -> None:
        self.driver.click(self.cloud_toggle_btn)

    def click_cloud_backup(self) -> None:
        self.driver.click(self.cloud_backup_btn)

    def click_cloud_restore(self) -> None:
        self.driver.click(self.cloud_restore_btn)

    def click_cloud_reset(self) -> None:
        self.driver.click(self.cloud_reset_btn)

    def click_remove_lua(self) -> None:
        self.driver.click(self.remove_btn)

    def get_cloud_badge_text(self) -> str:
        expr = (
            f"(function() {{"
            f"    var el = document.querySelector({json.dumps(self.cloud_badge)});"
            f"    return el ? el.innerText.trim() : '';"
            f"}})()"
        )
        return str(self.driver.evaluate(expr, await_promise=False) or "")


class AppRestoreModal(BaseModalPage):
    """Encapsulates the Cloud Restore Modal (#sls-restore-modal)."""

    def __init__(self, driver: "BrowserDriver"):
        super().__init__(
            driver=driver,
            root_selector="#sls-restore-modal",
            close_selector="#sls-restore-close",
            cancel_selector="#sls-modal-cancel",
        )

    def get_backups_count(self) -> int:
        expr = "document.querySelectorAll('#sls-restore-body .sls-btn-restore-item').length"
        return int(self.driver.evaluate(expr, await_promise=False) or 0)

    def get_backup_names(self) -> List[str]:
        expr = (
            "(function() {"
            "    var items = document.querySelectorAll('#sls-restore-body div[title]');"
            "    var res = [];"
            "    items.forEach(function(el) { res.push(el.textContent.trim()); });"
            "    return res;"
            "})()"
        )
        return list(self.driver.evaluate(expr, await_promise=False) or [])

    def click_restore_backup(self, index: int = 0) -> None:
        expr = (
            f"(function() {{"
            f"    var btns = document.querySelectorAll('#sls-restore-body .sls-btn-restore-item');"
            f"    if (btns[{index}]) {{ btns[{index}].click(); return true; }}"
            f"    return false;"
            f"}})()"
        )
        self.driver.evaluate(expr, await_promise=False)

    def wait_for_backups_count(self, min_count: int, timeout: float = 5.0) -> None:
        self.driver.wait_for_condition(
            lambda: self.get_backups_count() >= min_count,
            timeout=timeout,
            error_message=f"Backups count did not reach {min_count} within {timeout}s",
        )


class RemoveLuaModal(BaseModalPage):
    """Encapsulates the Remove Lua Modal (#sls-remove-modal)."""

    def __init__(self, driver: "BrowserDriver"):
        super().__init__(
            driver=driver,
            root_selector="#sls-remove-modal",
            close_selector="#sls-remove-cancel",
            cancel_selector="#sls-remove-cancel",
        )
        self.confirm_btn = "#sls-remove-confirm"

    def click_confirm(self) -> None:
        self.driver.click(self.confirm_btn)

    def click_cancel(self) -> None:
        self.driver.click(self.cancel_selector)

    def get_confirm_text(self) -> str:
        expr = (
            f"(function() {{"
            f"    var btn = document.querySelector({json.dumps(self.confirm_btn)});"
            f"    return btn ? btn.innerText.trim() : '';"
            f"}})()"
        )
        return str(self.driver.evaluate(expr, await_promise=False) or "")


class ResetSaveModal(BaseModalPage):
    """Encapsulates the Reset Save Modal (#sls-reset-modal)."""

    def __init__(self, driver: "BrowserDriver"):
        super().__init__(
            driver=driver,
            root_selector="#sls-reset-modal",
            close_selector="#sls-reset-close",
            cancel_selector="#sls-modal-cancel",
        )
        self.input_selector = "#sls-reset-input"
        self.confirm_btn = "#sls-modal-confirm"

    def type_confirm_text(self, text: str = "RESET") -> None:
        expr = (
            f"(function() {{"
            f"    var input = document.querySelector({json.dumps(self.input_selector)});"
            f"    if (!input) return false;"
            f"    input.value = {json.dumps(text)};"
            f"    input.dispatchEvent(new Event('input', {{ bubbles: true }}));"
            f"    return true;"
            f"}})()"
        )
        self.driver.evaluate(expr, await_promise=False)

    def is_confirm_disabled(self) -> bool:
        expr = (
            f"(function() {{"
            f"    var btn = document.querySelector({json.dumps(self.confirm_btn)});"
            f"    return btn ? !!btn.disabled : true;"
            f"}})()"
        )
        return bool(self.driver.evaluate(expr, await_promise=False))

    def click_confirm(self) -> None:
        self.driver.click(self.confirm_btn)


class LibraryDetailsPage:
    """Encapsulates the Steam Library App Details page."""

    def __init__(self, driver: "BrowserDriver"):
        self.driver = driver
        self.game_manager_modal = GameManagerModal(driver)
        self.navi_modal = NaviModal(driver)
        self.restore_modal = AppRestoreModal(driver)
        self.remove_modal = RemoveLuaModal(driver)
        self.reset_modal = ResetSaveModal(driver)

    def navigate_library(self, fixture_path: str | Path, appid: int = 1443430) -> None:
        self.driver.navigate_to_file(fixture_path, query=f"/app/{appid}")

    def inject_app_details_script(self, script_path: str | Path) -> None:
        self.driver.inject_script(script_path)

    def enable_desktop_ui(self, appid: int = 1443430) -> None:
        """Switch to desktop mode buttons and trigger button refresh."""
        self.driver.evaluate(
            "(function() {"
            "    document.body.classList.add('DesktopUI');"
            f"    if (window.__slsRefreshButtons) window.__slsRefreshButtons({appid});"
            "})()",
            await_promise=False,
        )

    def enable_deck_ui(self, appid: int = 1443430) -> None:
        """Switch to Steam Deck / GamepadUI single Navi button and refresh."""
        self.driver.evaluate(
            "(function() {"
            "    document.body.classList.remove('DesktopUI');"
            f"    if (window.__slsRefreshButtons) window.__slsRefreshButtons({appid});"
            "})()",
            await_promise=False,
        )

    def wait_for_injected_buttons(self, timeout: float = 5.0) -> None:
        """Wait until either .sls-navi-btn or .sls-config-btn appears."""
        self.driver.wait_for_condition(
            lambda: bool(self.driver.evaluate("!!document.querySelector('.sls-navi-btn, .sls-config-btn')")),
            timeout=timeout,
            error_message=f"Injected buttons did not appear within {timeout}s",
        )

    # --- Big Picture Mode Controls ---

    def has_navi_button(self) -> bool:
        return bool(self.driver.evaluate("!!document.querySelector('.sls-navi-btn')"))

    def get_navi_button_text(self) -> str:
        expr = (
            "(function() {"
            "    var els = Array.from(document.querySelectorAll('.sls-navi-btn'));"
            "    var btn = els.find(function(e) {"
            "        var r = e.getBoundingClientRect();"
            "        return r.width > 0 && r.height > 0 && window.getComputedStyle(e).pointerEvents !== 'none';"
            "    }) || els[0];"
            "    return btn ? btn.innerText.trim() : '';"
            "})()"
        )
        return str(self.driver.evaluate(expr, await_promise=False) or "")

    def click_navi_button(self) -> NaviModal:
        self.driver.click(".sls-navi-btn")
        self.navi_modal.wait_until_open()
        return self.navi_modal

    # --- Desktop Mode Controls ---

    def ensure_menu_open(self) -> None:
        """Open the Navi menu modal if not already open."""
        is_navi_open = bool(self.driver.evaluate("!!document.getElementById('sls-navi-modal')"))
        if not is_navi_open:
            self.click_navi_button()

    def click_config_button(self) -> GameManagerModal:
        self.ensure_menu_open()
        self.driver.click(".sls-config-btn, #sls-navi-btn-config")
        self.game_manager_modal.wait_until_open()
        return self.game_manager_modal

    def click_remove_lua_button(self) -> RemoveLuaModal:
        self.ensure_menu_open()
        self.driver.click(".sls-remove-lua-btn, #sls-navi-btn-remove")
        self.remove_modal.wait_until_open()
        return self.remove_modal

    def get_remove_lua_button_text(self) -> str:
        expr = (
            "(function() {"
            "    var els = Array.from(document.querySelectorAll('.sls-remove-lua-btn, #sls-navi-btn-remove, .sls-navi-btn'));"
            "    var btn = els.find(function(e) {"
            "        var r = e.getBoundingClientRect();"
            "        return r.width > 0 && r.height > 0 && window.getComputedStyle(e).pointerEvents !== 'none';"
            "    }) || els[0];"
            "    return btn ? btn.innerText.trim() : '';"
            "})()"
        )
        return str(self.driver.evaluate(expr, await_promise=False) or "")

    def click_cloud_toggle_button(self) -> None:
        self.ensure_menu_open()
        self.driver.click(".sls-cloud-toggle-btn, #sls-navi-btn-cloud-toggle")

    def get_cloud_toggle_text(self) -> str:
        self.ensure_menu_open()
        expr = (
            "(function() {"
            "    var btn = document.querySelector('.sls-cloud-toggle-btn, #sls-navi-btn-cloud-toggle');"
            "    return btn ? btn.innerText.trim() : '';"
            "})()"
        )
        return str(self.driver.evaluate(expr, await_promise=False) or "")

    def wait_for_cloud_toggle_text(self, text: str, timeout: float = 5.0) -> None:
        self.driver.wait_for_condition(
            lambda: text.lower() in self.get_cloud_toggle_text().lower(),
            timeout=timeout,
            error_message=f"Cloud toggle button did not contain '{text}' within {timeout}s",
        )

    def click_cloud_backup_button(self) -> None:
        self.ensure_menu_open()
        self.driver.click(".sls-cloud-backup-btn, #sls-navi-btn-cloud-backup")

    def get_cloud_backup_text(self) -> str:
        self.ensure_menu_open()
        expr = (
            "(function() {"
            "    var btn = document.querySelector('.sls-cloud-backup-btn, #sls-navi-btn-cloud-backup');"
            "    return btn ? btn.innerText.trim() : '';"
            "})()"
        )
        return str(self.driver.evaluate(expr, await_promise=False) or "")

    def wait_for_cloud_backup_text(self, text: str, timeout: float = 5.0) -> None:
        self.driver.wait_for_condition(
            lambda: text.lower() in self.get_cloud_backup_text().lower(),
            timeout=timeout,
            error_message=f"Cloud backup button did not contain '{text}' within {timeout}s",
        )

    def click_cloud_restore_button(self) -> AppRestoreModal:
        self.ensure_menu_open()
        self.driver.click(".sls-cloud-restore-btn, #sls-navi-btn-cloud-restore")
        self.restore_modal.wait_until_open()
        return self.restore_modal

    def click_cloud_reset_button(self) -> ResetSaveModal:
        self.ensure_menu_open()
        self.driver.click(".sls-cloud-reset-btn, #sls-navi-btn-cloud-reset")
        self.reset_modal.wait_until_open()
        return self.reset_modal
