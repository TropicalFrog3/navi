"""
Data models and typed structures for Jev + Antigravity IDE hybrid browser agent.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Literal, Optional

# Supported primary action primitives
ActionType = Literal[
    "click",
    "type",
    "select",
    "scroll_down",
    "scroll_up",
    "wait",
    "done",
    "blocked",
]

ALLOWED_ACTIONS: list[ActionType] = [
    "click",
    "type",
    "select",
    "scroll_down",
    "scroll_up",
    "wait",
    "done",
    "blocked",
]


@dataclass
class ElementInfo:
    """Represents an extracted interactive DOM element indexed with a numeric ID."""

    id: int
    tag: str
    role: str
    text: str
    selector: str
    element_type: Optional[str] = None
    placeholder: Optional[str] = None
    value: Optional[str] = None
    checked: Optional[bool] = None
    href: Optional[str] = None
    options: list[str] = field(default_factory=list)
    in_modal: bool = False
    is_cookie_related: bool = False

    @property
    def summary(self) -> str:
        """Formatted representation for the numbered catalog."""
        details: list[str] = []
        if self.element_type:
            details.append(f"type='{self.element_type}'")
        if self.placeholder:
            details.append(f"placeholder='{self.placeholder}'")
        if self.value:
            details.append(f"value='{self.value}'")
        if self.checked is not None:
            details.append(f"checked={self.checked}")
        if self.href:
            href_clean = self.href if len(self.href) <= 40 else self.href[:37] + "..."
            details.append(f"href='{href_clean}'")
        if self.options:
            opts_preview = ", ".join(self.options[:4])
            if len(self.options) > 4:
                opts_preview += f" (+{len(self.options) - 4} more)"
            details.append(f"options=[{opts_preview}]")
        if self.in_modal:
            details.append("in_modal=True")

        text_clean = " ".join(self.text.split()) if self.text else ""
        if len(text_clean) > 60:
            text_clean = text_clean[:57] + "..."

        type_label = f"{self.tag}[{self.element_type}]" if self.element_type else self.tag
        if self.role and self.role != self.tag:
            type_label += f"({self.role})"

        details_str = f" ({', '.join(details)})" if details else ""
        text_str = f" \"{text_clean}\"" if text_clean else ""

        return f"[{self.id}] {type_label}{text_str}{details_str}"


@dataclass
class ActionDecision:
    """Action decision resolved by either Jev or Antigravity."""

    action_type: ActionType
    target_id: str  # Number string e.g. "1" or "none"
    confidence_action: float
    confidence_target: float
    source: Literal["jev", "antigravity_fallback", "cookie_safeguard", "mock"]
    text_value: Optional[str] = None
    select_value: Optional[str] = None
    reasoning: str = ""
    is_cookie_banner: bool = False

    @property
    def is_high_confidence(self) -> bool:
        """Checks if both action and target meet confidence criteria (>= 0.7)."""
        return self.confidence_action >= 0.7 and self.confidence_target >= 0.7


@dataclass
class ActionRecord:
    """Single record in the sliding memory window."""

    step: int
    action_type: ActionType
    target_id: str
    element_summary: str
    source: str
    confidence_scores: tuple[float, float]
    input_value: Optional[str] = None
    outcome: str = ""
    url_after: str = ""

    def to_history_line(self) -> str:
        """Concise string format for reasoning engine context window."""
        val_str = f", input='{self.input_value}'" if self.input_value else ""
        target_str = f" on {self.element_summary}" if self.target_id != "none" else ""
        return (
            f"Step {self.step} [{self.source}]: {self.action_type}{target_str}{val_str} "
            f"-> {self.outcome}"
        )


@dataclass
class AntigravityRecoveryPlan:
    """Plan supplied by Antigravity when Jev is uncertain or blocked."""

    action_type: ActionType
    target_id: str
    text_value: Optional[str]
    select_value: Optional[str]
    explanation: str
    updated_sub_goal: Optional[str] = None


# Alias for backwards compatibility
LLMRecoveryPlan = AntigravityRecoveryPlan


@dataclass
class VerificationResult:
    """Independent verification evaluation from Antigravity."""

    verified: bool
    confidence: float
    reasoning: str
    corrective_action: Optional[ActionDecision] = None
