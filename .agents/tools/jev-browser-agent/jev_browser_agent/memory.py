"""
Memory constraints and sliding action window for the hybrid browser agent.
Maintains a strict sliding window of the last 5 actions to prevent hallucination loops,
and decomposes objectives into chained single-page sub-goals.
"""

from __future__ import annotations

import logging
from collections import deque
from typing import Optional

from jev_browser_agent.models import ActionDecision, ActionRecord, ActionType, ElementInfo

logger = logging.getLogger(__name__)


class SlidingMemoryWindow:
    """
    Sliding memory window strictly limited to the last N actions (default: 5).
    Evicts older actions automatically via a bounded deque to avoid token bloat
    and break repetitive hallucination loops.
    """

    def __init__(self, max_size: int = 5):
        self.max_size = max_size
        self._window: deque[ActionRecord] = deque(maxlen=max_size)
        self.total_actions_count: int = 0

    def add_action(
        self,
        step: int,
        decision: ActionDecision,
        element: Optional[ElementInfo],
        outcome: str,
        url_after: str,
    ) -> ActionRecord:
        """Record an executed action into the sliding window."""
        element_summary = element.summary if element else "(none)"
        record = ActionRecord(
            step=step,
            action_type=decision.action_type,
            target_id=decision.target_id,
            element_summary=element_summary,
            source=decision.source,
            confidence_scores=(decision.confidence_action, decision.confidence_target),
            input_value=decision.text_value or decision.select_value,
            outcome=outcome,
            url_after=url_after,
        )
        self._window.append(record)
        self.total_actions_count += 1
        return record

    def get_recent_history(self) -> list[ActionRecord]:
        """Return the records in the active sliding window."""
        return list(self._window)

    def format_for_prompt(self) -> str:
        """Format the sliding memory window into a compact text block for LLM prompts."""
        if not self._window:
            return "(No previous actions taken yet)"

        lines = [f"Last {len(self._window)} Actions (Sliding Window):"]
        for record in self._window:
            lines.append(f"  • {record.to_history_line()}")
        return "\n".join(lines)

    def detect_loop(self) -> tuple[bool, str]:
        """
        Detect if the agent is stuck in an execution loop (e.g. repeated actions).
        Returns (is_looping, explanation).
        """
        if len(self._window) < 3:
            return False, ""

        records = list(self._window)

        # 1. Check identical consecutive actions (last 3 actions identical action and target)
        last_three = records[-3:]
        if (
            last_three[0].action_type == last_three[1].action_type == last_three[2].action_type
            and last_three[0].target_id == last_three[1].target_id == last_three[2].target_id
        ):
            return (
                True,
                f"Loop detected: Action '{last_three[0].action_type}' on target '{last_three[0].target_id}' repeated 3 times without progression.",
            )

        # 2. Check ping-pong between 2 elements (A -> B -> A -> B)
        if len(records) >= 4:
            r = records[-4:]
            if (
                r[0].target_id == r[2].target_id
                and r[1].target_id == r[3].target_id
                and r[0].target_id != r[1].target_id
            ):
                return (
                    True,
                    f"Ping-pong oscillation detected between targets '{r[0].target_id}' and '{r[1].target_id}'.",
                )

        return False, ""

    def clear(self) -> None:
        """Reset the sliding memory window."""
        self._window.clear()
        self.total_actions_count = 0


class SubGoalManager:
    """
    Decomposes and manages chained single-page/single-form sub-goals.
    Prevents overwhelming the micro-decision classifier with end-to-end complexity.
    """

    def __init__(self, overall_goal: str, sub_goals: Optional[list[str]] = None):
        self.overall_goal = overall_goal
        if sub_goals and len(sub_goals) > 0:
            self.sub_goals = sub_goals
        else:
            # Default to a single sub-goal identical to the overall goal
            self.sub_goals = [overall_goal]
        self.current_index: int = 0

    @property
    def current_sub_goal(self) -> str:
        """Returns the active sub-goal currently being targeted."""
        if self.current_index < len(self.sub_goals):
            return self.sub_goals[self.current_index]
        return self.overall_goal

    def advance_sub_goal(self) -> bool:
        """
        Move to the next sub-goal in sequence.
        Returns True if more sub-goals remain, or False if all have completed.
        """
        if self.current_index < len(self.sub_goals) - 1:
            self.current_index += 1
            logger.info("Advanced to next sub-goal: %s", self.current_sub_goal)
            return True
        return False

    def update_sub_goals(self, new_sub_goals: list[str]) -> None:
        """Replace remaining sub-goals with an updated sequence (e.g. from Antigravity)."""
        if new_sub_goals:
            self.sub_goals = new_sub_goals
            self.current_index = 0
            logger.info("Sub-goals updated by reasoning engine: %s", self.sub_goals)

    def summary(self) -> str:
        """Human-readable sub-goal progress summary."""
        lines = [f"Overall Goal: {self.overall_goal}"]
        for idx, sg in enumerate(self.sub_goals):
            status = " [ACTIVE]" if idx == self.current_index else (" [DONE]" if idx < self.current_index else "")
            lines.append(f"  {idx + 1}. {sg}{status}")
        return "\n".join(lines)

