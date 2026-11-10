"""
Cookie and consent modal safeguards.
Deterministically detects and dismisses cookie consent dialogs, GDPR banners,
and blocking modal overlays without burning LLM tokens.
"""

from __future__ import annotations

import logging
import re
from typing import Optional

from playwright.async_api import Page

from jev_browser_agent.models import ElementInfo

logger = logging.getLogger(__name__)

# Common regex patterns used to accept or dismiss cookie banners
DISMISS_BUTTON_PATTERNS = [
    r"accept\s+all(?:\s+cookies)?",
    r"allow\s+all(?:\s+cookies)?",
    r"accept(?:\s+cookies)?",
    r"agree",
    r"i\s+agree",
    r"i\s+accept",
    r"got\s+it",
    r"dismiss",
    r"close",
    r"ok",
    r"understand",
    r"continue",
]

# Common container selectors for consent popups
CONSENT_CONTAINER_SELECTORS = [
    "#onetrust-banner-sdk",
    "#onetrust-consent-sdk",
    ".cookie-banner",
    ".cookie-consent",
    ".cookie-notice",
    "[id*='cookie-banner' i]",
    "[id*='cookie-consent' i]",
    "[class*='cookie-banner' i]",
    "[class*='cookie-consent' i]",
    "[aria-label*='cookie' i]",
    "[aria-label*='consent' i]",
    "div[id*='gdpr' i]",
    "div[class*='gdpr' i]",
    "dialog[open]",
]


class CookieModalGuard:
    """
    Automated safeguard for detecting and dismissing cookie banners, GDPR popups,
    and modal consent overlays.
    """

    async def handle_cookie_banner_if_present(
        self,
        page: Page,
        elements: list[ElementInfo],
        jev_flagged: bool = False,
    ) -> bool:
        """
        Check if a cookie banner or consent overlay is present and click accept/dismiss.

        Args:
            page: Active Playwright Page.
            elements: Currently indexed interactive elements.
            jev_flagged: True if Jev's Noul question flagged a cookie banner.

        Returns:
            True if a banner was detected and dismissed, False otherwise.
        """
        # 1. Search indexed elements for cookie/modal accept buttons
        candidate_element: Optional[ElementInfo] = None

        for el in elements:
            # Must be a clickable button-like element
            is_button_like = (
                el.tag == "button"
                or el.role == "button"
                or el.element_type in ("button", "submit")
            )
            if not is_button_like:
                continue

            text_lower = el.text.strip().lower()
            # Ignore policy/learn-more links
            if any(kw in text_lower for kw in ["learn more", "privacy policy", "cookie policy", "settings", "manage"]):
                continue

            if el.is_cookie_related or el.in_modal:
                for pattern in DISMISS_BUTTON_PATTERNS:
                    if re.search(rf"\b{pattern}\b", text_lower):
                        candidate_element = el
                        break
                if candidate_element:
                    break

        # 2. If not found in elements but Jev flagged or page has consent container
        if not candidate_element:
            for selector in CONSENT_CONTAINER_SELECTORS:
                try:
                    container = await page.query_selector(selector)
                    if container and await container.is_visible():
                        # Find accept button inside this container
                        for label in DISMISS_BUTTON_LABELS:
                            btn = await container.query_selector(f"button:has-text('{label}')")
                            if btn and await btn.is_visible():
                                logger.info("Detected consent banner button via selector '%s' with label '%s'", selector, label)
                                await btn.click()
                                await page.wait_for_timeout(500)
                                return True
                except Exception:
                    continue

        # 3. If we found a candidate element in indexed elements, click via its deterministic ID
        if candidate_element:
            logger.info(
                "Deterministically dismissing cookie banner via element [%d]: '%s'",
                candidate_element.id,
                candidate_element.text,
            )
            try:
                locator = page.locator(candidate_element.selector)
                await locator.click(timeout=3000)
                await page.wait_for_timeout(500)
                return True
            except Exception as e:
                logger.warning("Failed clicking candidate cookie element: %s", e)

        return False
