"""
Backwards-compatible bridge for reasoning handler.
Points to AntigravityReasoningHandler.
"""

from jev_browser_agent.antigravity_handler import AntigravityReasoningHandler

# Aliases for backward compatibility
ClaudeReasoningHandler = AntigravityReasoningHandler
LLMReasoningHandler = AntigravityReasoningHandler

__all__ = ["AntigravityReasoningHandler", "ClaudeReasoningHandler", "LLMReasoningHandler"]
