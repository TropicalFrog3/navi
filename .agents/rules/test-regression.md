# Test-Driven Bug Fix & Regression Prevention Rule

Every bug fix in Navi MUST include an accompanying automated test that reliably reproduces the defect. Test suites are executed **only when the user explicitly asks**, and always via `./install.sh` subcommands (`./install.sh test`, `./install.sh test-gate07`, `./install.sh test-ui`, `./install.sh test-flow`, `./install.sh coverage`) rather than raw `pytest`, `make`, or `./bin/unit_tests`. By default, verify changes compile via `./install.sh build`.

---

## 1. Mandatory Bug Fix Lifecycle

```
[1. Identify Defect]
         │
         ▼
[2. Write Failing Test]  ──>  tests/unit/ (GoogleTest) OR tests/integration/ (pytest)
         │
         ▼
[3. Verify Test Fails]   ──>  (Only when user asks to run tests) Confirm RED state via ./install.sh test / test-gate07 / test-ui
         │
         ▼
[4. Implement Fix]       ──>  Apply minimal targeted fix in src/ and verify compilation via ./install.sh build
         │
         ▼
[5. Verify Test Passes]  ──>  (Only when user asks to run tests) Confirm GREEN state via ./install.sh test / test-gate07 / test-ui
         │
         ▼
[6. Full Suite Run]      ──>  (Only when user asks to run tests) Execute ./install.sh test (and gate/UI suites) with ZERO regressions
```

---

## 2. Step-by-Step Requirements

### Step 1: Author an Automated Regression Test
Alongside fixing production code in `src/`, author a minimal automated test that reproduces the bug:
- **C++ Unit Tests (`tests/unit/`)**:
  - Implement a new test or test case using GoogleTest.
  - Target the specific class, method, or edge case in `src/{core,model,presenter,view}`.
  - When the user asks to run tests, execute via `./install.sh test`.
- **Integration / Python Tests (`tests/integration/`)**:
  - Implement an automated pytest test case for CLI commands, installer behavior, migrations, API endpoints, or IPC.
  - When the user asks to run tests, execute via `./install.sh test-gate07` or `./install.sh test-ui` (or `./install.sh test-flow` for full end-to-end flow).

### Step 2: Verify Test Failure (Red State — When Requested)
- If the user explicitly asks to execute tests before the fix, run `./install.sh test` (or `./install.sh test-gate07` / `./install.sh test-ui`) against the unfixed codebase.
- **Invariant**: The test MUST fail, and the failure mode must unambiguously reflect the reported bug.
- If the test passes before the fix is applied, the test does not reproduce the bug and must be revised.

### Step 3: Implement the Minimal Fix & Build Verification
- Apply the targeted fix in the appropriate MVP layer (`src/core/`, `src/model/`, `src/presenter/`, or `src/view/`).
- Verify compilation by running `./install.sh build` (default verification step).
- Only if the user explicitly asks to run tests, execute the relevant `./install.sh` test command (`./install.sh test`, `./install.sh test-gate07`, `./install.sh test-ui`, `./install.sh test-flow`) to confirm the GREEN state.

### Step 4: Run the Complete Test Suite (Only When User Asks)
- Execute test suites **only when the user explicitly asks**, always using `./install.sh` from the repo root:
  ```bash
  ./install.sh test
  ```
  (and `./install.sh test-gate07`, `./install.sh test-ui`, `./install.sh test-flow`, or `./install.sh coverage` as requested).
- When run on request, ensure:
  1. All GoogleTest unit tests pass (`./install.sh test`).
  2. All pytest integration / UI tests pass (`./install.sh test-gate07` / `./install.sh test-ui` / `./install.sh test-flow`).
  3. Code coverage reports generate cleanly via `./install.sh coverage` (when requested).
  4. Zero existing tests fail (zero regressions across all components).

---

## 3. Enforcement Invariants

1. **No Reproduction, No Fix**: Never accept or commit a bug fix without an accompanying reproduction test.
2. **Explicit User Request for Test Execution**: Do not execute test suites automatically after edits; default verification is `./install.sh build` (and `./install.sh bundle-scripts <target>` for CEF UI). Always use `./install.sh` subcommands when the user asks to run tests.
3. **Deterministic & Offline**: Regression tests must run offline without external network dependencies unless explicitly tagged under live flow flags (`--live`).
4. **Continuous Protection**: Once added, regression tests become permanent regression guards in the CI and local verification pipeline.

---

## 4. Exemption: Pure UI Markup & Aesthetic Template Changes
- Aesthetic UI modifications (HTML restructuring, section grouping, CSS spacing, typography) do **NOT** require authoring new C++ or pytest test cases.
- **Verification Contract for UI Markup**:
  1. Retain all existing DOM IDs (`id="sls-navi-btn-*"`) and class hooks so existing event listeners remain attached.
  2. Bundle modified scripts via `./install.sh bundle-scripts <target>` (run `./install.sh test-gate07` or `./install.sh test-ui` only when the user explicitly asks to execute tests).
  3. Do **NOT** grep the entire `tests/` directory unless an existing test actually fails.
