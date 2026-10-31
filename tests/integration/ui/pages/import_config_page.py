"""
Page Object Model for Manual Installation / Import Lua Page.

Encapsulates:
  - Top tab (#sls-top-tab)
  - Page container (#sls-page-container)
  - Drag-and-drop file staging (#sls-manual-dropzone) via synthetic DataTransfer
  - Staged file list (#sls-manual-file-list) and file toggle/removal
  - Import submit button (#sls-manual-submit)
  - Status message banner (#sls-manual-status)
From res/inject-scripts/import-lua-script.js.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import TYPE_CHECKING, Any, Dict, List, Optional

if TYPE_CHECKING:
    from tests.integration.ui.harness.browser_driver import BrowserDriver


class ImportConfigPage:
    """Encapsulates the Manual Installation page and tab controls."""

    def __init__(self, driver: "BrowserDriver"):
        self.driver = driver
        self.tab_selector = "#sls-top-tab"
        self.container_selector = "#sls-page-container"
        self.dropzone_selector = "#sls-manual-dropzone"
        self.file_list_selector = "#sls-manual-file-list"
        self.submit_selector = "#sls-manual-submit"
        self.status_selector = "#sls-manual-status"

    def inject_scripts(self, import_script_path: str | Path, jszip_path: Optional[str | Path] = None) -> None:
        """Inject jszip (if provided) followed by import-lua-script.js."""
        if jszip_path and Path(jszip_path).exists():
            self.driver.inject_script(jszip_path)
        self.driver.inject_script(import_script_path)

    def open(self) -> "ImportConfigPage":
        """Open the manual import page via top tab or window hook."""
        self.driver.evaluate(
            "(function() {"
            "    if (typeof window.__naviShowImportLuaPage === 'function') {"
            "        window.__naviShowImportLuaPage();"
            "    } else {"
            "        var tab = document.getElementById('sls-top-tab');"
            "        if (tab) tab.click();"
            "    }"
            "})()",
            await_promise=False,
        )
        self.wait_until_open()
        return self

    def is_open(self) -> bool:
        expr = (
            f"(function() {{"
            f"    var el = document.querySelector({json.dumps(self.container_selector)});"
            f"    if (!el) return false;"
            f"    return el.style.display !== 'none' && window.getComputedStyle(el).display !== 'none';"
            f"}})()"
        )
        return bool(self.driver.evaluate(expr, await_promise=False))

    def wait_until_open(self, timeout: float = 5.0) -> None:
        self.driver.wait_for_condition(
            self.is_open,
            timeout=timeout,
            error_message=f"Import page container '{self.container_selector}' did not open within {timeout}s",
        )

    # --- Drag & Drop Simulation ---

    def dispatch_drag_over(self) -> None:
        expr = (
            f"(function() {{"
            f"    var dropzone = document.querySelector({json.dumps(self.dropzone_selector)});"
            f"    if (!dropzone) return false;"
            f"    var ev = new DragEvent('dragover', {{ bubbles: true, cancelable: true }});"
            f"    dropzone.dispatchEvent(ev);"
            f"    return true;"
            f"}})()"
        )
        self.driver.evaluate(expr, await_promise=False)

    def dispatch_drag_leave(self) -> None:
        expr = (
            f"(function() {{"
            f"    var dropzone = document.querySelector({json.dumps(self.dropzone_selector)});"
            f"    if (!dropzone) return false;"
            f"    var ev = new DragEvent('dragleave', {{ bubbles: true, cancelable: true }});"
            f"    dropzone.dispatchEvent(ev);"
            f"    return true;"
            f"}})()"
        )
        self.driver.evaluate(expr, await_promise=False)

    def get_dropzone_style(self) -> Dict[str, str]:
        expr = (
            f"(function() {{"
            f"    var dropzone = document.querySelector({json.dumps(self.dropzone_selector)});"
            f"    if (!dropzone) return {{}};"
            f"    var s = window.getComputedStyle(dropzone);"
            f"    return {{"
            f"        borderColor: dropzone.style.borderColor || s.borderColor || '',"
            f"        backgroundColor: dropzone.style.backgroundColor || s.backgroundColor || ''"
            f"    }};"
            f"}})()"
        )
        return self.driver.evaluate(expr, await_promise=False) or {}

    def wait_for_file_list_visible(self, timeout: float = 5.0) -> None:
        self.driver.wait_for_condition(
            self.is_file_list_visible,
            timeout=timeout,
            error_message=f"Staged file list '{self.file_list_selector}' did not become visible within {timeout}s",
        )

    def click_submit(self, force: bool = False) -> None:
        if force:
            expr = (
                f"(function() {{"
                f"    var btn = document.querySelector({json.dumps(self.submit_selector)});"
                f"    if (btn && btn.onclick) btn.onclick(new Event('click'));"
                f"}})()"
            )
            self.driver.evaluate(expr, await_promise=False)
        else:
            self.driver.click(self.submit_selector)

    def stage_files(self, files: List[Dict[str, Any]]) -> None:
        """
        Stage files by synthesizing a DataTransfer with File objects and firing 'drop'.
        Each entry in files should have:
          - name: str (e.g. '1443430.lua', '1443431.manifest', 'bad.zip')
          - content: str or bytes (text or base64)
          - type: Optional[str]
          - is_base64: Optional[bool]
        """
        expr = (
            f"(function(filesList) {{"
            f"    var dropzone = document.querySelector({json.dumps(self.dropzone_selector)});"
            f"    if (!dropzone) return false;"
            f"    var dt = new DataTransfer();"
            f"    for (var i = 0; i < filesList.length; i++) {{"
            f"        var item = filesList[i];"
            f"        var blob;"
            f"        if (item.is_base64) {{"
            f"            var byteCharacters = atob(item.content);"
            f"            var byteNumbers = new Array(byteCharacters.length);"
            f"            for (var j = 0; j < byteCharacters.length; j++) {{"
            f"                byteNumbers[j] = byteCharacters.charCodeAt(j);"
            f"            }}"
            f"            var byteArray = new Uint8Array(byteNumbers);"
            f"            blob = new Blob([byteArray], {{ type: item.type || 'application/octet-stream' }});"
            f"        }} else {{"
            f"            blob = new Blob([item.content || ''], {{ type: item.type || 'text/plain' }});"
            f"        }}"
            f"        var file = new File([blob], item.name, {{ type: item.type || 'application/octet-stream' }});"
            f"        dt.items.add(file);"
            f"    }}"
            f"    var dropEv = new DragEvent('drop', {{ bubbles: true, cancelable: true, dataTransfer: dt }});"
            f"    dropzone.dispatchEvent(dropEv);"
            f"    return true;"
            f"}})({json.dumps(files)})"
        )
        self.driver.evaluate(expr, await_promise=False)

    # --- Staged File List & Removal ---

    def get_staged_file_count(self) -> int:
        expr = (
            f"(function() {{"
            f"    var list = document.querySelector({json.dumps(self.file_list_selector)});"
            f"    if (!list) return 0;"
            f"    return list.children ? list.children.length : 0;"
            f"}})()"
        )
        return int(self.driver.evaluate(expr, await_promise=False) or 0)

    def is_file_list_visible(self) -> bool:
        expr = (
            f"(function() {{"
            f"    var list = document.querySelector({json.dumps(self.file_list_selector)});"
            f"    if (!list) return false;"
            f"    return list.style.display !== 'none' && window.getComputedStyle(list).display !== 'none';"
            f"}})()"
        )
        return bool(self.driver.evaluate(expr, await_promise=False))

    def toggle_file(self, file_name_substr: str) -> None:
        """Click the staged file row matching file_name_substr to toggle checked/unchecked state."""
        expr = (
            f"(function() {{"
            f"    var list = document.querySelector({json.dumps(self.file_list_selector)});"
            f"    if (!list) return false;"
            f"    var items = list.querySelectorAll('details, div');"
            f"    for (var i = 0; i < items.length; i++) {{"
            f"        var t = items[i].innerText || items[i].textContent || '';"
            f"        if (t.indexOf({json.dumps(file_name_substr)}) !== -1) {{"
            f"            items[i].click();"
            f"            return true;"
            f"        }}"
            f"    }}"
            f"    return false;"
            f"}})()"
        )
        self.driver.evaluate(expr, await_promise=False)

    # --- Submit Button State ---

    def is_submit_disabled(self) -> bool:
        expr = (
            f"(function() {{"
            f"    var btn = document.querySelector({json.dumps(self.submit_selector)});"
            f"    return btn ? !!btn.disabled : true;"
            f"}})()"
        )
        return bool(self.driver.evaluate(expr, await_promise=False))

    def get_submit_text(self) -> str:
        expr = (
            f"(function() {{"
            f"    var btn = document.querySelector({json.dumps(self.submit_selector)});"
            f"    return btn ? btn.innerText.trim() : '';"
            f"}})()"
        )
        return str(self.driver.evaluate(expr, await_promise=False) or "")

    def wait_for_submit_enabled(self, timeout: float = 5.0) -> None:
        self.driver.wait_for_condition(
            lambda: not self.is_submit_disabled(),
            timeout=timeout,
            error_message=f"Submit button did not enable within {timeout}s",
        )

    def wait_for_submit_disabled(self, timeout: float = 5.0) -> None:
        self.driver.wait_for_condition(
            self.is_submit_disabled,
            timeout=timeout,
            error_message=f"Submit button did not disable within {timeout}s",
        )

    def wait_for_submit_text(self, expected_text: str, timeout: float = 5.0) -> None:
        self.driver.wait_for_condition(
            lambda: expected_text.lower() in self.get_submit_text().lower(),
            timeout=timeout,
            error_message=f"Submit button text did not contain '{expected_text}' within {timeout}s",
        )

    # --- Status Banner ---

    def get_status_text(self) -> str:
        expr = (
            f"(function() {{"
            f"    var status = document.querySelector({json.dumps(self.status_selector)});"
            f"    return status ? status.innerText.trim() : '';"
            f"}})()"
        )
        return str(self.driver.evaluate(expr, await_promise=False) or "")

    def get_status_color(self) -> str:
        expr = (
            f"(function() {{"
            f"    var status = document.querySelector({json.dumps(self.status_selector)});"
            f"    return status ? (status.style.color || window.getComputedStyle(status).color) : '';"
            f"}})()"
        )
        return str(self.driver.evaluate(expr, await_promise=False) or "")

    def wait_for_status_containing(self, text: str, timeout: float = 5.0) -> None:
        self.driver.wait_for_condition(
            lambda: text.lower() in self.get_status_text().lower(),
            timeout=timeout,
            error_message=f"Status banner did not contain '{text}' within {timeout}s (current: '{self.get_status_text()}')",
        )


class ConfigEditorPage:
    """Encapsulates the Navi Settings / Config Editor page (#sls-config-page-container)."""

    def __init__(self, driver: "BrowserDriver"):
        self.driver = driver
        self.tab_selector = "#sls-config-top-tab"
        self.container_selector = "#sls-config-page-container"
        self.save_btn_selector = "#sls-cfg-save-btn"
        self.discard_btn_selector = "#sls-cfg-discard-btn"
        self.dirty_pill_selector = "#sls-cfg-dirty-pill"
        self.status_banner_selector = "#sls-cfg-status-banner"
        self.search_input_selector = "#sls-cfg-search"

    def open(self) -> "ConfigEditorPage":
        """Open the config editor via top tab or window hook."""
        self.driver.evaluate(
            "(function() {"
            "    if (typeof window.__naviShowConfigPage === 'function') {"
            "        window.__naviShowConfigPage();"
            "    } else {"
            "        var tab = document.getElementById('sls-config-top-tab');"
            "        if (tab) tab.click();"
            "    }"
            "})()",
            await_promise=False,
        )
        self.wait_until_open()
        return self

    def is_open(self) -> bool:
        expr = (
            f"(function() {{"
            f"    var el = document.querySelector({json.dumps(self.container_selector)});"
            f"    if (!el) return false;"
            f"    return el.style.display !== 'none' && window.getComputedStyle(el).display !== 'none';"
            f"}})()"
        )
        return bool(self.driver.evaluate(expr, await_promise=False))

    def wait_until_open(self, timeout: float = 5.0) -> None:
        self.driver.wait_for_condition(
            self.is_open,
            timeout=timeout,
            error_message=f"Config container '{self.container_selector}' did not open within {timeout}s",
        )

    def switch_tab(self, target_section_id: str) -> None:
        """Switch category section via sidebar pill click."""
        expr = (
            f"(function() {{"
            f"    var targetId = {json.dumps(target_section_id)};"
            f"    var pill = document.querySelector('.sls-cfg-pill[data-target=\"' + targetId + '\"]');"
            f"    if (pill) {{ pill.click(); return true; }}"
            f"    return false;"
            f"}})()"
        )
        self.driver.evaluate(expr, await_promise=False)

    def filter_settings(self, query: str) -> None:
        """Set query into settings filter search box."""
        expr = (
            f"(function() {{"
            f"    var input = document.querySelector({json.dumps(self.search_input_selector)});"
            f"    if (!input) return false;"
            f"    input.value = {json.dumps(query)};"
            f"    input.dispatchEvent(new Event('input', {{ bubbles: true }}));"
            f"    return true;"
            f"}})()"
        )
        self.driver.evaluate(expr, await_promise=False)

    def get_visible_items_count(self) -> int:
        """Count setting items currently visible in the container."""
        expr = (
            f"(function() {{"
            f"    var container = document.querySelector({json.dumps(self.container_selector)});"
            f"    if (!container) return 0;"
            f"    var items = container.querySelectorAll('.sls-cfg-item');"
            f"    var count = 0;"
            f"    items.forEach(function(el) {{"
            f"        if (el.style.display !== 'none' && window.getComputedStyle(el).display !== 'none') {{"
            f"            var sec = el.closest('.sls-cfg-section');"
            f"            if (sec && sec.style.display !== 'none' && window.getComputedStyle(sec).display !== 'none') {{"
            f"                count++;"
            f"            }}"
            f"        }}"
            f"    }});"
            f"    return count;"
            f"}})()"
        )
        return int(self.driver.evaluate(expr, await_promise=False) or 0)

    # --- Field Getters / Setters ---

    def get_field_value(self, element_id: str) -> str:
        fid = element_id if element_id.startswith("sls-cfg-") else f"sls-cfg-{element_id}"
        expr = (
            f"(function() {{"
            f"    var el = document.getElementById({json.dumps(fid)});"
            f"    if (!el) return '';"
            f"    if (el.type === 'checkbox') return el.checked ? 'true' : 'false';"
            f"    return el.value || '';"
            f"}})()"
        )
        return str(self.driver.evaluate(expr, await_promise=False) or "")

    def set_field_value(self, element_id: str, value: str) -> None:
        fid = element_id if element_id.startswith("sls-cfg-") else f"sls-cfg-{element_id}"
        expr = (
            f"(function() {{"
            f"    var el = document.getElementById({json.dumps(fid)});"
            f"    if (!el) return false;"
            f"    el.value = {json.dumps(value)};"
            f"    el.dispatchEvent(new Event('input', {{ bubbles: true }}));"
            f"    el.dispatchEvent(new Event('change', {{ bubbles: true }}));"
            f"    return true;"
            f"}})()"
        )
        self.driver.evaluate(expr, await_promise=False)

    def is_toggle_checked(self, element_id: str) -> bool:
        fid = element_id if element_id.startswith("sls-cfg-") else f"sls-cfg-{element_id}"
        expr = (
            f"(function() {{"
            f"    var el = document.getElementById({json.dumps(fid)});"
            f"    return el ? !!el.checked : false;"
            f"}})()"
        )
        return bool(self.driver.evaluate(expr, await_promise=False))

    def set_toggle(self, element_id: str, checked: bool) -> None:
        fid = element_id if element_id.startswith("sls-cfg-") else f"sls-cfg-{element_id}"
        expr = (
            f"(function() {{"
            f"    var el = document.getElementById({json.dumps(fid)});"
            f"    if (!el) return false;"
            f"    el.checked = {'true' if checked else 'false'};"
            f"    el.dispatchEvent(new Event('change', {{ bubbles: true }}));"
            f"    el.dispatchEvent(new Event('input', {{ bubbles: true }}));"
            f"    return true;"
            f"}})()"
        )
        self.driver.evaluate(expr, await_promise=False)

    # --- Save / Discard Actions ---

    def is_dirty(self) -> bool:
        txt = self.get_dirty_pill_text()
        return "unsaved" in txt.lower()

    def get_dirty_pill_text(self) -> str:
        expr = (
            f"(function() {{"
            f"    var el = document.querySelector({json.dumps(self.dirty_pill_selector)});"
            f"    return el ? el.innerText.trim() : '';"
            f"}})()"
        )
        return str(self.driver.evaluate(expr, await_promise=False) or "")

    def is_save_disabled(self) -> bool:
        expr = (
            f"(function() {{"
            f"    var el = document.querySelector({json.dumps(self.save_btn_selector)});"
            f"    return el ? !!el.disabled : true;"
            f"}})()"
        )
        return bool(self.driver.evaluate(expr, await_promise=False))

    def get_save_btn_text(self) -> str:
        expr = (
            f"(function() {{"
            f"    var el = document.querySelector({json.dumps(self.save_btn_selector)});"
            f"    return el ? el.innerText.trim() : '';"
            f"}})()"
        )
        return str(self.driver.evaluate(expr, await_promise=False) or "")

    def click_save(self) -> None:
        self.driver.click(self.save_btn_selector)

    def is_discard_visible(self) -> bool:
        expr = (
            f"(function() {{"
            f"    var el = document.querySelector({json.dumps(self.discard_btn_selector)});"
            f"    if (!el) return false;"
            f"    return el.style.display !== 'none' && window.getComputedStyle(el).display !== 'none';"
            f"}})()"
        )
        return bool(self.driver.evaluate(expr, await_promise=False))

    def click_discard(self) -> None:
        self.driver.click(self.discard_btn_selector)

    def get_status_banner_text(self) -> str:
        expr = (
            f"(function() {{"
            f"    var el = document.querySelector({json.dumps(self.status_banner_selector)});"
            f"    return el ? el.innerText.trim() : '';"
            f"}})()"
        )
        return str(self.driver.evaluate(expr, await_promise=False) or "")

    def wait_for_status_banner(self, text: str, timeout: float = 5.0) -> None:
        self.driver.wait_for_condition(
            lambda: text.lower() in self.get_status_banner_text().lower(),
            timeout=timeout,
            error_message=f"Config status banner did not contain '{text}' within {timeout}s (current: '{self.get_status_banner_text()}')",
        )
