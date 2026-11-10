"""
Jev + Antigravity IDE Hybrid Fast-Browser Agent.
Pairs TypeSafe's Jev (System One) micro-decision model with the Antigravity IDE
reasoning engine for ultra-fast, cheap, and robust web automation.
"""

from jev_browser_agent.agent import AgentRunResult, HybridFastBrowserAgent
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
    AntigravityRecoveryPlan,
    ElementInfo,
    LLMRecoveryPlan,
    VerificationResult,
)

__all__ = [
    "ActionDecision",
    "ActionRecord",
    "ActionType",
    "AgentRunResult",
    "AntigravityReasoningHandler",
    "AntigravityRecoveryPlan",
    "Config",
    "CookieModalGuard",
    "DOMExtractor",
    "ElementInfo",
    "HybridFastBrowserAgent",
    "JevDecisionClient",
    "LLMRecoveryPlan",
    "SlidingMemoryWindow",
    "SubGoalManager",
    "VerificationResult",
    "default_config",
]
