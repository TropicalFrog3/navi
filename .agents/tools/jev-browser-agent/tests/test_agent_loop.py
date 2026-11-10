"""
End-to-end integration tests for HybridFastBrowserAgent execution loop.
"""

import pytest
from playwright.async_api import async_playwright

from jev_browser_agent.agent import HybridFastBrowserAgent
from jev_browser_agent.config import Config


@pytest.mark.asyncio
async def test_hybrid_agent_end_to_end_flow():
    async with async_playwright() as pw:
        browser = await pw.chromium.launch(headless=True)
        page = await browser.new_page()

        # Mock application page with search box and results
        test_app_html = """
        <!DOCTYPE html>
        <html>
        <head><title>Search Portal</title></head>
        <body>
            <div id="consent-dialog" class="cookie-banner">
                <span>Cookie Notice</span>
                <button onclick="document.getElementById('consent-dialog').remove()">Accept all</button>
            </div>
            <header>
                <h1>Search Portal</h1>
                <input id="search-box" type="search" placeholder="Type query here..." />
                <button id="search-submit" onclick="showResults()">Search</button>
            </header>
            <div id="results" style="display:none;">
                <h2>Search Results</h2>
                <a id="pricing-link" href="#pricing" onclick="showPricing()">View Pricing Plans</a>
            </div>
            <div id="pricing" style="display:none;">
                <h2>Thank you! Success: Pricing Plans confirmed</h2>
                <button id="finish-btn">Done</button>
            </div>
            <script>
                function showResults() {
                    document.getElementById('results').style.display = 'block';
                }
                function showPricing() {
                    document.getElementById('pricing').style.display = 'block';
                }
            </script>
        </body>
        </html>
        """

        await page.set_content(test_app_html)

        # Configure agent in mock/simulation mode
        config = Config(
            browser_headless=True,
            mock_mode=True,
            confidence_threshold=0.7,
            sliding_window_size=5,
            max_steps=10,
        )

        agent = HybridFastBrowserAgent(config=config)

        # Run agent on existing page
        result = await agent.run(
            start_url="about:blank",
            goal="Search for 'playwright' and find pricing plans",
            page=page,
        )

        # Verify outcome
        assert result.total_steps > 0
        assert len(result.history) <= 5  # Sliding window invariant!
        assert result.success is True
        assert result.verification is not None
        assert result.verification.verified is True

        await browser.close()


@pytest.mark.asyncio
async def test_jev_only_standalone_mode():
    async with async_playwright() as pw:
        browser = await pw.chromium.launch(headless=True)
        page = await browser.new_page()

        simple_html = """
        <!DOCTYPE html>
        <html>
        <head><title>Jev Standalone</title></head>
        <body>
            <h1>Welcome</h1>
            <a href="#done">Success: Target reached</a>
        </body>
        </html>
        """
        await page.set_content(simple_html)

        config = Config(
            browser_headless=True,
            mock_mode=True,
            jev_only=True,
            confidence_threshold=0.7,
            max_steps=5,
        )

        agent = HybridFastBrowserAgent(config=config)
        result = await agent.run(
            start_url="about:blank",
            goal="Reach the success target",
            page=page,
        )

        assert result.success is True
        assert result.total_steps > 0
        assert result.verification.verified is True
        assert "Jev-Only Mode" in result.verification.reasoning

        await browser.close()

