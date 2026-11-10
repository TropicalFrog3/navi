"""
@file  base_modal_page.py
@brief Page object base class for CEF modal dialog interactions


Base Page Object Model for Steam modal dialogs.

Provides shared interactions:
  - open/close detection and synchronization
  - ARIA attribute validation
  - Close button and Cancel button interactions
  - Steam gamepad event dispatching (vgp_ondirection, vgp_onok, vgp_oncancel)
  - Captured alert/confirm dialog inspections
"""

from __future__ import annotations

import json
import time
from typing import TYPE_CHECKING, Any, Callable, Dict, List, Optional

if TYPE_CHECKING:
    from tests.integration.ui.harness.browser_driver import BrowserDriver


class BaseModalPage:
    """Base class for all overlay modals in Navi UI integration tests."""

    # Gamepad Directional constants (D-Pad and analog buttons)
    GAMEPAD_UP = 9
    GAMEPAD_DOWN = 10
    GAMEPAD_LEFT = 11
    GAMEPAD_RIGHT = 12
    GAMEPAD_A = 0  # OK / Accept
    GAMEPAD_B = 1  # Cancel / Back

    def __init__(
        self,
        driver: "BrowserDriver",
        root_selector: str,
        close_selector: Optional[str] = None,
        cancel_selector: Optional[str] = None,
    ):
        self.driver = driver
        self.root_selector = root_selector
        self.close_selector = close_selector or (
            f"{root_selector} button[aria-label='Close'], "
            f"{root_selector} .sls-btn-close, "
            f"{root_selector} #sls-settings-close, "
            f"{root_selector} #sls-reset-close, "
            f"{root_selector} #sls-restore-close, "
            f"{root_selector} #sls-depot-close-x, "
            f"{root_selector} #sls-provider-close"
        )
        self.cancel_selector = cancel_selector or (
            f"{root_selector} #sls-modal-cancel, "
            f"{root_selector} #sls-depot-btn-cancel, "
            f"{root_selector} #sls-provider-cancel"
        )

    # --- Open / Close State ---

    def is_in_dom(self) -> bool:
        """Return True if root element exists in the DOM."""
        expr = f"document.querySelector({json.dumps(self.root_selector)}) !== null"
        return bool(self.driver.evaluate(expr, await_promise=False))

    def is_visible(self) -> bool:
        """Return True if root element exists and is rendered visible."""
        expr = (
            f"(function() {{"
            f"    var el = document.querySelector({json.dumps(self.root_selector)});"
            f"    if (!el) return false;"
            f"    var style = window.getComputedStyle(el);"
            f"    if (style.display === 'none' || style.visibility === 'hidden' || style.opacity === '0') return false;"
            f"    var r = el.getBoundingClientRect();"
            f"    return (r.width > 0 && r.height > 0);"
            f"}})()"
        )
        return bool(self.driver.evaluate(expr, await_promise=False))

    def is_open(self) -> bool:
        """Alias for is_visible to check if modal is currently open."""
        return self.is_visible()

    def wait_until_open(self, timeout: float = 5.0) -> None:
        """Wait until modal is present in DOM and displayed."""
        self.driver.wait_for_condition(
            self.is_visible,
            timeout=timeout,
            error_message=f"Modal '{self.root_selector}' did not open within {timeout}s",
        )

    def wait_until_closed(self, timeout: float = 5.0) -> None:
        """Wait until modal is removed from DOM."""
        self.driver.wait_for_condition(
            lambda: not self.is_in_dom(),
            timeout=timeout,
            error_message=f"Modal '{self.root_selector}' did not close within {timeout}s",
        )

    # --- ARIA & Attribute Inspection ---

    def get_attribute(self, attr_name: str, target_selector: Optional[str] = None) -> Optional[str]:
        """Get an attribute value from the modal root or child selector."""
        selector = target_selector or self.root_selector
        expr = (
            f"(function() {{"
            f"    var el = document.querySelector({json.dumps(selector)});"
            f"    return el ? el.getAttribute({json.dumps(attr_name)}) : null;"
            f"}})()"
        )
        return self.driver.evaluate(expr, await_promise=False)

    def get_role(self, target_selector: Optional[str] = None) -> Optional[str]:
        return self.get_attribute("role", target_selector)

    def get_aria_modal(self) -> Optional[str]:
        return self.get_attribute("aria-modal")

    def get_aria_label(self, target_selector: Optional[str] = None) -> Optional[str]:
        return self.get_attribute("aria-label", target_selector)

    def get_data_testid(self) -> Optional[str]:
        return self.get_attribute("data-testid")

    # --- Close & Cancel Actions ---

    def click_close(self) -> None:
        """Click the close button (X)."""
        self.driver.click(self.close_selector)

    def click_cancel(self) -> None:
        """Click the cancel button."""
        self.driver.click(self.cancel_selector)

    def wait_for_cancel_enabled(self, timeout: float = 5.0) -> None:
        """Wait until cancel button has pointer-events restored."""
        self.driver.wait_for_condition(
            lambda: bool(self.driver.evaluate(
                f"(function() {{"
                f"    var btn = document.querySelector({json.dumps(self.cancel_selector)});"
                f"    return btn && btn.style.pointerEvents !== 'none' && !btn.disabled;"
                f"}})()",
                await_promise=False,
            )),
            timeout=timeout,
            error_message="Cancel button remained disabled or had pointerEvents: none",
        )

    # --- Gamepad Controls ---

    def dispatch_direction(self, button: int) -> None:
        """Dispatch vgp_ondirection custom event with button ID (9=Up, 10=Down, 11=Left, 12=Right)."""
        self.driver.dispatch_gamepad("vgp_ondirection", button)

    def dispatch_ok(self, button: int = GAMEPAD_A) -> None:
        """Dispatch vgp_onok custom event."""
        self.driver.dispatch_gamepad("vgp_onok", button)

    def dispatch_cancel(self, button: int = GAMEPAD_B) -> None:
        """Dispatch vgp_oncancel custom event."""
        self.driver.dispatch_gamepad("vgp_oncancel", button)

    def get_active_element_info(self) -> Dict[str, Any]:
        """Return information about the currently focused element."""
        expr = (
            "(function() {"
            "    var el = document.activeElement;"
            "    if (!el) return null;"
            "    return {"
            "        tag: el.tagName ? el.tagName.toLowerCase() : '',"
            "        id: el.id || '',"
            "        className: el.className || '',"
            "        text: el.innerText || el.textContent || '',"
            "        outline: window.getComputedStyle(el).outline || '',"
            "        disabled: !!el.disabled"
            "    };"
            "})()"
        )
        return self.driver.evaluate(expr, await_promise=False) or {}

    def get_gamepad_outlined_element(self) -> Optional[Dict[str, Any]]:
        """Return the element currently displaying a white 3px outline from gamepad trapper."""
        expr = (
            f"(function() {{"
            f"    var modal = document.querySelector({json.dumps(self.root_selector)});"
            f"    if (!modal) return null;"
            f"    var all = modal.querySelectorAll('*');"
            f"    for (var i = 0; i < all.length; i++) {{"
            f"        var s = window.getComputedStyle(all[i]);"
            f"        if (s.outline && s.outline.indexOf('none') === -1 && s.outline !== 'none 0px rgb(0, 0, 0)') {{"
            f"            return {{"
            f"                tag: all[i].tagName ? all[i].tagName.toLowerCase() : '',"
            f"                id: all[i].id || '',"
            f"                className: all[i].className || '',"
            f"                text: (all[i].innerText || all[i].textContent || '').trim(),"
            f"                disabled: !!all[i].disabled"
            f"            }};"
            f"        }}"
            f"    }}"
            f"    return null;"
            f"}})()"
        )
        return self.driver.evaluate(expr, await_promise=False)

    # --- Captured Alerts ---

    def get_captured_alerts(self) -> List[Dict[str, Any]]:
        """Retrieve alerts intercepted by browser driver."""
        return self.driver.get_captured_alerts()

    def clear_captured_alerts(self) -> None:
        """Clear all captured alerts."""
        self.driver.clear_captured_alerts()

    def wait_for_alert_containing(self, text: str, timeout: float = 5.0) -> Dict[str, Any]:
        """Poll until an alert containing `text` has been recorded."""
        def check() -> Optional[Dict[str, Any]]:
            for a in self.get_captured_alerts():
                if text in a.get("message", ""):
                    return a
            return None

        return self.driver.wait_for_condition(
            check,
            timeout=timeout,
            error_message=f"No captured alert containing '{text}' found within {timeout}s",
        )
