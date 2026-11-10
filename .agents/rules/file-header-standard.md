---
name: file-header-standard
description: Mandate structured file headers for agent-readable source navigation.
trigger: always_on
---
# File Header Standard

When **creating a new source file** or **substantially modifying** an existing one
(changing its purpose, adding/removing major functionality), you MUST ensure it has
a structured header block as the first non-shebang content.

## Required Fields
- `@file`  — basename of the file (must match actual filename)
- `@brief` — single-line purpose (≤120 chars). This line is extracted by
  `scripts/dev/sync-manifests.py` into CONTEXT.md manifests.

## Optional Fields
- `@notes` — bullet list of institutional knowledge: gotchas, exit-code semantics,
  non-obvious coupling, debugging tips, extension points. Add these when the
  information **cannot be derived from reading the code**.
- `@see`  — cross-references to non-obvious related files (omit if the connection
  is already clear from `#include` or same-directory proximity).

## Format by Language
- C++/JS: `/** ... */` block
- Python: `""" ... """` docstring  
- Shell: `#`-prefixed comment block (after shebang)

## What NOT to Include
- `@layer` — redundant with directory path
- `@depends` — redundant with `#include` / `import`
- Changelogs — use `git log`
- Full API surface — that's what the code is for

