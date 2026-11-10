"""
TypeSafe / Jev System One decision client.
Formulates batched micro-decision queries evaluating action type and target element
against DOM catalogs with sub-20ms latency and calibrated confidence scores.
"""

from __future__ import annotations

import logging
import re
from typing import Any, Optional

from jev_browser_agent.config import Config, default_config
from jev_browser_agent.models import (
    ALLOWED_ACTIONS,
    ActionDecision,
    ActionType,
    ElementInfo,
)

logger = logging.getLogger(__name__)

# Action criteria dictionary defining strict semantic boundaries for Jev
ACTION_CRITERIA: dict[str, str] = {
    "click": "Click a link, button, tab, checkbox, radio button, or clickable element.",
    "type": "Enter or fill text into an input field, search box, or textarea.",
    "select": "Choose an option from a dropdown or select menu.",
    "scroll_down": "Scroll down the page to view more content or unrevealed interactive elements.",
    "scroll_up": "Scroll up the page to view earlier content.",
    "wait": "Pause briefly to allow dynamic content, navigation, or animations to finish loading.",
    "done": "The target goal or sub-goal is fully accomplished on this page/flow.",
    "blocked": "Progress is halted (e.g., CAPTCHA, authentication wall without credentials, error page, or no path forward).",
}


class JevDecisionClient:
    """
    Client for querying the Jev (TypeSafe System One) micro-decision model.
    Batches action type selection, element target selection, and cookie banner detection
    into a single API round-trip.
    """

    def __init__(self, config: Optional[Config] = None):
        self.config = config or default_config
        self._typesafe_client = None

        if not self.config.mock_mode and self.config.typesafe_api_key:
            try:
                from typesafe_sdk import TypeSafeClient

                self._typesafe_client = TypeSafeClient(
                    api_key=self.config.typesafe_api_key,
                    base_url=self.config.typesafe_base_url,
                )
                logger.info("Initialized live TypeSafe / Jev client (model: %s)", self.config.typesafe_model)
            except Exception as e:
                logger.warning("Could not initialize TypeSafeClient (%s). Using fallback mode.", e)
                self._typesafe_client = None

    def query_action(
        self,
        goal: str,
        catalog_text: str,
        elements: list[ElementInfo],
        criteria_map: dict[str, str],
        history_text: str,
        page_metadata: dict[str, Any],
    ) -> ActionDecision:
        """
        Evaluate current DOM state against Jev System One questions in a batched call.

        Returns:
            ActionDecision containing chosen action_type, target_id, and confidences.
        """
        # If in mock mode or client not available, run deterministic mock evaluation
        if self.config.mock_mode or not self._typesafe_client:
            return self._mock_evaluate(
                goal=goal,
                elements=elements,
                catalog_text=catalog_text,
                history_text=history_text,
                page_metadata=page_metadata,
            )

        from typesafe_sdk import Choice, Noul

        # Formulate state payload
        state_payload = {
            "current_goal": goal,
            "page_url": page_metadata.get("url", ""),
            "page_title": page_metadata.get("title", ""),
            "interactive_elements_catalog": catalog_text,
            "recent_action_history": history_text,
        }

        # Formulate batched questions following TypeSafe craft (Companion Nouls for null cases)
        questions = {
            "action_type": Choice(
                instructions="Based on the current goal and visible elements, what is the next immediate browser action?",
                criteria=ACTION_CRITERIA,
            ),
            "target_element": Choice(
                instructions="Which numbered element ID from the catalog should be the target for this action?",
                criteria=criteria_map,
            ),
            "has_matching_element": Noul(
                instructions="Is there an interactive element in the catalog that clearly serves the next step?",
                criteria={
                    "true": "At least one button, link, or input in the catalog directly advances the user's goal.",
                    "false": "None of the visible elements in the catalog match or advance the goal, or the target element is outside the current viewport.",
                },
            ),
            "is_goal_complete": Noul(
                instructions="Does the current page state indicate that the user's goal has been fully completed?",
                criteria={
                    "true": "The page shows a confirmation, completion message, successful outcome, or the final destination is reached.",
                    "false": "The goal is still in progress, on an intermediate step, or not yet finished.",
                },
            ),
            "is_cookie_banner": Noul(
                instructions="Is an active cookie consent banner, GDPR popup, or blocking modal overlay visible on this page?",
                criteria={
                    "true": "A cookie consent dialogue, privacy acceptance banner, or modal backdrop is visible over the page content.",
                    "false": "Normal webpage content is unobstructed; no consent dialog or modal overlay is present.",
                },
            ),
        }

        try:
            response = self._typesafe_client.system_one(
                state=state_payload,
                questions=questions,
                model=self.config.typesafe_model,
            )

            # 1. Parse action_type
            action_ans = response.answers.get("action_type")
            action_type_str = action_ans.choice if hasattr(action_ans, "choice") else "wait"
            confidence_action = float(action_ans.confidence) if hasattr(action_ans, "confidence") else 0.5

            if action_type_str not in ALLOWED_ACTIONS:
                action_type_str = "wait"

            # 2. Parse target_element
            target_ans = response.answers.get("target_element")
            target_id = target_ans.choice if hasattr(target_ans, "choice") else "none"
            confidence_target = float(target_ans.confidence) if hasattr(target_ans, "confidence") else 0.5

            # 3. Companion Noul: goal completion check (threshold >= 0.70)
            goal_complete_ans = response.answers.get("is_goal_complete")
            if hasattr(goal_complete_ans, "noul") and goal_complete_ans.noul >= 0.70:
                action_type_str = "done"
                target_id = "none"
                confidence_action = max(confidence_action, goal_complete_ans.noul)

            # 4. Companion Noul: null-case element check (threshold < 0.30 means nothing fits)
            has_match_ans = response.answers.get("has_matching_element")
            if hasattr(has_match_ans, "noul") and has_match_ans.noul < 0.30:
                if action_type_str in ("click", "type", "select"):
                    # Code truth table: nothing matches in visible viewport, decide to scroll down
                    action_type_str = "scroll_down"
                    target_id = "none"

            # 5. Companion Noul: cookie banner check (threshold >= 0.65)
            cookie_ans = response.answers.get("is_cookie_banner")
            is_cookie = False
            if hasattr(cookie_ans, "noul"):
                is_cookie = cookie_ans.noul >= 0.65

            logger.info(
                "Jev micro-decision: action=%s (conf=%.2f), target=%s (conf=%.2f), cookie_banner=%s",
                action_type_str,
                confidence_action,
                target_id,
                confidence_target,
                is_cookie,
            )

            return ActionDecision(
                action_type=action_type_str,  # type: ignore[arg-type]
                target_id=target_id,
                confidence_action=confidence_action,
                confidence_target=confidence_target,
                source="jev",
                is_cookie_banner=is_cookie,
                reasoning=f"Jev batch classifier (action conf: {confidence_action:.2f}, target conf: {confidence_target:.2f})",
            )

        except Exception as e:
            logger.error("Jev API call failed: %s. Falling back to low-confidence decision.", e)
            return ActionDecision(
                action_type="blocked",
                target_id="none",
                confidence_action=0.0,
                confidence_target=0.0,
                source="jev",
                reasoning=f"Jev API error: {str(e)}",
            )

    def _mock_evaluate(
        self,
        goal: str,
        elements: list[ElementInfo],
        catalog_text: str,
        history_text: str,
        page_metadata: dict[str, Any],
    ) -> ActionDecision:
        """
        Deterministic simulation of Jev decision-making for offline testing and evaluation.
        Matches keywords from goal against DOM elements.
        """
        goal_lower = goal.lower()

        # Check for cookie banner buttons
        for el in elements:
            if el.is_cookie_related or el.in_modal:
                text_lower = el.text.lower()
                if any(w in text_lower for w in ["accept", "agree", "allow", "dismiss", "got it", "close"]):
                    return ActionDecision(
                        action_type="click",
                        target_id=str(el.id),
                        confidence_action=0.98,
                        confidence_target=0.95,
                        source="cookie_safeguard",
                        is_cookie_banner=True,
                        reasoning="Deterministic cookie/modal safeguard match",
                    )

        # Check if goal looks finished (e.g. goal keywords found on confirmation/completed page)
        title_lower = page_metadata.get("title", "").lower()
        body_lower = page_metadata.get("body_text", "").lower()
        has_done_el = any(el.text.strip().lower() in ("done", "finish", "complete") for el in elements)

        if has_done_el or any(w in title_lower or w in body_lower or w in catalog_text.lower() for w in ["thank you", "confirmed", "order placed", "success", "welcome to your dashboard"]):
            return ActionDecision(
                action_type="done",
                target_id="none",
                confidence_action=0.93,
                confidence_target=0.95,
                source="mock",
                reasoning="Mock: page matches target completion state",
            )

        # Look for input to type into (only if not already filled)
        input_keywords = ["search", "email", "name", "query", "user", "address", "phone"]
        for el in elements:
            if el.tag in ("input", "textarea") and el.element_type not in ("button", "submit", "checkbox", "radio"):
                if el.value and len(el.value.strip()) > 0:
                    continue
                el_desc = f"{el.text} {el.placeholder or ''} {el.element_type or ''}".lower()
                for kw in input_keywords:
                    if kw in goal_lower and kw in el_desc:
                        return ActionDecision(
                            action_type="type",
                            target_id=str(el.id),
                            confidence_action=0.91,
                            confidence_target=0.92,
                            source="mock",
                            reasoning=f"Mock: element [{el.id}] matches input keyword '{kw}'",
                        )

        # Look for buttons or links matching words in the goal
        goal_words = [w for w in re.findall(r"\b\w{3,}\b", goal_lower) if w not in {"the", "and", "for", "with", "click", "find", "open", "page"}]
        best_match_el = None
        best_match_score = 0

        for el in elements:
            el_text_lower = (el.text + " " + (el.href or "")).lower()
            score = sum(1 for w in goal_words if w in el_text_lower)
            if score > best_match_score:
                best_match_score = score
                best_match_el = el

        if best_match_el and best_match_score > 0:
            act: ActionType = "type" if best_match_el.tag in ("input", "textarea") else "click"
            return ActionDecision(
                action_type=act,
                target_id=str(best_match_el.id),
                confidence_action=0.88,
                confidence_target=0.89,
                source="mock",
                reasoning=f"Mock: element [{best_match_el.id}] matches goal keyword(s)",
            )

        # Fallback to scroll down or wait if nothing matched
        if len(elements) > 0:
            return ActionDecision(
                action_type="scroll_down",
                target_id="none",
                confidence_action=0.75,
                confidence_target=0.99,
                source="mock",
                reasoning="Mock: no direct element match, scrolling down",
            )

        return ActionDecision(
            action_type="blocked",
            target_id="none",
            confidence_action=0.2,
            confidence_target=0.2,
            source="mock",
            reasoning="Mock: no interactive elements found on page",
        )
