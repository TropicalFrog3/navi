#!/usr/bin/env python3
"""Jev-routed subagent dispatcher for the Navi orchestrator (Option A + Automatic Quota Failover).

On every non-trivial task, the orchestrator calls this script first. The local
Jev decision layer (H2O-Lightning via llama.cpp, `jev-decision` skill) decides:
  1. Whether to delegate the task to a `navi-worker` subagent (`delegate_subagent`)
     or handle it directly in the main orchestrator (`handle_directly`).
  2. Which AGY model family from `agy models` to use.
  3. Which reasoning effort (`low`, `medium`, `high`) to use.

Automatic Quota Failover:
  - If a model hits AGY token / quota exhaustion (HTTP 429 / RESOURCE_EXHAUSTED / Individual quota reached),
    the dispatcher records a cooldown (persisted in ~/.cache/navi-agents/quota_cooldowns.json)
    with the exact reset duration parsed from the error message.
  - The dispatcher automatically fails over immediately to the next candidate model
    ranked by Jev probability without failing the task.
  - Subsequent routes automatically deprioritize cooled-down models until their reset time.

Usage:
  route_subagent.py models
  route_subagent.py quota [--clear] [--clear-model ID]
  route_subagent.py route --task "..." [--context "..."] [--ignore-cooldown]
  route_subagent.py spawn --task "..." [--context "..."] [--agent navi-worker]
                          [--model-override ID] [--force] [--ignore-cooldown]
                          [--dry-run] [--timeout 0s]
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
CATALOG_PATH = HERE / "model_catalog.json"
JEV_SCRIPTS = Path("/home/nicolas/.gemini/config/skills/jev-decision/scripts")
CACHE_DIR = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "navi-agents"
CACHE_PATH = CACHE_DIR / "agy_models.json"
COOLDOWN_PATH = CACHE_DIR / "quota_cooldowns.json"
CACHE_TTL_S = 3600
EFFORTS = ["low", "medium", "high"]
EFFORT_RE = re.compile(r"^(?P<family>.+)-(?P<effort>low|medium|high|xhigh|max)$")
GENERIC_DESC = "General-purpose AGY model (no catalog description available)."

QUOTA_ERROR_PATTERNS = [
    re.compile(r"RESOURCE_EXHAUSTED", re.IGNORECASE),
    re.compile(r"Individual quota reached", re.IGNORECASE),
    re.compile(r"quota reached", re.IGNORECASE),
    re.compile(r"rate_limit|rate limit", re.IGNORECASE),
    re.compile(r"exceeded your.*quota", re.IGNORECASE),
    re.compile(r"error_code\":\s*429"),
]


def load_catalog() -> dict:
    with open(CATALOG_PATH, encoding="utf-8") as fh:
        return json.load(fh)


def is_quota_exhausted(stderr_text: str, returncode: int) -> bool:
    if returncode == 0:
        return False
    return any(p.search(stderr_text) for p in QUOTA_ERROR_PATTERNS)


def parse_reset_duration(text: str) -> float:
    """Parse 'Resets in 117h34m40s' into seconds. Default 3600 if not found."""
    m = re.search(r"Resets in (?:(\d+)d)?(?:(\d+)h)?(?:(\d+)m)?(?:(\d+)s)?", text)
    if m:
        days = int(m.group(1) or 0)
        hours = int(m.group(2) or 0)
        mins = int(m.group(3) or 0)
        secs = int(m.group(4) or 0)
        total = days * 86400 + hours * 3600 + mins * 60 + secs
        if total > 0:
            return float(total)
    return 3600.0


def parse_reset_str(text: str) -> str:
    m = re.search(r"Resets in [^\.\n]+", text)
    return m.group(0) if m else "quota cooldown active"


def load_cooldowns() -> dict:
    if not COOLDOWN_PATH.is_file():
        return {}
    try:
        data = json.loads(COOLDOWN_PATH.read_text(encoding="utf-8"))
        now = time.time()
        active = {k: v for k, v in data.items() if v.get("cooldown_until", 0) > now}
        if len(active) != len(data):
            COOLDOWN_PATH.write_text(json.dumps(active, indent=2))
        return active
    except Exception:
        return {}


def record_cooldown(model_id: str, error_text: str):
    duration = parse_reset_duration(error_text)
    until = time.time() + duration
    cooldowns = load_cooldowns()
    cooldowns[model_id] = {
        "cooldown_until": until,
        "cooldown_until_str": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(until)),
        "resets_in": parse_reset_str(error_text),
        "recorded_at": time.time(),
    }
    COOLDOWN_PATH.parent.mkdir(parents=True, exist_ok=True)
    COOLDOWN_PATH.write_text(json.dumps(cooldowns, indent=2))


def clear_cooldowns(model_id: str = None) -> int:
    if not COOLDOWN_PATH.is_file():
        return 0
    if not model_id:
        active = load_cooldowns()
        count = len(active)
        COOLDOWN_PATH.unlink(missing_ok=True)
        return count
    cooldowns = load_cooldowns()
    removed = 1 if model_id in cooldowns else 0
    cooldowns.pop(model_id, None)
    COOLDOWN_PATH.write_text(json.dumps(cooldowns, indent=2))
    return removed


def fetch_agy_models(refresh: bool = False) -> list:
    """Return [(model_id, display_name)] from `agy models`, cached for an hour."""
    if not refresh and CACHE_PATH.is_file() and time.time() - CACHE_PATH.stat().st_mtime < CACHE_TTL_S:
        try:
            return [tuple(x) for x in json.loads(CACHE_PATH.read_text())]
        except (OSError, ValueError):
            pass
    out = subprocess.run(["agy", "models"], capture_output=True, text=True, timeout=60)
    models = []
    for line in out.stdout.splitlines():
        if "\t" not in line:
            continue
        mid, name = line.split("\t", 1)
        mid = mid.strip()
        if mid and " " not in mid:
            models.append((mid, name.strip()))
    if not models:
        raise RuntimeError(f"`agy models` returned no models (rc={out.returncode}): {out.stderr.strip()[:300]}")
    CACHE_PATH.parent.mkdir(parents=True, exist_ok=True)
    CACHE_PATH.write_text(json.dumps(models))
    return models


def group_families(models: list) -> dict:
    """{family: {effort: model_id}}; IDs without an effort suffix map to 'medium'."""
    fams: dict = {}
    for mid, _name in models:
        m = EFFORT_RE.match(mid)
        fam, eff = (m.group("family"), m.group("effort")) if m else (mid, "medium")
        fams.setdefault(fam, {})[eff] = mid
    return fams


def snap_effort(available: dict, wanted: str) -> str:
    """Pick the closest available effort for a family."""
    if wanted in available:
        return wanted
    order = EFFORTS + ["xhigh", "max"]
    wi = order.index(wanted) if wanted in order else 1
    return min(available, key=lambda e: abs((order.index(e) if e in order else 1) - wi))


def jev_route(task: str, context: str, families: dict, catalog: dict) -> dict:
    sys.path.insert(0, str(JEV_SCRIPTS))
    from decide import decide  # noqa: E402  (local jev-decision skill)

    del_desc = catalog.get("delegation", {
        "delegate_subagent": "Self-contained engineering subtask suitable for a subagent.",
        "handle_directly": "Keep in the main orchestrator without spawning a subagent.",
    })
    fam_desc = catalog.get("families", {})
    criteria = {}
    for fam, effs in families.items():
        desc = fam_desc.get(fam, GENERIC_DESC)
        criteria[fam] = f"{desc} Efforts available: {', '.join(sorted(effs))}."
    eff_desc = catalog.get("efforts", {})
    state = {
        "project": "Navi: 32-bit C++ Steam client hooking library (steamclient.so/steamui.so detours), "
                   "CEF JS injection, bash install/release scripts, pytest integration tests, Steam Deck ops.",
        "subtask": task,
        "extra_context": context or "none",
        "goal": "Decide whether to delegate this subtask to a subagent or handle it directly, "
                "and choose the cheapest model and effort that will complete it correctly.",
    }
    questions = {
        "delegation": {
            "type": "choice",
            "instructions": "Should the orchestrator delegate this task to a subagent or handle it directly?",
            "criteria": del_desc,
        },
        "model": {
            "type": "choice",
            "instructions": "Which model family should run this subtask?",
            "criteria": criteria,
        },
        "effort": {
            "type": "score",
            "instructions": "How much reasoning effort does this subtask need?",
            "criteria": [f"{e}: {eff_desc.get(e, e)}" for e in EFFORTS],
        },
    }
    return decide(state, questions)


def build_candidates_list(
    families: dict,
    model_probs: dict,
    effort_wanted: str,
    fallback_model: str,
    override: str = None,
) -> list:
    """Build a deterministic preference list of AGY model IDs based on Jev probabilities."""
    candidates = []
    if override:
        candidates.append(override)

    # Sort families by Jev's probability descending
    ranked_families = sorted(
        families.keys(),
        key=lambda f: model_probs.get(f, 0.0),
        reverse=True
    )

    for fam in ranked_families:
        eff = snap_effort(families[fam], effort_wanted)
        mid = families[fam][eff]
        if mid not in candidates:
            candidates.append(mid)

    if fallback_model and fallback_model not in candidates:
        candidates.append(fallback_model)

    return candidates


def route(
    task: str,
    context: str = "",
    override: str = None,
    refresh: bool = False,
    ignore_cooldown: bool = False,
) -> dict:
    catalog = load_catalog()
    models = fetch_agy_models(refresh)
    ids = {m for m, _ in models}
    families = group_families(models)
    cooldowns = load_cooldowns()

    fallback = catalog.get("fallback_model")
    if fallback not in ids:
        fallback = sorted(ids)[0]
    thr = catalog.get("min_confidence", {})

    if override:
        if override not in ids:
            raise SystemExit(f"--model-override {override!r} is not in `agy models`: {sorted(ids)}")
        candidates = [override]
        for m in sorted(ids):
            if m not in candidates:
                candidates.append(m)
        return {
            "delegate": True,
            "execution_mode": "delegate_subagent",
            "model_id": override,
            "candidates": candidates,
            "lane": "override",
            "jev_called": False,
            "active_cooldowns": {c: cooldowns[c] for c in candidates if c in cooldowns},
        }

    try:
        res = jev_route(task, context, families, catalog)
    except Exception as exc:  # Jev offline / error -> transparent fallback
        candidates = [fallback] + [m for m in sorted(ids) if m != fallback]
        return {
            "delegate": True,
            "execution_mode": "delegate_subagent",
            "model_id": fallback,
            "candidates": candidates,
            "lane": "fallback",
            "jev_called": False,
            "reason": f"Jev unavailable: {exc}",
            "active_cooldowns": {c: cooldowns[c] for c in candidates if c in cooldowns},
        }

    d_ans = res["answers"]["delegation"]
    m_ans = res["answers"]["model"]
    e_ans = res["answers"]["effort"]

    exec_mode = d_ans["choice"]
    if d_ans["confidence"] < thr.get("delegate", 0.10):
        exec_mode = "handle_directly"
    delegate = exec_mode == "delegate_subagent"

    effort = EFFORTS[min(range(3), key=lambda i: abs(i - e_ans["score"]))]
    low_conf = m_ans["confidence"] < thr.get("model", 0.15) or e_ans["confidence"] < thr.get("effort", 0.10)

    # Build full ranked candidates list from Jev probabilities
    raw_candidates = build_candidates_list(
        families,
        m_ans["probabilities"],
        effort,
        fallback,
    )

    # Filter/reorder candidates by quota cooldowns
    active_cooldowns = {c: cooldowns[c] for c in raw_candidates if c in cooldowns}
    if not ignore_cooldown:
        available = [c for c in raw_candidates if c not in cooldowns]
        cooled = [c for c in raw_candidates if c in cooldowns]
        candidates = available + cooled if available else raw_candidates
    else:
        candidates = raw_candidates

    selected_model = candidates[0] if candidates else fallback
    family = m_ans["choice"]

    out = {
        "delegate": delegate,
        "execution_mode": exec_mode,
        "model_id": selected_model,
        "jev_primary_choice": raw_candidates[0] if raw_candidates else selected_model,
        "candidates": candidates,
        "active_cooldowns": active_cooldowns,
        "family": family,
        "effort": effort,
        "lane": "act",
        "jev_called": True,
        "jev": {
            "delegation_probs": d_ans["probabilities"],
            "delegation_confidence": d_ans["confidence"],
            "model_probs": m_ans["probabilities"],
            "model_confidence": m_ans["confidence"],
            "effort_score": e_ans["score"],
            "effort_confidence": e_ans["confidence"],
            "latency_ms": res.get("latency_ms"),
            "warnings": res.get("warnings", []),
        },
    }
    if low_conf:
        out.update({
            "model_id": fallback,
            "lane": "fallback",
            "jev_choice": selected_model,
            "reason": "Jev confidence below catalog min_confidence",
        })
    return out


def execute_subagent_with_failover(
    candidates: list,
    prompt: str,
    agent: str,
    mode: str,
    timeout: str,
    dry_run: bool,
) -> int:
    """Run agy subagent, automatically failing over to next candidate on quota exhaustion."""
    for idx, model_id in enumerate(candidates):
        cmd = ["agy", "--model", model_id, "--agent", agent, "--print-timeout", timeout]
        if mode:
            cmd += ["--mode", mode]
        cmd.append(f"--print={prompt}")

        if dry_run:
            print(f"[DRY-RUN] Candidate {idx + 1}/{len(candidates)}: {' '.join(cmd[:-1])} --print=<prompt>", file=sys.stderr)
            return 0

        print(f"[DISPATCHER] Launching subagent with candidate {idx + 1}/{len(candidates)}: {model_id}", file=sys.stderr, flush=True)

        proc = subprocess.Popen(
            cmd,
            cwd=os.getcwd(),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        stdout, stderr = proc.communicate()

        if proc.returncode != 0 and is_quota_exhausted(stderr, proc.returncode):
            reset_msg = parse_reset_str(stderr)
            record_cooldown(model_id, stderr)
            print(f"[QUOTA FAILOVER] Model '{model_id}' failed: RESOURCE_EXHAUSTED (quota reached, {reset_msg}).", file=sys.stderr, flush=True)
            if idx + 1 < len(candidates):
                next_model = candidates[idx + 1]
                print(f"[QUOTA FAILOVER] Automatically failing over to next candidate: '{next_model}'...", file=sys.stderr, flush=True)
                continue
            else:
                print(f"[QUOTA FAILOVER] Error: all candidate models exhausted their quota limits.", file=sys.stderr, flush=True)
                sys.stderr.write(stderr)
                return proc.returncode

        # If success or normal non-quota exit code:
        if stdout:
            sys.stdout.write(stdout)
            sys.stdout.flush()
        if stderr:
            sys.stderr.write(stderr)
            sys.stderr.flush()
        return proc.returncode

    return 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--refresh-models", action="store_true", help="Bypass the `agy models` cache")
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("models", help="List AGY models grouped by family/effort")

    p_quota = sub.add_parser("quota", help="View or clear active quota cooldowns")
    p_quota.add_argument("--clear", action="store_true", help="Clear all model quota cooldowns")
    p_quota.add_argument("--clear-model", help="Clear quota cooldown for a specific model ID")

    for name in ("route", "spawn"):
        p = sub.add_parser(name)
        p.add_argument("--task", "-t", required=True, help="Self-contained subtask description")
        p.add_argument("--context", "-c", default="", help="Extra routing context (constraints, files involved)")
        p.add_argument("--model-override", help="Force this AGY model ID as primary candidate")
        p.add_argument("--ignore-cooldown", action="store_true", help="Do not deprioritize models currently in quota cooldown")
        if name == "spawn":
            p.add_argument("--agent", default="navi-worker", help="AGY agent to run (default: navi-worker)")
            p.add_argument("--mode", default=None, choices=["auto", "accept-edits", "plan"],
                           help="agy --mode (omitted by default; agent commandExecutionPolicy applies)")
            p.add_argument("--timeout", default="0s", help="agy --print-timeout (e.g. 30m); 0s = no limit")
            p.add_argument("--force", action="store_true",
                           help="Spawn the subagent even if Jev recommends handle_directly")
            p.add_argument("--dry-run", action="store_true", help="Print the routing/agy command without running it")
    args = ap.parse_args()

    if args.cmd == "models":
        print(json.dumps(group_families(fetch_agy_models(args.refresh_models)), indent=2))
        return 0

    if args.cmd == "quota":
        if args.clear or args.clear_model:
            cleared = clear_cooldowns(args.clear_model)
            print(f"Cleared {cleared} quota cooldown(s).")
            return 0
        cooldowns = load_cooldowns()
        if not cooldowns:
            print("No active quota cooldowns.")
        else:
            print(json.dumps(cooldowns, indent=2))
        return 0

    decision = route(
        args.task,
        args.context,
        args.model_override,
        args.refresh_models,
        ignore_cooldown=args.ignore_cooldown,
    )
    if args.cmd == "route":
        print(json.dumps(decision, indent=2))
        return 0

    if not decision.get("delegate", True) and not args.force:
        print(json.dumps({
            "routing": decision,
            "spawned": False,
            "action": "handle_directly_in_orchestrator",
        }), file=sys.stderr, flush=True)
        print("JEV_DECISION: handle_directly (orchestrator executes this task inline without spawning a subagent)")
        return 0

    prompt = args.task if not args.context else f"{args.task}\n\nContext:\n{args.context}"
    candidates = decision.get("candidates", [decision["model_id"]])

    print(json.dumps({
        "routing": decision,
        "spawned": True,
        "primary_model": candidates[0],
        "candidates_order": candidates,
    }), file=sys.stderr, flush=True)

    return execute_subagent_with_failover(
        candidates=candidates,
        prompt=prompt,
        agent=args.agent,
        mode=args.mode,
        timeout=args.timeout,
        dry_run=args.dry_run,
    )


if __name__ == "__main__":
    sys.exit(main())
