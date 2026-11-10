"""
@file  store_app_page.py
@brief Page object representing Steam store application pages


Page Object Model for Steam Store App Page and Modals.

Encapsulates:
  - Store Lua button (.sls-lua-btn)
  - Provider Config Modal (#sls-overlay-modal)
  - Depot Selection Modal (#sls-depot-overlay-modal)
From res/inject-scripts/store-page-script.js and depot-selection-script.js.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import TYPE_CHECKING, Any, Dict, List, Optional

from .base_modal_page import BaseModalPage

if TYPE_CHECKING:
    from tests.integration.ui.harness.browser_driver import BrowserDriver


class ProviderConfigModal(BaseModalPage):
    """Encapsulates the Lua provider modal (#sls-overlay-modal)."""

    def __init__(self, driver: "BrowserDriver"):
        super().__init__(
            driver=driver,
            root_selector="#sls-overlay-modal",
            close_selector="#sls-provider-close",
            cancel_selector="#sls-overlay-modal #sls-provider-cancel",
        )

    def get_api_key(self, provider_idx: int) -> str:
        expr = (
            f"(function() {{"
            f"    var input = document.querySelector('[data-sls-api-key=\"{provider_idx}\"]');"
            f"    return input ? input.value : '';"
            f"}})()"
        )
        return str(self.driver.evaluate(expr, await_promise=False) or "")

    def set_api_key(self, provider_idx: int, key: str) -> None:
        expr = (
            f"(function() {{"
            f"    var input = document.querySelector('[data-sls-api-key=\"{provider_idx}\"]');"
            f"    if (!input) return false;"
            f"    input.value = {json.dumps(key)};"
            f"    input.dispatchEvent(new Event('input', {{ bubbles: true }}));"
            f"    input.dispatchEvent(new Event('change', {{ bubbles: true }}));"
            f"    return true;"
            f"}})()"
        )
        self.driver.evaluate(expr, await_promise=False)

    def focus_api_key(self, provider_idx: int) -> None:
        expr = (
            f"(function() {{"
            f"    var input = document.querySelector('[data-sls-api-key=\"{provider_idx}\"]');"
            f"    if (!input) return false;"
            f"    input.focus();"
            f"    return true;"
            f"}})()"
        )
        self.driver.evaluate(expr, await_promise=False)

    def click_download_provider(self, provider_idx: int) -> None:
        self.driver.click(f'[data-sls-provider-download="{provider_idx}"]')

    def click_auto_download(self) -> None:
        self.driver.click("#sls-auto-search")

    def click_clear_app_cache(self) -> None:
        self.driver.click("#sls-clear-app-cache")

    def click_clear_all_cache(self) -> None:
        self.driver.click("#sls-clear-all-cache")


class DepotSelectionModal(BaseModalPage):
    """Encapsulates the Depot Selection Modal (#sls-depot-overlay-modal)."""

    def __init__(self, driver: "BrowserDriver"):
        super().__init__(
            driver=driver,
            root_selector="#sls-depot-overlay-modal",
            close_selector="#sls-depot-close-x",
            cancel_selector="#sls-depot-btn-cancel",
        )
        self.download_btn_selector = "#sls-depot-btn-download"

    def get_checkbox_count(self) -> int:
        expr = "document.querySelectorAll('#sls-depot-content .sls-depot-checkbox').length"
        return int(self.driver.evaluate(expr, await_promise=False) or 0)

    def is_checkbox_checked(self, index: int) -> bool:
        expr = (
            f"(function() {{"
            f"    var cbs = document.querySelectorAll('#sls-depot-content .sls-depot-checkbox');"
            f"    return cbs[{index}] ? cbs[{index}].checked : false;"
            f"}})()"
        )
        return bool(self.driver.evaluate(expr, await_promise=False))

    def toggle_checkbox(self, index: int) -> None:
        expr = (
            f"(function() {{"
            f"    var cbs = document.querySelectorAll('#sls-depot-content .sls-depot-checkbox');"
            f"    if (!cbs[{index}]) return false;"
            f"    cbs[{index}].checked = !cbs[{index}].checked;"
            f"    cbs[{index}].dispatchEvent(new Event('change', {{ bubbles: true }}));"
            f"    return true;"
            f"}})()"
        )
        self.driver.evaluate(expr, await_promise=False)

    def is_download_enabled(self) -> bool:
        expr = (
            f"(function() {{"
            f"    var btn = document.querySelector({json.dumps(self.download_btn_selector)});"
            f"    if (!btn) return false;"
            f"    return btn.style.pointerEvents !== 'none' && btn.style.opacity !== '0.5' && !btn.disabled;"
            f"}})()"
        )
        return bool(self.driver.evaluate(expr, await_promise=False))

    def get_download_text(self) -> str:
        expr = (
            f"(function() {{"
            f"    var btn = document.querySelector({json.dumps(self.download_btn_selector)});"
            f"    return btn ? btn.innerText.trim() : '';"
            f"}})()"
        )
        return str(self.driver.evaluate(expr, await_promise=False) or "")

    def click_download(self) -> None:
        self.driver.click(self.download_btn_selector)

    def wait_for_download_text(self, expected_text: str, timeout: float = 5.0) -> None:
        self.driver.wait_for_condition(
            lambda: expected_text.lower() in self.get_download_text().lower(),
            timeout=timeout,
            error_message=f"Download button text did not contain '{expected_text}' within {timeout}s",
        )


class StoreAppPage:
    """Entry point for testing Steam Store app pages with injected Lua tools."""

    def __init__(self, driver: "BrowserDriver"):
        self.driver = driver
        self.provider_modal = ProviderConfigModal(driver)
        self.depot_modal = DepotSelectionModal(driver)

    def navigate_store_app(self, fixture_path: str | Path, appid: int = 1443430) -> None:
        self.driver.navigate_to_file(fixture_path, query=f"/app/{appid}")

    def inject_scripts(
        self,
        store_script_path: str | Path,
        depot_script_path: Optional[str | Path] = None,
    ) -> None:
        self.driver.inject_script(store_script_path)
        if depot_script_path and Path(depot_script_path).exists():
            self.driver.inject_script(depot_script_path)

    def inject_depot_script(self, depot_script_path: str | Path, appid: int = 1443430) -> DepotSelectionModal:
        code = Path(depot_script_path).read_text(encoding="utf-8")
        code = code.replace("%APPID%", str(appid))
        self.driver.evaluate(code, await_promise=False)
        self.depot_modal.wait_until_open()
        return self.depot_modal

    def get_lua_button_text(self, appid: int = 1443430) -> str:
        expr = (
            f"(function() {{"
            f"    var btn = document.querySelector('.sls-lua-btn[data-sls-appid=\"{appid}\"]') || document.querySelector('.sls-lua-btn');"
            f"    if (!btn) return '';"
            f"    return btn.innerText.trim();"
            f"}})()"
        )
        return str(self.driver.evaluate(expr, await_promise=False) or "")

    def is_lua_button_present(self, appid: int = 1443430) -> bool:
        expr = (
            f"document.querySelector('.sls-lua-btn[data-sls-appid=\"{appid}\"]') !== null || "
            f"document.querySelector('.sls-lua-btn') !== null"
        )
        return bool(self.driver.evaluate(expr, await_promise=False))

    def wait_for_lua_button(self, appid: int = 1443430, timeout: float = 5.0) -> None:
        self.driver.wait_for_condition(
            lambda: self.is_lua_button_present(appid),
            timeout=timeout,
            error_message=f"Lua button for AppID {appid} did not appear within {timeout}s",
        )

    def click_lua_button(self, appid: int = 1443430) -> None:
        expr = (
            f"(function() {{"
            f"    var btn = document.querySelector('.sls-lua-btn[data-sls-appid=\"{appid}\"]') || document.querySelector('.sls-lua-btn');"
            f"    if (!btn) return false;"
            f"    var link = btn.querySelector('a') || btn;"
            f"    link.click();"
            f"    return true;"
            f"}})()"
        )
        self.driver.evaluate(expr, await_promise=False)
