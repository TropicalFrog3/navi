---
name: project-structure-cleaner
description: Audit, clean, and reorganize project directory structures dynamically across any software stack. Use when the user asks to clean up files, organize folders, fix messy project layouts, remove dead or duplicate files, standardize file naming conventions, or refactor directory structures. Always proposes a dry-run migration plan before moving or deleting files.
---

# Project Structure Cleaner

Audit, clean, and reorganize software project file structures dynamically across any tech stack. This skill identifies misplaced files, dead artifacts, naming inconsistencies, and cluttered root directories, then proposes a verifiable dry-run migration plan and updates all affected imports and configurations upon approval.

---

## When to Use

- The user asks to clean up, organize, or restructure files and folders in their workspace or repository.
- A project root is cluttered with loose scripts, temporary files, configs, or misplaced source/test files.
- The user wants to enforce consistent file/directory naming conventions (e.g. kebab-case, snake_case, PascalCase).
- The user wants to identify and remove dead files, build artifacts, or duplicate modules.
- The user invokes `/project-structure-cleaner` or asks to tidy up the repo architecture.

---

## Workflow

### Step 1: Detect Stack and Conventions

Inspect the repository root to identify the language, framework, and existing conventions before proposing any changes:

1. **Scan Manifests and Config Files:**
   - **Node.js / TypeScript / JavaScript:** `package.json`, `tsconfig.json`, `pnpm-workspace.yaml`, `bun.lockb`, `deno.json`.
   - **Python:** `pyproject.toml`, `setup.py`, `poetry.lock`, `Pipfile`, `requirements.txt`.
   - **Rust:** `Cargo.toml`, `Cargo.lock`.
   - **Go:** `go.mod`, `go.sum`.
   - **C / C++:** `CMakeLists.txt`, `Makefile`, `meson.build`.
   - **Containers & CI:** `Dockerfile`, `docker-compose.yml`, `.github/workflows/`, `.gitlab-ci.yml`.

2. **Identify Protected and Framework-Enforced Directories:**
   - Never move or rename framework-routed or compiler-enforced entrypoints unless explicitly instructed:
     - **Next.js:** `app/`, `pages/`, `public/`.
     - **Nuxt / SvelteKit / Astro:** file-based route folders (`routes/`, `src/routes/`, `pages/`).
     - **Rust:** `src/main.rs`, `src/lib.rs`, `src/bin/`, `tests/`, `benches/`.
     - **Python Packages:** top-level package namespace directories containing `__init__.py`.
     - **Go:** `cmd/`, `internal/`, `pkg/`.
     - **CI/CD:** `.github/workflows/`.

3. **Respect `.gitignore` and Exclusions:**
   - Always exclude ignored dependency and output directories from audits (`node_modules/`, `venv/`, `.venv/`, `target/`, `dist/`, `build/`, `.git/`, `__pycache__/`).
   - Use `git check-ignore <path>` or `ripgrep` respecting `.gitignore` to avoid inspecting generated build files as source files.

---

### Step 2: Audit the Current File Structure

Perform a comprehensive structural scan across the project, evaluating the repository against the **Four Cleanup Pillars**:

#### Pillar 1: Directory Organization
- **Cluttered Root:** Identify loose utility scripts, test files, scratch files, or documentation dumped in the project root.
- **Logical Grouping:** Propose grouping loose files into standard conventions:
  - `src/` (or package directory): Core application/library source code.
  - `tests/` (or `test/`, `spec/`): Unit, integration, and end-to-end tests.
  - `scripts/`: Dev, operational, build, or deployment helper scripts.
  - `docs/`: Guides, architectural decision records (ADRs), specifications, API references.
  - `config/`: Secondary tool configurations where supported.
  - `assets/` (or `public/`, `static/`): Static images, fonts, media, schemas.

#### Pillar 2: Dead Files, Artifacts, and Duplicates
- **Temporary & Editor Artifacts:** Flag `.DS_Store`, `Thumbs.db`, `*.tmp`, `*.bak`, `*.swp`, `*~`, `.vscode/*.log`.
- **Tracked Build Residue:** Detect tracked compiler artifacts (`*.o`, `*.so`, `*.pyc`, `dist/`, `coverage/`).
- **Orphaned Files:** Search for source files with zero incoming imports or references across the repository (using `grep_search` / `ripgrep`).
- **Duplicate Modules:** Detect duplicate helper functions or utility files (e.g. `utils.js` and `helpers.js` exporting the same logic).

#### Pillar 3: Naming Conventions
- Detect inconsistent file and directory casing within the same project layer:
  - **Python / Rust:** `snake_case.py`, `snake_case.rs`.
  - **React / Vue / Svelte Components:** `PascalCase.tsx`, `PascalCase.vue`.
  - **TypeScript / JavaScript Utilities:** `kebab-case.ts` (or `camelCase.ts` depending on existing project pattern).
  - **CLI / Shell Scripts:** `kebab-case.sh`.
- Ensure consistency: If the project predominantly uses `kebab-case` for modules, flag isolated `camelCase` or `snake_case` files.

#### Pillar 4: Configuration and `.gitignore` Hygiene
- Identify tracked files that should be listed in `.gitignore` (e.g. `.env.local`, local SQLite databases, coverage folders).
- Identify scattered configuration files that modern tools allow consolidating (e.g. merging Ruff, Black, and Mypy into `pyproject.toml`, or ESLint and Prettier configs).
- *Caution:* Do not move root-bound configuration files (`tsconfig.json`, `.eslintrc.*`, `Cargo.toml`, `.env`) into subfolders unless build tools explicitly support custom config paths.

---

### Step 3: Present a Dry-Run Migration Plan (Mandatory Gate)

> [!IMPORTANT]
> **MANDATORY GATE:** Never move, rename, or delete files before presenting a clear dry-run migration plan and receiving explicit user confirmation.

Format the proposal with the following sections:

#### 1. Target Directory Tree
Display a clear visual before-and-after tree showing the proposed hierarchy:
```text
project-root/
├── src/
│   ├── components/
│   └── utils/
├── tests/
├── scripts/
└── docs/
```

#### 2. File Operations Table
List every proposed change explicitly:

| Action | Current Path | Target Path | Reason |
| :--- | :--- | :--- | :--- |
| `MOVE` | `setup_helpers.py` | `scripts/setup_helpers.py` | Move standalone utility script out of repo root |
| `RENAME` | `src/utils/stringHelper.ts` | `src/utils/string-helper.ts` | Align with project kebab-case convention |
| `DELETE` | `src/old_api.bak` | *(Remove)* | Dead backup file |
| `IGNORE` | `.env.development` | `.gitignore` | Prevent committing sensitive environment configs |

#### 3. Impact Analysis
List all files and configs requiring refactoring as a direct consequence of the moves:
- **Import Statements:** Files whose `import` / `require` / `use` statements will need updating.
- **Config & Build Files:** `tsconfig.json` path aliases, `vite.config.ts`, `Dockerfile`, `Makefile`, `docker-compose.yml`.
- **CI / Workflows:** `.github/workflows/*.yml` step paths.
- **Documentation:** Broken relative links in `README.md` or `docs/`.

#### 4. User Approval Prompt
Pause and ask the user to review the dry-run plan. Wait for confirmation or specific adjustments before touching any files.

---

### Step 4: Execute Moves and Refactor References

Once approved:

1. **Use Git-Aware Operations:**
   - If inside a Git repository, always use `git mv` instead of standard file moves/deletions to preserve file history and blame.
   - For case-only renames on case-insensitive filesystems (macOS APFS / Windows NTFS), use a two-step move:
     ```bash
     git mv Helper.ts temp_Helper.ts && git mv temp_Helper.ts helper.ts
     ```

2. **Update Imports and References:**
   - Systematically update relative paths (`../`) and alias paths (`@/`) in all affected source files.
   - Grep across the entire codebase for references to moved filenames (including raw strings, dynamic `import()`, `require()`, script invocations, and asset URLs).
   - Update path targets in `package.json` (`main`, `module`, `exports`, `scripts`), `tsconfig.json`, `pyproject.toml`, Dockerfiles, and CI workflow files.

3. **Update `.gitignore`:**
   - Append rules for any newly identified build artifacts, caches, or environment files.

4. **Clean Up Empty Directories:**
   - Remove parent directories left completely empty after migrations.

---

### Step 5: Verify Integrity

Run automated verification steps to ensure no links, types, or build steps broke:

1. **Typechecking & Linting:**
   - TypeScript/JS: `npx tsc --noEmit` or `npm run typecheck`, `npm run lint`
   - Python: `mypy .`, `ruff check .`, `flake8`
   - Rust: `cargo check`
   - Go: `go vet ./...`

2. **Test Suite Execution:**
   - Run unit/integration tests (`npm test`, `pytest`, `cargo test`, `go test ./...`) to ensure module resolutions succeed at runtime.

3. **Git Status Review:**
   - Run `git status` to verify staged changes, ensure renames are cleanly tracked, and confirm no untracked dangling files remain.

---

## Gotchas & Edge Cases

- **Framework Routing & Entrypoints:** In frameworks with file-based routing (Next.js `app/`, Nuxt `pages/`, Astro `src/pages/`, SvelteKit `src/routes/`), moving files alters public URLs and API endpoints. Only reorganize non-routed shared directories (`components/`, `lib/`, `utils/`) unless the user explicitly requested route changes.
- **Case-Insensitive Filesystems:** Renaming `File.ts` to `file.ts` on macOS or Windows will fail with `destination already exists` if attempted directly. Always use a two-step `git mv` with an intermediate temporary filename.
- **Dynamic Imports and String Paths:** Static AST refactoring and simple import search-and-replace can miss dynamic `require()`, `import()`, file reads (`fs.readFileSync()`, Python `open()`), or shell script invocations. Always search for the base filename across all text files.
- **Tooling Config Root Requirements:** Many tools (`.eslintrc`, `.prettierrc`, `tsconfig.json`, `Cargo.toml`, `.env`) require config files to be in the project root by default. Never move root-bound configs into `config/` unless build/run scripts are explicitly updated with `--config flags`.
- **Package Exports (`package.json` / Python `__all__`):** Moving public API files can break external consumers if library entrypoints are modified. Check `package.json` `exports` or library entrypoint modules (`index.ts`, `__init__.py`, `lib.rs`) before moving exported files.
