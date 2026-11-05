# Workspace Agent Rules

- **Mandatory Jev Routing (Decision Layer)**:
  - On every non-trivial task (codebase search, implementation, refactoring, bug fixing, test writing/execution, debugging, or review), the main agent **MUST FIRST** consult Jev via `.agents/scripts/route_subagent.py`:
    ```bash
    python3 .agents/scripts/route_subagent.py route --task "<subtask>" --context "<files, constraints>"
    ```
  - **Jev makes the decisions**:
    1. `execution_mode` (`delegate_subagent` vs `handle_directly`): whether the subtask runs in a subagent or inline in the main agent.
    2. `effort` (`low`/`medium`/`high`): the reasoning rigor to apply to the subtask.
  - The main agent executes Jev's decision itself: the router never spawns anything and never calls an external agent CLI — it only prints the decision JSON. If `handle_directly`, execute the task directly; if `delegate_subagent`, run it in a subagent using the harness's own subagent mechanism, honoring Jev's `effort`.
  - Subagents do not spawn further subagents and do not run device/release operations (`scripts/dev/deck-ctl.sh`, `steam-os/steamos-ctl.sh`, `./install.sh release`) unless explicitly asked.
  - The main agent reviews subagent diffs and re-runs verification before reporting success.
- **Token Efficiency & Targeted Exploration**:
  - Minimize token usage with targeted `grep_search` and line-bounded `view_file` calls over repository-wide reads.
  - Avoid reading vendored or generated directories (`include/`, `lib/`, `third_party/`, `venv/`, `obj/`, `bin/`) unless explicitly required.
- **Verification Standards**:
  - Verify code changes via unified compilation (`./install.sh build`), unit/integration tests (`./install.sh test` or `make test`, `pytest -q tests/integration`, `make test-gate07`, `make check-cloudredirect`), and `git diff` self-review.
  - Respect all domain invariants in `.agents/rules/*` and `.agents/skills/*`.
- **Concise Outputs**:
  - Provide concise summaries: files changed, diff overview, test results, Jev routing decision (`execution_mode`, `effort`), and brief explanations.
