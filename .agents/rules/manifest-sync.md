---
name: manifest-sync
description: Auto-regenerate CONTEXT.md file manifests after source file changes.
trigger: always_on
---
# Manifest Sync Rule

After you **create, delete, rename, or change the @brief of** any authored source file,
you MUST spawn a lightweight subagent to run:

    python3 scripts/dev/sync-manifests.py

This regenerates the `<!-- MANIFEST:BEGIN -->` … `<!-- MANIFEST:END -->` sections
in each workspace CONTEXT.md. The subagent should:
1. Run the script
2. Verify exit code 0
3. Report which CONTEXT.md files were updated

If the script reports a file missing `@brief`, the subagent must add the header
before re-running.

Do NOT manually edit the manifest sections — they are machine-generated.

