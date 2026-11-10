"""
Unit tests for deterministic cookie banner and modal safeguards.
"""

import pytest
from playwright.async_api import async_playwright

from jev_browser_agent.cookie_guard import CookieModalGuard
from jev_browser_agent.dom import DOMExtractor


@pytest.mark.asyncio
async def test_cookie_banner_deterministic_dismissal():
    async with async_playwright() as pw:
        browser = await pw.chromium.launch(headless=True)
        page = await browser.new_page()

        html_with_cookie_banner = """
        <!DOCTYPE html>
        <html>
        <head><title>GDPR Protected Site</title></head>
        <body>
            <div id="cookie-banner" class="cookie-notice" role="dialog" aria-modal="true">
                <p>We use cookies to improve your browsing experience.</p>
                <button id="accept-cookies-btn" onclick="document.getElementById('cookie-banner').remove()">Accept all cookies</button>
            </div>
            <main>
                <h1>Welcome to our portal</h1>
                <a href="/dashboard">Go to Dashboard</a>
            </main>
        </body>
        </html>
        """

        await page.set_content(html_with_cookie_banner)

        extractor = DOMExtractor()
        elements, catalog_text, metadata = await extractor.extract_catalog(page)

        # Cookie button should be present in elements
        assert any("Accept all cookies" in el.text for el in elements)

        # Guard dismisses banner
        guard = CookieModalGuard()
        dismissed = await guard.handle_cookie_banner_if_present(page, elements)
        assert dismissed is True

        # Verify banner element was removed from the DOM
        banner_present = await page.query_selector("#cookie-banner")
        assert banner_present is None

        # Re-index DOM state after dismissal
        elements_after, catalog_after, _ = await extractor.extract_catalog(page)
        assert len(elements_after) == 1
        assert elements_after[0].text == "Go to Dashboard"

        await browser.close()

