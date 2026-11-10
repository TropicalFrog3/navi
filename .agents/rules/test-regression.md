# Test-Driven Bug Fix & Regression Prevention Rule

Every bug fix in Navi MUST follow a strict test-first regression prevention protocol. No bug fix shall be applied to production source code without an accompanying automated test that reliably reproduces the defect.

---

## 1. Mandatory Bug Fix Lifecycle

```
[1. Identify Defect]
         │
         ▼
[2. Write Failing Test]  ──>  tests/unit/ (GoogleTest) OR tests/integration/ (pytest)
         │
         ▼
[3. Verify Test Fails]   ──>  Confirm RED state before changing any production code
         │
         ▼
[4. Implement Fix]       ──>  Apply minimal targeted fix in src/
         │
         ▼
[5. Verify Test Passes]  ──>  Confirm GREEN state for the new reproduction test
         │
         ▼
[6. Full Suite Run]      ──>  Execute ./install.sh test with ZERO regressions
```

---

## 2. Step-by-Step Requirements

### Step 1: Write a Failing Automated Test
Before touching production code in `src/`, author a minimal automated test that reproduces the bug:
- **C++ Unit Tests (`tests/unit/`)**:
  - Implement a new test or test case using GoogleTest.
  - Target the specific class, method, or edge case in `src/{core,model,presenter,view}`.
  - Compile and run via `./bin/unit_tests --gtest_filter=<TestCaseName>.*`.
- **Integration / Python Tests (`tests/integration/`)**:
  - Implement an automated pytest test case for CLI commands, installer behavior, migrations, API endpoints, or IPC.
  - Run via `pytest tests/integration/<test_file>.py -k <test_name>`.

### Step 2: Verify Test Failure (Red State)
- Run the newly authored test against the unfixed codebase.
- **Invariant**: The test MUST fail, and the failure mode must unambiguously reflect the reported bug.
- If the test passes before the fix is applied, the test does not reproduce the bug and must be revised.

### Step 3: Implement the Minimal Fix (Green State)
- Apply the targeted fix in the appropriate MVP layer (`src/core/`, `src/model/`, `src/presenter/`, or `src/view/`).
- Re-run the reproducing test to confirm it now passes cleanly.

### Step 4: Run the Complete Test Suite
- Run the complete validation suite from the repo root:
  ```bash
  ./install.sh test
  ```
- Ensure:
  1. All GoogleTest unit tests pass (`bin/unit_tests`).
  2. All pytest integration tests pass (`tests/integration`).
  3. Code coverage reports generate cleanly via `gcovr` (`./install.sh coverage`).
  4. Zero existing tests fail (zero regressions across all components).

---

## 3. Enforcement Invariants

1. **No Reproduction, No Fix**: Never accept or commit a bug fix without an accompanying reproduction test.
2. **Deterministic & Offline**: Regression tests must run offline without external network dependencies unless explicitly tagged under live flow flags (`--live`).
3. **Continuous Protection**: Once added, regression tests become permanent regression guards in the CI and local verification pipeline.
