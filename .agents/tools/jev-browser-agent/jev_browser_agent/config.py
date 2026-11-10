"""
Configuration management for Jev + Antigravity IDE Hybrid Fast-Browser Agent.
Loads configuration from environment variables, .env file, and active Antigravity IDE session.
"""

from __future__ import annotations

import os
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

from dotenv import load_dotenv

# Automatically load .env if present
env_path = Path(__file__).resolve().parent.parent / ".env"
if env_path.exists():
    load_dotenv(dotenv_path=env_path)
else:
    load_dotenv()


def find_antigravity_binary() -> Optional[str]:
    """Detect if the Antigravity CLI (agy) is available on the machine."""
    path = shutil.which("agy")
    if path:
        return path
    candidate = os.path.expanduser("~/.local/bin/agy")
    if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
        return candidate
    return None


@dataclass
class Config:
    """Agent runtime configuration settings."""

    # TypeSafe / Jev Settings
    typesafe_api_key: str = (
        os.getenv("TYPESAFE_API_KEY")
        or os.getenv("JEV_API_KEY")
        or ""
    )
    typesafe_base_url: str = os.getenv("TYPESAFE_BASE_URL", "https://api.typesafe.ai")
    typesafe_model: str = os.getenv("TYPESAFE_MODEL", "jev-latest")

    # Antigravity IDE Settings
    # If logged into Antigravity IDE with your Google Account, no API key is required!
    antigravity_bin: Optional[str] = find_antigravity_binary()
    antigravity_api_key: str = (
        os.getenv("GEMINI_API_KEY")
        or os.getenv("ANTIGRAVITY_API_KEY")
        or os.getenv("GOOGLE_API_KEY")
        or ""
    )
    antigravity_model: str = (
        os.getenv("ANTIGRAVITY_MODEL")
        or os.getenv("GEMINI_MODEL")
        or ""
    )

    # Browser Automation Settings
    browser_headless: bool = os.getenv("BROWSER_HEADLESS", "false").lower() in ("true", "1", "yes")
    browser_slow_mo: int = int(os.getenv("BROWSER_SLOW_MO", "50"))
    viewport_width: int = int(os.getenv("BROWSER_VIEWPORT_WIDTH", "1280"))
    viewport_height: int = int(os.getenv("BROWSER_VIEWPORT_HEIGHT", "800"))

    # Decision Engine Parameters
    confidence_threshold: float = float(os.getenv("CONFIDENCE_THRESHOLD", "0.7"))
    sliding_window_size: int = int(os.getenv("SLIDING_WINDOW_SIZE", "5"))
    max_steps: int = int(os.getenv("MAX_STEPS", "30"))
    auto_dismiss_cookie_banners: bool = os.getenv("AUTO_DISMISS_COOKIE_BANNERS", "true").lower() in ("true", "1", "yes")

    # Jev-Only Mode (Bypasses LLM entirely; relies 100% on Jev micro-decisions & heuristics)
    jev_only: bool = os.getenv("JEV_ONLY", "false").lower() in ("true", "1", "yes")

    # Simulation / Mock Mode
    mock_mode: bool = os.getenv("MOCK_MODE", "false").lower() in ("true", "1", "yes")

    @property
    def has_antigravity_session(self) -> bool:
        """Returns True if user has an active Antigravity IDE connection or API key."""
        return bool(self.antigravity_bin or self.antigravity_api_key)

    def validate_keys_for_live(self) -> list[str]:
        """Check if required credentials are configured for live execution."""
        missing = []
        if not self.mock_mode:
            if not self.typesafe_api_key:
                missing.append("TYPESAFE_API_KEY (or JEV_API_KEY)")
            if not self.jev_only and not self.has_antigravity_session:
                missing.append("Antigravity IDE connection (agy CLI) or GEMINI_API_KEY")
        return missing


# Global default configuration instance
default_config = Config()
