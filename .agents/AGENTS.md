# Workspace Agent Rules

- **Task Routing**:
  - On every non-trivial task, the main agent decides whether to handle directly or delegate to a subagent based on task complexity and scope.
  - Subagents do not spawn further subagents and do not run device/release operations (`scripts/dev/deck-ctl.sh`, `steam-os/steamos-ctl.sh`, `./install.sh release`) unless explicitly asked.
  - The main agent reviews subagent diffs and re-runs verification before reporting success.
- **Token Efficiency & Targeted Exploration**:
  - Minimize token usage with targeted `grep_search` and line-bounded `view_file` calls over repository-wide reads.
  - Avoid reading vendored or generated directories (`include/`, `lib/`, `third_party/`, `venv/`, `obj/`, `bin/`) unless explicitly required.
  - Run path-scoped status and diff checks (`git status -s -- <dir>` and `git diff -- <dir>`) rather than repo-wide `git status` when verifying component edits on shared or dirty working trees.
- **Verification Standards**:
  - Verify code changes via `./install.sh build`, `./install.sh test`, and `git diff` self-review.
  - Respect all domain invariants in `.agents/rules/*` and `.agents/skills/*`.
- **Concise Outputs**:
  - Provide concise summaries: files changed, diff overview, test results, and brief explanations.
