"""
CLI entry point for the Jev + Antigravity IDE Hybrid Fast-Browser Agent.
"""

from __future__ import annotations

import argparse
import asyncio
import logging
import sys

from jev_browser_agent.agent import HybridFastBrowserAgent
from jev_browser_agent.config import Config


def setup_logging(verbose: bool = False) -> None:
    """Configure console logging format."""
    level = logging.DEBUG if verbose else logging.INFO
    log_format = "[%(levelname)s] %(asctime)s - %(name)s: %(message)s"
    logging.basicConfig(level=level, format=log_format, datefmt="%H:%M:%S")


def parse_args() -> argparse.Namespace:
    """Parse CLI arguments."""
    parser = argparse.ArgumentParser(
        description="Run the Jev + Antigravity IDE Hybrid Fast-Browser Agent"
    )
    parser.add_argument(
        "--url",
        type=str,
        default="https://news.ycombinator.com",
        help="Target webpage URL to navigate to (default: https://news.ycombinator.com)",
    )
    parser.add_argument(
        "--goal",
        type=str,
        default="Find the search box, search for 'playwright', and view the first result",
        help="End-to-end user goal definition",
    )
    parser.add_argument(
        "--sub-goals",
        type=str,
        nargs="*",
        default=None,
        help="Optional ordered list of sub-goals to chain",
    )
    parser.add_argument(
        "--headless",
        action="store_true",
        default=False,
        help="Run the browser in headless mode (default: false / visible)",
    )
    parser.add_argument(
        "--slow-mo",
        type=int,
        default=50,
        help="Slow-motion delay in milliseconds between browser actions (default: 50ms)",
    )
    parser.add_argument(
        "--jev-only",
        action="store_true",
        default=False,
        help="Run 100% on Jev alone, bypassing the Antigravity reasoning engine entirely",
    )
    parser.add_argument(
        "--mock",
        action="store_true",
        default=False,
        help="Enable deterministic simulation mode (does not call live Jev or Antigravity APIs)",
    )
    parser.add_argument(
        "--threshold",
        type=float,
        default=0.7,
        help="Confidence threshold for deterministic Jev execution (default: 0.7)",
    )
    parser.add_argument(
        "--max-steps",
        type=int,
        default=25,
        help="Maximum navigation steps before terminating (default: 25)",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Enable verbose debug logging",
    )
    return parser.parse_args()


async def async_main() -> int:
    args = parse_args()
    setup_logging(args.verbose)

    config = Config(
        browser_headless=args.headless,
        browser_slow_mo=args.slow_mo,
        mock_mode=args.mock,
        jev_only=args.jev_only,
        confidence_threshold=args.threshold,
        max_steps=args.max_steps,
    )

    # Validate API keys if not running in mock mode
    if not config.mock_mode:
        missing_keys = config.validate_keys_for_live()
        if missing_keys:
            print("\n" + "!" * 70)
            print("WARNING: Missing required credentials in environment or .env file:")
            for k in missing_keys:
                print(f"  - {k}")
            print("\nYou can either:")
            print("  1. Add TYPESAFE_API_KEY to your .env file")
            print("  2. Pass --mock to run in deterministic offline simulation mode")
            print("!" * 70 + "\n")
            print("Falling back to --mock simulation mode for this execution...")
            config.mock_mode = True

    agent = HybridFastBrowserAgent(config=config)

    engine_desc = (
        f"Antigravity IDE (Native Google Account Session: {config.antigravity_bin})"
        if config.antigravity_bin
        else f"Google GenAI SDK ({config.antigravity_model or 'gemini-2.5-flash'})"
    )

    print("\n" + "=" * 70)
    print("HYBRID FAST-BROWSER AGENT (ANTIGRAVITY IDE)")
    print(f"Target URL:       {args.url}")
    print(f"User Goal:        {args.goal}")
    print(f"Reasoning Engine: {engine_desc}")
    print(f"Mode:             {'MOCK SIMULATION' if config.mock_mode else 'LIVE API (Jev + Antigravity IDE)'}")
    print(f"Confidence:       >= {config.confidence_threshold}")
    print(f"Memory Window:    {config.sliding_window_size} actions")
    print("=" * 70 + "\n")

    result = await agent.run(
        start_url=args.url,
        goal=args.goal,
        sub_goals=args.sub_goals,
    )

    print("\n" + "=" * 70)
    print("RUN COMPLETE")
    print(f"Success:      {result.success}")
    print(f"Total Steps:  {result.total_steps}")
    print(f"Final URL:    {result.final_url}")
    print(f"Summary:      {result.summary}")
    print("\nRecent Action Trail (Sliding Window):")
    for rec in result.history:
        print(f"  • {rec.to_history_line()}")

    if result.verification:
        print(f"\nAntigravity Verification Result:")
        print(f"  Verified:   {result.verification.verified}")
        print(f"  Confidence: {result.verification.confidence:.2f}")
        print(f"  Reasoning:  {result.verification.reasoning}")
    print("=" * 70 + "\n")

    return 0 if result.success else 1


def main() -> None:
    exit_code = asyncio.run(async_main())
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
