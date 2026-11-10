"""
Hybrid Fast-Browser Agent core runner.
Implements the dual-agent architecture pairing Jev's ultra-fast System One
micro-decisions with the Antigravity IDE / Gemini reasoning engine.
"""

from __future__ import annotations

import asyncio
import logging
import time
from dataclasses import dataclass
from typing import Any, Optional

from playwright.async_api import Browser, BrowserContext, Page, async_playwright

from jev_browser_agent.antigravity_handler import AntigravityReasoningHandler
from jev_browser_agent.config import Config, default_config
from jev_browser_agent.cookie_guard import CookieModalGuard
from jev_browser_agent.dom import DOMExtractor
from jev_browser_agent.jev_client import JevDecisionClient
from jev_browser_agent.memory import SlidingMemoryWindow, SubGoalManager
from jev_browser_agent.models import (
    ActionDecision,
    ActionRecord,
    ActionType,
    ElementInfo,
    VerificationResult,
)

logger = logging.getLogger(__name__)


@dataclass
class AgentRunResult:
    """Final outcome summary of an agent execution run."""

    success: bool
    total_steps: int
    goal: str
    final_url: str
    history: list[ActionRecord]
    verification: Optional[VerificationResult]
    summary: str


class HybridFastBrowserAgent:
    """
    Hybrid fast-browser agent pairing Jev (TypeSafe System One) and Antigravity IDE.

    Key Architectural Invariants:
    1. DOM to numbers (no screenshots/pixels passed to decision engine).
    2. Batched Jev micro-decision queries (action type + target element ID).
    3. Deterministic execution threshold: Pure code execution when both confidences >= 0.7.
    4. Text typing intervention: Direct goal extraction or dynamic Antigravity escalation.
    5. Escalation: Antigravity handles low confidence (<0.7) or blocked states.
    6. Independent verification: Antigravity independently verifies 'done' to eliminate false positives.
    7. Memory constraint: Sliding memory window strictly capped at last 5 actions.
    """

    def __init__(self, config: Optional[Config] = None):
        self.config = config or default_config
        self.dom_extractor = DOMExtractor()
        self.jev_client = JevDecisionClient(self.config)
        self.llm_handler = AntigravityReasoningHandler(self.config)
        self.cookie_guard = CookieModalGuard()
        self.memory = SlidingMemoryWindow(max_size=self.config.sliding_window_size)

    async def run(
        self,
        start_url: str,
        goal: str,
        sub_goals: Optional[list[str]] = None,
        page: Optional[Page] = None,
    ) -> AgentRunResult:
        """
        Execute the hybrid agent loop for a given objective.

        Args:
            start_url: Initial webpage URL to navigate to.
            goal: Target end-to-end user goal.
            sub_goals: Optional chained list of single-page/single-form sub-goals.
            page: Optional existing Playwright Page (for test harness reuse).

        Returns:
            AgentRunResult containing execution trail, verification, and status.
        """
        self.memory.clear()
        sub_goal_manager = SubGoalManager(overall_goal=goal, sub_goals=sub_goals)

        owns_browser = page is None
        playwright_cm = None
        browser: Optional[Browser] = None
        context: Optional[BrowserContext] = None

        if owns_browser:
            playwright_cm = async_playwright()
            pw = await playwright_cm.__aenter__()
            browser = await pw.chromium.launch(
                headless=self.config.browser_headless,
                slow_mo=self.config.browser_slow_mo,
            )
            context = await browser.new_context(
                viewport={
                    "width": self.config.viewport_width,
                    "height": self.config.viewport_height,
                }
            )
            page = await context.new_page()

        assert page is not None

        logger.info("=" * 70)
        logger.info("Starting Hybrid Fast-Browser Agent (Antigravity IDE Integration)")
        logger.info("URL: %s", start_url)
        logger.info("Goal: %s", goal)
        logger.info("Sub-goals:\n%s", sub_goal_manager.summary())
        logger.info("=" * 70)

        # Initial navigation (skip if already loaded or testing on about:blank with existing DOM)
        if start_url and start_url != "about:blank":
            try:
                await page.goto(start_url, wait_until="domcontentloaded", timeout=30000)
                await page.wait_for_timeout(1000)
            except Exception as e:
                logger.warning("Initial navigation warning: %s", e)

        step = 0
        final_verification: Optional[VerificationResult] = None
        task_succeeded = False

        try:
            while step < self.config.max_steps:
                step += 1
                step_start_time = time.perf_counter()
                active_sub_goal = sub_goal_manager.current_sub_goal

                logger.info("\n--- [Step %d / %d] Sub-Goal: '%s' ---", step, self.config.max_steps, active_sub_goal)

                # 1. State Extraction: DOM to Numbers (no pixels)
                elements, catalog_text, metadata = await self.dom_extractor.extract_catalog(page)

                # 2. Cookie / Modal Safeguard Check
                if self.config.auto_dismiss_cookie_banners:
                    dismissed = await self.cookie_guard.handle_cookie_banner_if_present(page, elements)
                    if dismissed:
                        logger.info("Cookie banner dismissed. Re-indexing DOM state...")
                        elements, catalog_text, metadata = await self.dom_extractor.extract_catalog(page)

                # 3. Check for Repetitive Loops in Sliding Memory Window
                is_looping, loop_reason = self.memory.detect_loop()
                if is_looping:
                    logger.warning("[!] Loop detection triggered: %s. Forcing Antigravity recovery...", loop_reason)
                    recovery_plan = self.llm_handler.recover_from_uncertainty_or_block(
                        goal=goal,
                        current_sub_goal=active_sub_goal,
                        catalog_text=catalog_text,
                        elements=elements,
                        history=self.memory.get_recent_history(),
                        reason_for_escalation=f"Execution loop: {loop_reason}",
                    )
                    decision = ActionDecision(
                        action_type=recovery_plan.action_type,
                        target_id=recovery_plan.target_id,
                        confidence_action=0.95,
                        confidence_target=0.95,
                        source="antigravity_fallback",
                        text_value=recovery_plan.text_value,
                        select_value=recovery_plan.select_value,
                        reasoning=f"Loop escape intervention: {recovery_plan.explanation}",
                    )
                else:
                    # 4. Batched Jev Micro-Decision Classifier Query
                    criteria_map = self.dom_extractor.format_criteria_map(elements)
                    history_prompt = self.memory.format_for_prompt()

                    decision = self.jev_client.query_action(
                        goal=active_sub_goal,
                        catalog_text=catalog_text,
                        elements=elements,
                        criteria_map=criteria_map,
                        history_text=history_prompt,
                        page_metadata=metadata,
                    )

                    # Handle Jev cookie banner detection flag
                    if decision.is_cookie_banner and self.config.auto_dismiss_cookie_banners:
                        dismissed = await self.cookie_guard.handle_cookie_banner_if_present(
                            page, elements, jev_flagged=True
                        )
                        if dismissed:
                            logger.info("Cookie banner dismissed via Jev flag. Refreshing DOM catalog...")
                            elements, catalog_text, metadata = await self.dom_extractor.extract_catalog(page)

                # 5. Evaluate Decision & Apply Deterministic Execution Rules
                decision_to_execute = decision

                # --- RULE 4: VERIFICATION OF 'DONE' ---
                if decision.action_type == "done":
                    if self.config.jev_only:
                        logger.info("[SUCCESS] Jev declared task 'done' (Jev-Only mode: accepted directly).")
                        task_succeeded = True
                        final_verification = VerificationResult(
                            verified=True,
                            confidence=decision.confidence_action,
                            reasoning="Jev-Only Mode: Direct acceptance of Jev 'done' output.",
                        )
                        self.memory.add_action(
                            step=step,
                            decision=decision,
                            element=None,
                            outcome="Task completed via Jev decision.",
                            url_after=metadata.get("url", ""),
                        )
                        break

                    logger.info("[*] Jev returned 'done'. Initiating independent Antigravity verification check...")
                    verification = self.llm_handler.verify_completion(
                        goal=goal,
                        sub_goals=sub_goal_manager.sub_goals,
                        history=self.memory.get_recent_history(),
                        final_catalog_text=catalog_text,
                        page_url=metadata.get("url", ""),
                        page_title=metadata.get("title", ""),
                    )
                    final_verification = verification

                    if verification.verified:
                        logger.info("[SUCCESS] Antigravity VERIFIED task completion: %s", verification.reasoning)
                        if sub_goal_manager.advance_sub_goal():
                            logger.info("Advanced to next sub-goal. Continuing execution.")
                            continue
                        else:
                            task_succeeded = True
                            self.memory.add_action(
                                step=step,
                                decision=decision,
                                element=None,
                                outcome=f"Task completed and verified by Antigravity: {verification.reasoning}",
                                url_after=metadata.get("url", ""),
                            )
                            break
                    else:
                        logger.warning(
                            "[FALSE POSITIVE] Antigravity rejected 'done' declaration: %s",
                            verification.reasoning,
                        )
                        if verification.corrective_action:
                            decision_to_execute = verification.corrective_action
                        else:
                            recovery = self.llm_handler.recover_from_uncertainty_or_block(
                                goal=goal,
                                current_sub_goal=active_sub_goal,
                                catalog_text=catalog_text,
                                elements=elements,
                                history=self.memory.get_recent_history(),
                                reason_for_escalation=f"False positive 'done': {verification.reasoning}",
                            )
                            decision_to_execute = ActionDecision(
                                action_type=recovery.action_type,
                                target_id=recovery.target_id,
                                confidence_action=0.9,
                                confidence_target=0.9,
                                source="antigravity_fallback",
                                text_value=recovery.text_value,
                                select_value=recovery.select_value,
                                reasoning=recovery.explanation,
                            )

                # --- RULE 3: LOW CONFIDENCE (< 0.7) OR BLOCKED ESCALATION ---
                elif not decision.is_high_confidence or decision.action_type == "blocked":
                    if self.config.jev_only:
                        logger.info("[!] Low confidence / blocked in Jev-Only mode. Applying deterministic heuristic fallback.")
                        act_type = "scroll_down" if (decision.action_type == "blocked" or decision.target_id == "none") else "click"
                        tgt = "none" if act_type == "scroll_down" else (str(elements[0].id) if elements else "none")
                        decision_to_execute = ActionDecision(
                            action_type=act_type,
                            target_id=tgt,
                            confidence_action=0.75,
                            confidence_target=0.75,
                            source="jev",
                            reasoning="Jev-only heuristic fallback",
                        )
                    else:
                        reason = (
                            f"Low confidence (action={decision.confidence_action:.2f}, target={decision.confidence_target:.2f})"
                            if decision.action_type != "blocked"
                            else f"Jev indicated blocked: {decision.reasoning}"
                        )
                        logger.info("[!] Escalating to Antigravity (%s)", reason)
                        recovery_plan = self.llm_handler.recover_from_uncertainty_or_block(
                            goal=goal,
                            current_sub_goal=active_sub_goal,
                            catalog_text=catalog_text,
                            elements=elements,
                            history=self.memory.get_recent_history(),
                            reason_for_escalation=reason,
                            jev_prediction=decision,
                        )
                        if recovery_plan.updated_sub_goal:
                            sub_goal_manager.update_sub_goals([recovery_plan.updated_sub_goal])

                        decision_to_execute = ActionDecision(
                            action_type=recovery_plan.action_type,
                            target_id=recovery_plan.target_id,
                            confidence_action=0.95,
                            confidence_target=0.95,
                            source="antigravity_fallback",
                            text_value=recovery_plan.text_value,
                            select_value=recovery_plan.select_value,
                            reasoning=f"Antigravity recovery: {recovery_plan.explanation}",
                        )

                # --- RULE 2: TEXT TYPING INTERVENTION ---
                target_element: Optional[ElementInfo] = None
                if decision_to_execute.target_id != "none":
                    try:
                        target_id_int = int(decision_to_execute.target_id)
                        target_element = next((el for el in elements if el.id == target_id_int), None)
                    except ValueError:
                        target_element = None

                if decision_to_execute.action_type == "type" and target_element:
                    if not decision_to_execute.text_value:
                        resolved_text = self.llm_handler.resolve_typing_text(
                            goal=active_sub_goal,
                            element=target_element,
                            history=self.memory.get_recent_history(),
                            catalog_text=catalog_text,
                        )
                        decision_to_execute.text_value = resolved_text

                # 6. Execute Action Deterministically in Playwright
                outcome_desc = await self._execute_action_in_browser(page, decision_to_execute, target_element)
                url_after = page.url

                # 7. Record in Sliding Memory Window (Last 5 Actions Capped)
                self.memory.add_action(
                    step=step,
                    decision=decision_to_execute,
                    element=target_element,
                    outcome=outcome_desc,
                    url_after=url_after,
                )

                elapsed_ms = (time.perf_counter() - step_start_time) * 1000
                logger.info(
                    "Step %d executed in %.1f ms | Source: %s | Action: %s -> %s",
                    step,
                    elapsed_ms,
                    decision_to_execute.source,
                    decision_to_execute.action_type,
                    outcome_desc,
                )

                if decision_to_execute.action_type == "done" and task_succeeded:
                    break

        finally:
            if owns_browser and context and browser and playwright_cm:
                await context.close()
                await browser.close()
                await playwright_cm.__aexit__(None, None, None)

        summary_msg = (
            f"Goal '{goal}' {'SUCCEEDED' if task_succeeded else 'TERMINATED'} after {step} steps. "
            f"Verified: {final_verification.verified if final_verification else 'N/A'}"
        )

        return AgentRunResult(
            success=task_succeeded,
            total_steps=step,
            goal=goal,
            final_url=page.url if page else "",
            history=self.memory.get_recent_history(),
            verification=final_verification,
            summary=summary_msg,
        )

    async def _execute_action_in_browser(
        self,
        page: Page,
        decision: ActionDecision,
        target_el: Optional[ElementInfo],
    ) -> str:
        """
        Execute deterministic micro-action in pure code via Playwright locator.
        """
        action = decision.action_type

        try:
            if action == "click":
                if not target_el:
                    return "Click skipped: target element not found in catalog"
                locator = page.locator(target_el.selector)
                await locator.click(timeout=5000)
                await page.wait_for_timeout(500)
                return f"Clicked [{target_el.id}] '{target_el.text}'"

            elif action == "type":
                if not target_el:
                    return "Type skipped: target element not found in catalog"
                text_to_type = decision.text_value or ""
                locator = page.locator(target_el.selector)
                await locator.fill(text_to_type, timeout=5000)
                if (target_el.element_type == "search" or "search" in target_el.text.lower()) and "enter" in decision.reasoning.lower():
                    await locator.press("Enter")
                    await page.wait_for_timeout(800)
                return f"Typed '{text_to_type}' into [{target_el.id}]"

            elif action == "select":
                if not target_el:
                    return "Select skipped: target element not found"
                select_val = decision.select_value or (target_el.options[0] if target_el.options else "")
                locator = page.locator(target_el.selector)
                await locator.select_option(label=select_val)
                return f"Selected '{select_val}' in [{target_el.id}]"

            elif action == "scroll_down":
                await page.evaluate("window.scrollBy({top: window.innerHeight * 0.7, behavior: 'smooth'})")
                await page.wait_for_timeout(500)
                return "Scrolled down 70% of viewport"

            elif action == "scroll_up":
                await page.evaluate("window.scrollBy({top: -window.innerHeight * 0.7, behavior: 'smooth'})")
                await page.wait_for_timeout(500)
                return "Scrolled up 70% of viewport"

            elif action == "wait":
                await page.wait_for_timeout(1500)
                return "Waited 1.5s for dynamic content"

            elif action == "done":
                return "Task completion acknowledged"

            elif action == "blocked":
                return f"Execution blocked: {decision.reasoning}"

            return f"Unhandled action type: {action}"

        except Exception as e:
            logger.error("Error executing action '%s' on %s: %s", action, target_el.selector if target_el else "page", e)
            return f"Execution failed: {str(e)}"
