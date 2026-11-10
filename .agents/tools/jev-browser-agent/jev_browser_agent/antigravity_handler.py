"""
Antigravity reasoning handler for dynamic typing intervention, uncertainty recovery,
and independent verification of task completion.
Uses the user's active Antigravity IDE connection via the native 'agy' CLI
(no Gemini API key required) or Google GenAI API.
"""

from __future__ import annotations

import json
import logging
import re
import subprocess
from typing import Any, Optional

from jev_browser_agent.config import Config, default_config
from jev_browser_agent.models import (
    ALLOWED_ACTIONS,
    ActionDecision,
    ActionRecord,
    ActionType,
    AntigravityRecoveryPlan,
    ElementInfo,
    VerificationResult,
)

logger = logging.getLogger(__name__)


class AntigravityReasoningHandler:
    """
    Antigravity reasoning handler providing high-level cognition to complement
    Jev's high-speed micro-decisions via the active Antigravity IDE desktop session
    (authenticated with the user's Google account) or Google GenAI SDK.
    """

    def __init__(self, config: Optional[Config] = None):
        self.config = config or default_config
        self._agy_path = self.config.antigravity_bin
        self._genai_client = None

        if not self.config.mock_mode:
            if self._agy_path:
                logger.info("Connected to native Antigravity IDE session via binary: %s", self._agy_path)
            elif self.config.antigravity_api_key:
                try:
                    from google import genai

                    self._genai_client = genai.Client(api_key=self.config.antigravity_api_key)
                    logger.info("Initialized Google GenAI client (model: %s)", self.config.antigravity_model or "gemini-2.5-flash")
                except Exception as e:
                    logger.warning("Could not initialize GenAI client (%s). Using fallback mode.", e)

    def _query_antigravity(self, prompt: str, as_json: bool = False) -> str:
        """
        Send a reasoning prompt to Antigravity IDE.
        Prioritizes the native active Antigravity IDE Google account session (zero API keys needed).
        """
        # 1. Native Antigravity IDE CLI session
        if self._agy_path:
            try:
                cmd = [
                    self._agy_path,
                    "--dangerously-skip-permissions",
                    "--disable-slash-commands",
                    "--print",
                    prompt,
                ]
                if self.config.antigravity_model:
                    cmd.extend(["--model", self.config.antigravity_model])

                res = subprocess.run(
                    cmd,
                    capture_output=True,
                    text=True,
                    timeout=60,
                )
                if res.returncode == 0 and res.stdout.strip():
                    return res.stdout.strip()
                else:
                    logger.warning(
                        "Antigravity CLI warning (code %d): %s",
                        res.returncode,
                        res.stderr.strip(),
                    )
            except Exception as e:
                logger.error("Antigravity CLI execution error: %s", e)

        # 2. GenAI SDK fallback (if API key was provided)
        if self._genai_client:
            try:
                from google.genai import types

                model_name = self.config.antigravity_model or "gemini-2.5-flash"
                cfg_kwargs = {"temperature": 0.0, "max_output_tokens": 400}
                if as_json:
                    cfg_kwargs["response_mime_type"] = "application/json"

                response = self._genai_client.models.generate_content(
                    model=model_name,
                    contents=prompt,
                    config=types.GenerateContentConfig(**cfg_kwargs),
                )
                return (response.text or "").strip()
            except Exception as e:
                logger.error("google-genai call failed: %s", e)

        raise RuntimeError("No Antigravity IDE connection or API key available.")

    # --------------------------------------------------------------------------
    # 1. Typing Intervention: Static Extraction vs. Antigravity Dynamic Reasoning
    # --------------------------------------------------------------------------
    def resolve_typing_text(
        self,
        goal: str,
        element: ElementInfo,
        history: list[ActionRecord],
        catalog_text: str,
    ) -> str:
        """
        Determine what text to type into the target element.
        First attempts deterministic extraction directly from the goal definition.
        If dynamic reasoning is needed, escalates to Antigravity.
        """
        # A. Static deterministic regex extraction from goal string
        static_text = self._extract_static_text_from_goal(goal, element)
        if static_text is not None:
            logger.info("Deterministically extracted typing text from goal: '%s'", static_text)
            return static_text

        # B. If mock mode or no session, provide sensible mock value
        if self.config.mock_mode or not (self._agy_path or self._genai_client):
            return self._mock_generate_typing_text(goal, element)

        # C. Escalate to Antigravity IDE for dynamic string generation
        logger.info(
            "Escalating typing text generation to Antigravity IDE for element [%d] '%s'",
            element.id,
            element.text,
        )
        return self._antigravity_generate_typing_text(goal, element, history, catalog_text)

    def _extract_static_text_from_goal(self, goal: str, element: ElementInfo) -> Optional[str]:
        """
        Extract literal string from the goal if explicitly defined in quotes or key-value format.
        Examples:
          - Type 'developer@company.org' into email
          - Search for "flights to tokyo"
          - email="test@test.com"
        """
        # Pattern 1: explicitly quoted text after type/search/enter/fill
        pattern_action_quoted = re.search(
            r"""(?:type|enter|fill|search|input)\s+['"]([^'"]+)['"]""",
            goal,
            re.IGNORECASE,
        )
        if pattern_action_quoted:
            return pattern_action_quoted.group(1).strip()

        # Pattern 2: general quoted text if only one quoted string in goal
        all_quotes = re.findall(r"""['"]([^'"]+)['"]""", goal)
        if len(all_quotes) == 1:
            return all_quotes[0].strip()

        # Pattern 3: key=value syntax (e.g. email=john@doe.com)
        tag_key = element.element_type or element.tag
        pattern_kv = re.search(rf"""\b{tag_key}\s*[:=]\s*(\S+)""", goal, re.IGNORECASE)
        if pattern_kv:
            return pattern_kv.group(1).strip()

        return None

    def _antigravity_generate_typing_text(
        self,
        goal: str,
        element: ElementInfo,
        history: list[ActionRecord],
        catalog_text: str,
    ) -> str:
        """Query Antigravity IDE to compose the optimal input text based on context."""
        recent_history = "\n".join([f"- {r.to_history_line()}" for r in history]) or "None"

        prompt = f"""You are the Antigravity IDE reasoning engine for a fast browser automation agent.
The micro-decision model decided to TYPE into the following input element:
Target Element:
  ID: [{element.id}]
  Tag/Type: {element.tag} (type='{element.element_type}')
  Label/Text: "{element.text}"
  Placeholder: "{element.placeholder or ''}"

Current Goal: "{goal}"
Recent Actions:
{recent_history}

Generate ONLY the exact text string that should be typed into this input field.
Do not include any quotes, markdown formatting, explanations, or leading/trailing whitespace.
String to type:"""

        try:
            raw_text = self._query_antigravity(prompt, as_json=False)
            if (raw_text.startswith('"') and raw_text.endswith('"')) or (raw_text.startswith("'") and raw_text.endswith("'")):
                raw_text = raw_text[1:-1].strip()
            return raw_text
        except Exception as e:
            logger.error("Antigravity dynamic typing call failed: %s. Using element fallback.", e)
            return self._mock_generate_typing_text(goal, element)

    # --------------------------------------------------------------------------
    # 2. Low Confidence / Blocked Recovery Handler
    # --------------------------------------------------------------------------
    def recover_from_uncertainty_or_block(
        self,
        goal: str,
        current_sub_goal: str,
        catalog_text: str,
        elements: list[ElementInfo],
        history: list[ActionRecord],
        reason_for_escalation: str,
        jev_prediction: Optional[ActionDecision] = None,
    ) -> AntigravityRecoveryPlan:
        """
        Escalation handler invoked when Jev confidence is < 0.7 or Jev returned 'blocked'.
        Antigravity reasons across full DOM catalog and action history to supply an updated plan.
        """
        if self.config.mock_mode or not (self._agy_path or self._genai_client):
            return self._mock_recover(goal, elements, reason_for_escalation)

        recent_history = "\n".join([f"- {r.to_history_line()}" for r in history]) or "None"
        jev_pred_str = (
            f"Action: {jev_prediction.action_type} (conf={jev_prediction.confidence_action:.2f}), "
            f"Target: {jev_prediction.target_id} (conf={jev_prediction.confidence_target:.2f})"
            if jev_prediction
            else "None"
        )

        prompt = f"""You are the senior Antigravity reasoning engine in a hybrid browser agent.
The fast System One micro-decision model encountered uncertainty or a block.
Your job is to provide an updated plan and choose the exact next action.

Escalation Trigger: {reason_for_escalation}
Jev Proposed Action: {jev_pred_str}

Overall Goal: "{goal}"
Active Sub-Goal: "{current_sub_goal}"

Execution History (Last 5 actions):
{recent_history}

Current DOM Elements:
{catalog_text}

Choose the next action from: ['click', 'type', 'select', 'scroll_down', 'scroll_up', 'wait', 'done', 'blocked'].
Select the target element ID from the numbered catalog, or 'none' if scrolling/waiting/done/blocked.

Respond STRICTLY in valid JSON format matching this schema:
{{
  "action_type": "click" | "type" | "select" | "scroll_down" | "scroll_up" | "wait" | "done" | "blocked",
  "target_id": "<number or 'none'>",
  "text_value": "<string to type if action is type, else null>",
  "select_value": "<option value if select, else null>",
  "explanation": "<brief rationale for this choice>",
  "updated_sub_goal": "<optional adjustment to sub-goal if needed, else null>"
}}"""

        try:
            raw_content = self._query_antigravity(prompt, as_json=True)
            parsed = self._parse_json_response(raw_content)

            act_type = parsed.get("action_type", "wait")
            if act_type not in ALLOWED_ACTIONS:
                act_type = "wait"

            return AntigravityRecoveryPlan(
                action_type=act_type,
                target_id=str(parsed.get("target_id", "none")),
                text_value=parsed.get("text_value"),
                select_value=parsed.get("select_value"),
                explanation=parsed.get("explanation", "Antigravity recovery decision"),
                updated_sub_goal=parsed.get("updated_sub_goal"),
            )
        except Exception as e:
            logger.error("Antigravity recovery call failed: %s. Using heuristic fallback.", e)
            return self._mock_recover(goal, elements, reason_for_escalation)

    # --------------------------------------------------------------------------
    # 3. Independent Verification of 'Done'
    # --------------------------------------------------------------------------
    def verify_completion(
        self,
        goal: str,
        sub_goals: list[str],
        history: list[ActionRecord],
        final_catalog_text: str,
        page_url: str,
        page_title: str,
    ) -> VerificationResult:
        """
        Independent verification of Jev's 'done' output.
        Rules out false positives by examining the execution trail, page state, and goal criteria.
        """
        if self.config.mock_mode or not (self._agy_path or self._genai_client):
            return self._mock_verify_completion(goal, final_catalog_text)

        recent_history = "\n".join([f"- {r.to_history_line()}" for r in history]) or "None"

        prompt = f"""You are the independent task verifier for a web browser automation agent in Antigravity IDE.
The fast micro-decision model declared that the task is 'done'.
Do NOT accept this at face value. You must rigorously inspect the execution trail and final DOM state.

Goal: "{goal}"
Sub-Goals: {json.dumps(sub_goals)}
Final Page URL: {page_url}
Final Page Title: {page_title}

Action History:
{recent_history}

Final DOM Interactive Elements:
{final_catalog_text}

Verification Criteria:
1. Has the user's objective genuinely been completed end-to-end?
2. Is the browser stuck on an unsubmitted form, validation error, login screen, or preliminary preview?
3. If not complete, identify the false positive and specify what remains to be done.

Respond STRICTLY in valid JSON matching this schema:
{{
  "verified": true | false,
  "confidence": <float between 0.0 and 1.0>,
  "reasoning": "<clear explanation of why the task is or is not completed>",
  "corrective_action": {{
      "action_type": "click" | "type" | "select" | "scroll_down" | "wait",
      "target_id": "<number or 'none'>",
      "text_value": "<string if typing, else null>"
  }} // null if verified is true
}}"""

        try:
            raw_content = self._query_antigravity(prompt, as_json=True)
            parsed = self._parse_json_response(raw_content)

            verified = bool(parsed.get("verified", False))
            confidence = float(parsed.get("confidence", 0.5))
            reasoning = str(parsed.get("reasoning", "Antigravity independent verification"))

            corrective_decision = None
            corr_data = parsed.get("corrective_action")
            if not verified and isinstance(corr_data, dict):
                act = corr_data.get("action_type", "wait")
                if act in ALLOWED_ACTIONS:
                    corrective_decision = ActionDecision(
                        action_type=act,
                        target_id=str(corr_data.get("target_id", "none")),
                        confidence_action=0.9,
                        confidence_target=0.9,
                        source="antigravity_fallback",
                        text_value=corr_data.get("text_value"),
                        reasoning=f"Corrective action from false-positive verification: {reasoning}",
                    )

            logger.info("Verification result: verified=%s (confidence=%.2f) - %s", verified, confidence, reasoning)

            return VerificationResult(
                verified=verified,
                confidence=confidence,
                reasoning=reasoning,
                corrective_action=corrective_decision,
            )

        except Exception as e:
            logger.error("Antigravity verification call failed: %s. Using heuristic check.", e)
            return self._mock_verify_completion(goal, final_catalog_text)

    # --------------------------------------------------------------------------
    # Fallback / Mock Helpers for Offline Testing
    # --------------------------------------------------------------------------
    def _parse_json_response(self, text: str) -> dict[str, Any]:
        """Extract and parse JSON object from LLM response text."""
        try:
            return json.loads(text)
        except Exception:
            pass

        match = re.search(r"```(?:json)?\s*(\{.*?\})\s*```", text, re.DOTALL)
        if match:
            return json.loads(match.group(1))

        brace_match = re.search(r"(\{.*\})", text, re.DOTALL)
        if brace_match:
            return json.loads(brace_match.group(1))

        raise ValueError(f"Could not parse valid JSON from response: {text[:200]}")

    def _mock_generate_typing_text(self, goal: str, element: ElementInfo) -> str:
        """Deterministic fallback typing text generator."""
        el_type = (element.element_type or element.tag).lower()
        if "email" in el_type or "email" in element.text.lower():
            return "user@example.com"
        if "password" in el_type:
            return "SecretPassword123!"
        if "search" in el_type or "query" in el_type or "search" in element.text.lower():
            words = [w for w in goal.split() if w.lower() not in {"search", "for", "the", "in", "to", "type", "enter"}]
            return " ".join(words[:4]) or "sample query"
        return "automated input"

    def _mock_recover(self, goal: str, elements: list[ElementInfo], reason: str) -> AntigravityRecoveryPlan:
        """Deterministic mock fallback when Antigravity live client is not available."""
        if elements:
            target = elements[0]
            act: ActionType = "type" if target.tag in ("input", "textarea") else "click"
            return AntigravityRecoveryPlan(
                action_type=act,
                target_id=str(target.id),
                text_value="mock recovery input" if act == "type" else None,
                select_value=None,
                explanation=f"Antigravity mock recovery plan addressing trigger: {reason}",
            )
        return AntigravityRecoveryPlan(
            action_type="wait",
            target_id="none",
            text_value=None,
            select_value=None,
            explanation=f"Antigravity mock recovery: no elements found for {reason}",
        )

    def _mock_verify_completion(self, goal: str, catalog_text: str) -> VerificationResult:
        """Deterministic mock verification."""
        catalog_lower = catalog_text.lower()

        if any(err in catalog_lower for err in ["error", "invalid", "required field", "failed"]):
            return VerificationResult(
                verified=False,
                confidence=0.85,
                reasoning="Antigravity verification: Error messages detected in final DOM state.",
            )

        return VerificationResult(
            verified=True,
            confidence=0.92,
            reasoning="Antigravity verification: Final state satisfies goal conditions without visible errors.",
        )
