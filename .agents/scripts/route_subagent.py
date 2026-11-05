#!/usr/bin/env python3
"""Jev decision router for the Navi workspace (decision layer only).

The local Jev decision layer (H2O-Lightning via llama.cpp, `jev-decision`
skill at /home/nicolas/.gemini/config/skills/jev-decision/scripts) decides,
for a given self-contained subtask:

  1. `execution_mode` — `delegate_subagent` (the caller runs the subtask in
     a subagent via its own harness) vs `handle_directly` (the caller runs
     it inline).
  2. `effort` — reasoning rigor (`low`, `medium`, `high`) the caller should
     apply to the subtask.

This script only routes decisions. It never spawns processes beyond the Jev
`decide()` client, never calls an external agent CLI, and never reads or
writes any files besides printing JSON to stdout.

Usage:
  route_subagent.py route --task "..." [--context "..."]

Exit codes:
  0  always (a decision was printed; inspect the JSON's `jev_called` and
     `lane` fields to tell a real Jev decision from the offline fallback).
"""
import argparse
import json
import sys
from pathlib import Path

JEV_SCRIPTS = Path("/home/nicolas/.gemini/config/skills/jev-decision/scripts")
EFFORTS = ["low", "medium", "high"]

STATE_PROJECT = (
    "Navi: 32-bit C++ Steam client hooking library (steamclient.so/steamui.so detours), "
    "CEF JS injection, bash install/release scripts, pytest integration tests, Steam Deck ops."
)

DELEGATION_CRITERIA = {
    "delegate_subagent": (
        "Codebase search, reading/editing files, implementing features, fixing bugs, "
        "writing or running tests, debugging crashes/hooks, binary analysis, or code review."
    ),
    "handle_directly": (
        "Direct chat reply to a simple user question that requires no code or file work, "
        "OR Steam Deck hardware deployment (deck-ctl.sh, steamos-ctl.sh) and release "
        "publishing (./install.sh release, git push)."
    ),
}

EFFORT_CRITERIA = {
    "low": "Trivial or well-specified subtask; little reasoning needed (lookups, renames, formatting, listing files).",
    "medium": "Normal engineering subtask with some ambiguity (implement a feature, fix a known bug, write tests).",
    "high": "Hard, high-stakes or open-ended subtask (crash/race debugging, binary analysis, design, security-sensitive changes).",
}


def jev_decide(task: str, context: str) -> dict:
    sys.path.insert(0, str(JEV_SCRIPTS))
    from decide import decide  # noqa: E402  (local jev-decision skill)

    state = {
        "project": STATE_PROJECT,
        "subtask": task,
        "extra_context": context or "none",
        "goal": "Decide whether the caller should delegate this subtask to a subagent or handle it directly, "
                "and how much reasoning effort it deserves.",
    }
    questions = {
        "execution_mode": {
            "type": "choice",
            "instructions": "Should the caller delegate this task to a subagent or handle it directly?",
            "criteria": DELEGATION_CRITERIA,
        },
        "effort": {
            "type": "score",
            "instructions": "How much reasoning effort does this subtask need?",
            "criteria": list(EFFORT_CRITERIA.values()),
        },
    }
    return decide(state, questions)


def route(task: str, context: str = "") -> dict:
    try:
        res = jev_decide(task, context)
    except Exception as exc:  # Jev offline / error -> conservative fallback
        return {
            "execution_mode": "handle_directly",
            "effort": "medium",
            "lane": "fallback",
            "jev_called": False,
            "reason": f"Jev unavailable: {exc}",
        }

    e_ans = res["answers"]["execution_mode"]
    f_ans = res["answers"]["effort"]
    effort = EFFORTS[min(range(len(EFFORTS)), key=lambda i: abs(i - f_ans["score"]))]
    return {
        "execution_mode": e_ans["choice"],
        "effort": effort,
        "lane": "act",
        "jev_called": True,
        "jev": {
            "execution_mode_probs": e_ans["probabilities"],
            "execution_mode_confidence": e_ans["confidence"],
            "effort_score": f_ans["score"],
            "effort_confidence": f_ans["confidence"],
            "latency_ms": res.get("latency_ms"),
            "warnings": res.get("warnings", []),
        },
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("route", help="Print Jev's routing decision (execution_mode + effort) as JSON")
    p.add_argument("--task", "-t", required=True, help="Self-contained subtask description")
    p.add_argument("--context", "-c", default="", help="Extra routing context (constraints, files involved)")
    args = ap.parse_args()

    print(json.dumps(route(args.task, args.context), indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
