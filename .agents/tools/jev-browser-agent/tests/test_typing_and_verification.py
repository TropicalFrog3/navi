"""
Unit tests for typing text resolution and independent verification of 'done' via Antigravity.
"""

from jev_browser_agent.antigravity_handler import AntigravityReasoningHandler
from jev_browser_agent.config import Config
from jev_browser_agent.models import ActionRecord, ElementInfo


def test_static_typing_text_extraction():
    handler = AntigravityReasoningHandler(config=Config(mock_mode=True))

    input_el = ElementInfo(
        id=2,
        tag="input",
        role="textbox",
        text="Work Email",
        selector='[data-jev-id="2"]',
        element_type="email",
    )

    # 1. Quoted string in goal
    goal1 = "Navigate to signup and type 'developer@company.org' into the email field"
    extracted1 = handler.resolve_typing_text(goal1, input_el, [], "")
    assert extracted1 == "developer@company.org"

    # 2. Key-value style in goal
    goal2 = "Fill registration with email=admin@cloud.io and submit"
    extracted2 = handler.resolve_typing_text(goal2, input_el, [], "")
    assert extracted2 == "admin@cloud.io"

    # 3. Search query with double quotes
    search_el = ElementInfo(
        id=3,
        tag="input",
        role="searchbox",
        text="Search",
        selector='[data-jev-id="3"]',
        element_type="search",
    )
    goal3 = 'Search for "playwright async tutorial" on documentation'
    extracted3 = handler.resolve_typing_text(goal3, search_el, [], "")
    assert extracted3 == "playwright async tutorial"


def test_independent_verification_of_done():
    handler = AntigravityReasoningHandler(config=Config(mock_mode=True))

    # Test 1: DOM contains error message -> False positive rejected!
    history = [
        ActionRecord(
            step=1,
            action_type="click",
            target_id="1",
            element_summary="button 'Submit'",
            source="jev",
            confidence_scores=(0.9, 0.9),
            outcome="Clicked Submit",
        )
    ]
    catalog_with_error = """
    Page Title: Form Submission
    --- Numbered Element Catalog ---
    [1] div "Error: Email is required"
    [2] button "Submit"
    """

    res_fail = handler.verify_completion(
        goal="Submit contact form",
        sub_goals=["Submit contact form"],
        history=history,
        final_catalog_text=catalog_with_error,
        page_url="https://example.com/form",
        page_title="Form Submission",
    )
    assert res_fail.verified is False
    assert "Error" in res_fail.reasoning

    # Test 2: Normal completion state without errors -> Verified true!
    catalog_success = """
    Page Title: Thank You
    --- Numbered Element Catalog ---
    [1] h1 "Thank you for your submission"
    [2] a "Back to Home"
    """
    res_success = handler.verify_completion(
        goal="Submit contact form",
        sub_goals=["Submit contact form"],
        history=history,
        final_catalog_text=catalog_success,
        page_url="https://example.com/thank-you",
        page_title="Thank You",
    )
    assert res_success.verified is True
    assert res_success.confidence >= 0.7
