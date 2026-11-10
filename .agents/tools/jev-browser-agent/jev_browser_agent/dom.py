"""
DOM state extraction and interactive element numbering.
Transforms raw webpage DOM into a structured, numbered catalog of interactive
elements without visual screenshots or pixel overhead.
"""

from __future__ import annotations

import logging
from typing import Any, Optional

from playwright.async_api import Page

from jev_browser_agent.models import ElementInfo

logger = logging.getLogger(__name__)

# In-page JavaScript to scan, filter, tag, and serialize interactive elements
DOM_EXTRACTION_SCRIPT = """
() => {
    // 1. Remove previous jev markers
    document.querySelectorAll('[data-jev-id]').forEach(el => el.removeAttribute('data-jev-id'));

    // 2. Query candidates for interactive elements
    const selector = [
        'a[href]',
        'button',
        'input:not([type="hidden"])',
        'textarea',
        'select',
        'details',
        '[role="button"]',
        '[role="link"]',
        '[role="checkbox"]',
        '[role="radio"]',
        '[role="tab"]',
        '[role="switch"]',
        '[role="menuitem"]',
        '[role="combobox"]',
        '[contenteditable="true"]',
        '[tabindex="0"]',
        '[data-action]',
        '[onclick]'
    ].join(', ');

    const candidates = Array.from(document.querySelectorAll(selector));
    const results = [];
    let currentId = 1;

    // Helper: test if an element is genuinely visible and has dimensions
    function isElementVisible(el) {
        if (!el || el.nodeType !== Node.ELEMENT_NODE) return false;
        
        // Check disabled
        if (el.disabled || el.getAttribute('aria-disabled') === 'true') {
            return false;
        }

        // Check aria-hidden
        if (el.closest('[aria-hidden="true"]')) {
            return false;
        }

        const style = window.getComputedStyle(el);
        if (style.display === 'none' || style.visibility === 'hidden' || style.opacity === '0') {
            return false;
        }

        const rect = el.getBoundingClientRect();
        if (rect.width <= 0 || rect.height <= 0) {
            return false;
        }

        // Must be within viewport or scrollable document
        return true;
    }

    // Helper: test if element is in modal or banner
    function isInModalOrBanner(el) {
        const modalAncestor = el.closest('[role="dialog"], [role="alertdialog"], [aria-modal="true"], .modal, .dialog, #cookie-banner, [class*="cookie" i], [id*="cookie" i], [class*="consent" i], [id*="consent" i]');
        return !!modalAncestor;
    }

    // Helper: test if element is cookie related
    function isCookieRelated(el, text) {
        const lower = (text + ' ' + (el.id || '') + ' ' + (el.className || '')).toLowerCase();
        return lower.includes('cookie') || lower.includes('consent') || lower.includes('gdpr');
    }

    // Helper: extract best text label
    function getBestText(el) {
        // Priority 1: aria-label
        const ariaLabel = el.getAttribute('aria-label');
        if (ariaLabel && ariaLabel.trim()) return ariaLabel.trim();

        // Priority 2: aria-labelledby
        const labelledBy = el.getAttribute('aria-labelledby');
        if (labelledBy) {
            const labelEl = document.getElementById(labelledBy);
            if (labelEl && labelEl.innerText.trim()) return labelEl.innerText.trim();
        }

        // Priority 3: for input labels
        if (el.id) {
            const labelEl = document.querySelector(`label[for="${el.id}"]`);
            if (labelEl && labelEl.innerText.trim()) return labelEl.innerText.trim();
        }
        const parentLabel = el.closest('label');
        if (parentLabel && parentLabel.innerText.trim()) {
            return parentLabel.innerText.trim();
        }

        // Priority 4: placeholder or title
        const placeholder = el.getAttribute('placeholder');
        if (placeholder && placeholder.trim()) return placeholder.trim();
        const title = el.getAttribute('title');
        if (title && title.trim()) return title.trim();

        // Priority 5: innerText / textContent
        let text = el.innerText || el.textContent || '';
        text = text.replace(/\\s+/g, ' ').trim();
        if (text) {
            // Cut off if too long
            return text.slice(0, 100);
        }

        // Priority 6: value for submit / button inputs
        if (el.value && typeof el.value === 'string' && el.value.trim()) {
            return el.value.trim().slice(0, 100);
        }

        return '';
    }

    for (const el of candidates) {
        if (!isElementVisible(el)) continue;

        // Tag with deterministic attribute
        el.setAttribute('data-jev-id', String(currentId));

        const tag = el.tagName.toLowerCase();
        const role = el.getAttribute('role') || '';
        const elType = el.getAttribute('type') || (tag === 'textarea' ? 'textarea' : null);
        const text = getBestText(el);
        const placeholder = el.getAttribute('placeholder') || null;
        const value = (el.value !== undefined && el.value !== null) ? String(el.value).slice(0, 80) : null;
        const checked = (el.type === 'checkbox' || el.type === 'radio') ? el.checked : null;
        const href = (tag === 'a' && el.getAttribute('href')) ? el.getAttribute('href') : null;
        const inModal = isInModalOrBanner(el);
        const cookieRelated = isCookieRelated(el, text);

        let options = [];
        if (tag === 'select') {
            options = Array.from(el.querySelectorAll('option')).map(opt => (opt.innerText || opt.value || '').trim()).filter(Boolean).slice(0, 10);
        }

        results.push({
            id: currentId,
            tag: tag,
            role: role,
            element_type: elType,
            text: text,
            selector: `[data-jev-id="${currentId}"]`,
            placeholder: placeholder,
            value: value,
            checked: checked,
            href: href,
            options: options,
            in_modal: inModal,
            is_cookie_related: cookieRelated
        });

        currentId++;
        if (currentId > 150) {
            // Guard against massive pages flooding token limits
            break;
        }
    }

    return {
        url: window.location.href,
        title: document.title,
        body_text: (document.body ? document.body.innerText.replace(/\\s+/g, ' ').trim().slice(0, 500) : ''),
        elements: results,
        has_dialog: !!document.querySelector('[role="dialog"], [aria-modal="true"]')
    };
}
"""


class DOMExtractor:
    """Extracts and catalogizes interactive elements from the live Playwright page."""

    async def extract_catalog(self, page: Page) -> tuple[list[ElementInfo], str, dict[str, Any]]:
        """
        Extract interactive elements and produce a compact, numbered text catalog.

        Returns:
            tuple:
                - list[ElementInfo]: Structured objects for code lookup
                - str: Numbered text catalog for prompt / Jev evaluation
                - dict[str, Any]: Page metadata (url, title, has_dialog)
        """
        raw_data = await page.evaluate(DOM_EXTRACTION_SCRIPT)
        elements_data = raw_data.get("elements", [])
        url = raw_data.get("url", "")
        title = raw_data.get("title", "")
        body_text = raw_data.get("body_text", "")
        has_dialog = raw_data.get("has_dialog", False)

        elements: list[ElementInfo] = []
        catalog_lines: list[str] = []

        for item in elements_data:
            el_info = ElementInfo(
                id=item["id"],
                tag=item["tag"],
                role=item["role"],
                text=item["text"],
                selector=item["selector"],
                element_type=item["element_type"],
                placeholder=item["placeholder"],
                value=item["value"],
                checked=item["checked"],
                href=item["href"],
                options=item["options"],
                in_modal=item["in_modal"],
                is_cookie_related=item["is_cookie_related"],
            )
            elements.append(el_info)
            catalog_lines.append(el_info.summary)

        catalog_text = (
            f"Page Title: {title}\n"
            f"Page URL: {url}\n"
            f"Interactive Elements Count: {len(elements)}\n"
            f"--- Numbered Element Catalog ---\n"
            + ("\n".join(catalog_lines) if catalog_lines else "(No interactive elements found)")
        )

        metadata = {
            "url": url,
            "title": title,
            "body_text": body_text,
            "has_dialog": has_dialog,
            "element_count": len(elements),
        }

        return elements, catalog_text, metadata

    def format_criteria_map(self, elements: list[ElementInfo]) -> dict[str, str]:
        """
        Build criteria dictionary mapping element ID strings ('1', '2', ..., 'none')
        to their descriptions for Jev Choice question.
        """
        criteria: dict[str, str] = {}
        for el in elements:
            clean_summary = el.summary.replace(f"[{el.id}] ", "")
            criteria[str(el.id)] = clean_summary

        # Mandatory 'none' option for actions like scroll, wait, done, or blocked
        criteria["none"] = "No specific element target (for scroll, wait, done, or blocked)"
        return criteria
