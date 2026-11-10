# Navi agent Workspace Cheat Sheet

Quick-reference prompt guide for navigating your 3-layer context-routed Navi repository (`SLSsteam_n_ACCELA`) using OpenCode.

---

## 1. Feature Lifecycle Pipeline (`plans/`)
Move work items cleanly through `01-backlog/` → `02-specs/` → `03-active/` → `04-done/`.

| Stage | Copy-Paste Prompt | What It Triggers |
|---|---|---|
| **1. Capture an Idea** | `"Go to plans (Idea intake): add [feature idea] as a new backlog item in 01-backlog/."` | Reads `plans/CONTEXT.md` + `01-backlog/dev-plan.md`, triggers skill `plans-pipeline`, skips `src/**`. |
| **2. Write a Spec** | `"Go to plans (Spec an item): write a v1 spec for [topic] in 02-specs/<topic>-spec-v1.md."` | Reads the backlog item + `docs/CONTEXT.md`, creates `<topic>-spec-v1.md`. *(Add `"with deep-reasoning"` for risky designs).* |
| **3. Start Building** | `"Go to plans (Start implementation): move 02-specs/<topic>-spec-v1.md to 03-active/ and route to [src / res / tools] CONTEXT.md."` | Moves spec to `03-active/` and switches directly to the target workspace's `CONTEXT.md` task row. |
| **4. Close Out & Archive** | `"Go to plans (Close-out): archive 03-active/<topic>-spec-v1.md to 04-done/ using the latest report in tests/reports/."` | Appends outcome + date, moves spec to `04-done/`, writes `YYYY-MM-DD-handoff-<topic>.md`. |
| **5. Roadmap Check** | `"Go to plans (Roadmap review): summarize current active items, specs, and backlog."` | Reads `01-backlog/`, `02-specs/`, and `03-active/` while skipping `04-done/`, `src/**`, and `res/**`. |

---

## 2. Native C++ Engine (`src/`)

| Task | Copy-Paste Prompt | What It Triggers |
|---|---|---|
| **Steam Client Update** | `"Go to src (New Steam client version): audit and update hashes/vftables for the new Steam build."` | Loads skill `steam-version-update`, `res/version`, `src/core/patterns.*`, `src/vftableinfo.*`, and runs audit scripts. |
| **New Hook / C++ Feature** | `"Go to src (New native hook/feature): implement [feature] in [core/model/presenter/view]."` | Loads `cpp_hooking_patterns.md` + `steam_cef_ui_patterns.md` and skill `steam-native-hooking`; skips `res/inject-scripts/`, `tools/`, `tests/`. |
| **Cloud Saves / CloudRedirect** | `"Go to src (Cloud-saves change): update [component] and run ./install.sh check-cloudredirect."` | Loads `docs/references/cloud-redirect-architecture.md`, `cloud_oauth*`, presenter cloud files, and `docs/api/rest.md`. |
| **Test-First Bug Fix** | `"Go to src (Bug fix): reproduce [bug] with a failing test first, then fix it."` | Enforces `.agents/rules/test-regression.md` (RED → GREEN) + `tests/unit/test_stubs*` before editing engine code. |

---

## 3. Runtime Resources & CEF UI (`res/`)

| Task | Copy-Paste Prompt | What It Triggers |
|---|---|---|
| **CEF UI / JS Injection** | `"Go to res (CEF inject-script change): update the [library/store/cloud/shell] script for [feature]."` | Reads `res/inject-scripts/README.md` first + `src/view/cdpinject.cpp:850-875`; triggers `vibe-implement-annotations` / `cef-cdp-injection` and `./install.sh test-ui`. |
| **Config Schema / Migration** | `"Go to res (Config template change): add/migrate key [KeyName] in config.yaml."` | Loads `res/config.yaml`, `config.diff.yaml`, `cache.diff.yaml`, `src/model/migrations/`, and `tests/unit/test_config.cpp`. |
| **Swap `cloud_redirect.so`** | `"Go to res (cloud_redirect.so replacement): update the binary and verify lockstep versions."` | Checks `.gitignore` `!res/cloud_redirect.so`, `cloud-redirect-architecture.md`, and runs `./install.sh check-cloudredirect`. |
| **Version / Release Bump** | `"Go to res (Version / release bump): bump version to [vX.Y.Z] and update SafeModeHashes."` | Updates `res/version` + `res/updates.yaml` and triggers skill `release-workflow`. |

---

## 4. Tooling, Verification & Docs (`tools/`, `tests/`, `docs/`)

* **Standalone Tooling (`tools/`):**
  ```text
  Go to tools: update [cef-annotator / schema-grabber / ticket-grabber / steamstub-unpack / cloud_pending] to [goal].
  ```
* **Verification & Test Gates (`tests/`):**
  ```text
  Go to tests: run [unit / test-gate07 / UI journey test_ujNN / Steam Deck live pass] and write the report to tests/reports/YYYY-MM-DD-<gate>-<summary>.md.
  ```
* **Documentation & API Specs (`docs/`):**
  ```text
  Go to docs: update [docs/api/ipc.md / docs/api/rest.md / docs/references/cloud-redirect-*] to reflect [change].
  ```

---

## 5. Maintenance & Subagent Routing

* **Sync Routing Tables After Adding/Moving Files:**
  ```text
  Run skill routing-docs-maintainer to audit recent file changes and sync AGENTS.md and all CONTEXT.md routing tables.
  ```
* **Subagent Delegation (`.agents/`):**
  ```text
  Follow .agents/AGENTS.md to decide whether to handle directly or delegate [task] to a worker subagent.
  ```

---

## 3 Golden Habits for Maximum Speed & Minimum Tokens
1. **Always name the room in the first 4 words:** Starting with `"Go to res..."` or `"Go to src..."` forces a direct `read` on that folder's `CONTEXT.md`.
2. **Start in `Plan` mode for design, switch to `Build` mode for edits:** Verify the agent loaded the right files for ~1,500 tokens before letting it write code.
3. **One task per session:** Open a fresh OpenCode session per task so your context starts at the clean ~11k token baseline (~9.8k cached).