"""
Unit tests for JevDecisionClient following TypeSafe craft guidelines.
Tests the companion Nouls, confidence thresholds, and decision parsing.
"""

from unittest.mock import MagicMock
from typesafe_sdk import ChoiceAnswer, NoulAnswer, SystemOneResponse, Usage

from jev_browser_agent.config import Config
from jev_browser_agent.jev_client import JevDecisionClient
from jev_browser_agent.models import ElementInfo


def test_jev_client_mock_evaluation():
    config = Config(mock_mode=True)
    client = JevDecisionClient(config=config)

    elements = [
        ElementInfo(id=1, tag="button", role="button", text="Submit", selector="#submit", element_type="button"),
        ElementInfo(id=2, tag="input", role="textbox", text="", selector="#search", element_type="text", placeholder="Search..."),
    ]

    criteria_map = {"1": "button - Submit", "2": "input - Search", "none": "No element"}

    decision = client.query_action(
        goal="Search for documentation",
        catalog_text="[1] <button> Submit\n[2] <input> Search",
        elements=elements,
        criteria_map=criteria_map,
        history_text="No previous actions",
        page_metadata={"title": "Doc Portal", "url": "https://example.com"},
    )

    assert decision.action_type in ("type", "click")
    assert decision.confidence_action >= 0.7
    assert decision.source == "mock"


def test_jev_client_live_companion_nouls():
    config = Config(mock_mode=False, typesafe_api_key="mock_test_key")
    client = JevDecisionClient(config=config)

    # Mock the underlying typesafe client
    mock_ts = MagicMock()
    client._typesafe_client = mock_ts

    usage = Usage(input_tokens=50, output_tokens=10)

    # Scenario A: is_goal_complete Noul >= 0.70 overrides action to 'done'
    mock_ts.system_one.return_value = SystemOneResponse(
        model="jev-1.14",
        usage=usage,
        answers={
            "action_type": ChoiceAnswer(type="choice", choice="click", confidence=0.85, probabilities={"click": 0.85}),
            "target_element": ChoiceAnswer(type="choice", choice="1", confidence=0.80, probabilities={"1": 0.80}),
            "has_matching_element": NoulAnswer(type="noul", noul=0.88),
            "is_goal_complete": NoulAnswer(type="noul", noul=0.92),
            "is_cookie_banner": NoulAnswer(type="noul", noul=0.05),
        },
    )

    elements = [ElementInfo(id=1, tag="button", role="button", text="Done", selector="#done", element_type="button")]
    decision = client.query_action(
        goal="Finish registration",
        catalog_text="[1] <button> Done",
        elements=elements,
        criteria_map={"1": "button - Done", "none": "No element"},
        history_text="",
        page_metadata={"title": "Success", "url": "https://example.com/success"},
    )

    assert decision.action_type == "done"
    assert decision.target_id == "none"
    assert decision.confidence_action >= 0.90
    assert decision.is_cookie_banner is False

    # Scenario B: has_matching_element < 0.30 triggers scroll_down fallback
    mock_ts.system_one.return_value = SystemOneResponse(
        model="jev-1.14",
        usage=usage,
        answers={
            "action_type": ChoiceAnswer(type="choice", choice="click", confidence=0.75, probabilities={"click": 0.75}),
            "target_element": ChoiceAnswer(type="choice", choice="1", confidence=0.40, probabilities={"1": 0.40}),
            "has_matching_element": NoulAnswer(type="noul", noul=0.15),  # Nothing fits!
            "is_goal_complete": NoulAnswer(type="noul", noul=0.10),
            "is_cookie_banner": NoulAnswer(type="noul", noul=0.02),
        },
    )

    decision_b = client.query_action(
        goal="Find footer link",
        catalog_text="[1] <button> Header button",
        elements=elements,
        criteria_map={"1": "button - Header button", "none": "No element"},
        history_text="",
        page_metadata={"title": "Main", "url": "https://example.com"},
    )

    assert decision_b.action_type == "scroll_down"
    assert decision_b.target_id == "none"

    # Scenario C: is_cookie_banner >= 0.65 marks cookie banner true
    mock_ts.system_one.return_value = SystemOneResponse(
        model="jev-1.14",
        usage=usage,
        answers={
            "action_type": ChoiceAnswer(type="choice", choice="click", confidence=0.95, probabilities={"click": 0.95}),
            "target_element": ChoiceAnswer(type="choice", choice="1", confidence=0.95, probabilities={"1": 0.95}),
            "has_matching_element": NoulAnswer(type="noul", noul=0.95),
            "is_goal_complete": NoulAnswer(type="noul", noul=0.01),
            "is_cookie_banner": NoulAnswer(type="noul", noul=0.88),  # Active banner!
        },
    )

    decision_c = client.query_action(
        goal="Accept cookies",
        catalog_text="[1] <button> Accept",
        elements=elements,
        criteria_map={"1": "button - Accept", "none": "No element"},
        history_text="",
        page_metadata={"title": "Main", "url": "https://example.com"},
    )

    assert decision_c.is_cookie_banner is True
