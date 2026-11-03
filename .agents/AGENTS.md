# Workspace Agent Rules

- **Mandatory Jev Routing & Subagent Delegation (Option A)**:
  - `orchestrator` (`.agents/agents/orchestrator/agent.md`) is the main agent in this workspace (`mainAgent: true`, `commandExecutionPolicy: auto`, full toolset).
  - **Never decide on your own whether to use a subagent or which model to use.** On every non-trivial task (codebase search, implementation, refactoring, bug fixing, test writing/execution, debugging, or review), the orchestrator **MUST FIRST** invoke `.agents/scripts/route_subagent.py`:
    ```bash
    python3 .agents/scripts/route_subagent.py spawn --task "<subtask>" --context "<files, constraints>"
    ```
  - **Jev makes both decisions**:
    1. `delegation` (`delegate_subagent` vs `handle_directly`): whether to spawn `navi-worker` (`.agents/agents/navi-worker/agent.md`) or handle the task inline in the orchestrator.
    2. `model` + `effort` (`low`/`medium`/`high`): chosen from every model listed by `agy models`, using `.agents/scripts/model_catalog.json`.
  - When `spawn` runs:
    - If Jev chooses `delegate_subagent`, `route_subagent.py` immediately spawns `navi-worker` with Jev's chosen `--model <model_id>`.
    - If Jev chooses `handle_directly`, `route_subagent.py` prints `JEV_DECISION: handle_directly` and exits `0` without spawning a subagent; only then does the orchestrator execute the task directly.
  - **Automatic Quota Failover**: If a selected model hits an AGY quota or rate limit (HTTP 429 / RESOURCE_EXHAUSTED), `route_subagent.py` automatically parses the reset duration, records a local cooldown (`~/.cache/navi-agents/quota_cooldowns.json`), and fails over immediately to the next ranked candidate model until the task succeeds. Inspect or reset cooldowns via `python3 .agents/scripts/route_subagent.py quota [--clear]`.
  - Subagents do not spawn further subagents and do not run device/release operations (`scripts/dev/deck-ctl.sh`, `steam-os/steamos-ctl.sh`, `./install.sh release`) unless explicitly asked.
  - The orchestrator reviews subagent diffs and re-runs verification before reporting success.
- **Model Selection**:
  - The orchestrator uses `model: inherit` (whatever model the user selected). Subagent models and delegation decisions are chosen per task by Jev; use `--model-override` or `--force` only when the user explicitly requests it.
- **Token Efficiency & Targeted Exploration**:
  - Minimize token usage with targeted `grep_search` and line-bounded `view_file` calls over repository-wide reads.
  - Avoid reading vendored or generated directories (`include/`, `lib/`, `third_party/`, `venv/`, `obj/`, `bin/`) unless explicitly required.
- **Verification Standards**:
  - Verify code changes via unified compilation (`./install.sh build`), unit/integration tests (`./install.sh test` or `make test`, `pytest -q tests/integration`, `make test-gate07`, `make check-cloudredirect`), and `git diff` self-review.
  - Respect all domain invariants in `.agents/rules/*` and `.agents/skills/*`.
- **Concise Outputs**:
  - Provide concise summaries: files changed, diff overview, test results, Jev routing decision (`execution_mode`, `model_id`, `effort`), and brief explanations.
