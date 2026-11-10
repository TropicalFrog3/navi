"""
Unit tests for DOM state extraction and element numbering.
"""

import pytest
from playwright.async_api import Page, async_playwright

from jev_browser_agent.dom import DOMExtractor


@pytest.mark.asyncio
async def test_dom_extractor_numbering_and_catalog():
    async with async_playwright() as pw:
        browser = await pw.chromium.launch(headless=True)
        page = await browser.new_page()

        sample_html = """
        <!DOCTYPE html>
        <html>
        <head><title>Test Store</title></head>
        <body>
            <header>
                <a href="/pricing">Pricing</a>
                <button id="trial-btn">Start Free Trial</button>
            </header>
            <main>
                <form id="signup-form">
                    <label for="email">Work Email</label>
                    <input id="email" type="email" placeholder="name@company.com" />

                    <label for="pwd">Password</label>
                    <input id="pwd" type="password" />

                    <label for="country">Country</label>
                    <select id="country">
                        <option value="US">United States</option>
                        <option value="CA">Canada</option>
                    </select>

                    <button type="submit">Submit Registration</button>
                </form>
                <!-- Hidden element that should be ignored -->
                <div style="display: none;"><button>Hidden Button</button></div>
            </main>
        </body>
        </html>
        """

        await page.set_content(sample_html)

        extractor = DOMExtractor()
        elements, catalog_text, metadata = await extractor.extract_catalog(page)

        # Assertions
        assert metadata["title"] == "Test Store"
        assert len(elements) == 6  # 1 link, 2 buttons, 2 inputs, 1 select = 6 elements
        # Check elements count
        assert len(elements) == 6

        # Check numbering
        assert elements[0].id == 1
        assert elements[0].tag == "a"
        assert elements[0].text == "Pricing"
        assert elements[0].selector == '[data-jev-id="1"]'

        # Check DOM injection
        attr = await page.locator('[data-jev-id="1"]').get_attribute("href")
        assert attr == "/pricing"

        # Check submit button
        submit_btn = next(el for el in elements if el.text == "Submit Registration")
        assert submit_btn.tag == "button"

        # Check catalog formatting
        assert "[1] a \"Pricing\"" in catalog_text
        assert "Start Free Trial" in catalog_text
        assert "placeholder='name@company.com'" in catalog_text

        # Check criteria map
        criteria = extractor.format_criteria_map(elements)
        assert "1" in criteria
        assert "none" in criteria
        assert len(criteria) == 7  # 6 elements + "none"

        await browser.close()
