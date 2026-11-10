---
name: release-workflow
description: End-to-end workflow for bumping versions, compiling 32-bit release bundles, running automated tests, and publishing GitHub releases for Navi.
---

# Navi Release Workflow Skill

## Overview
This skill documents the standard, idempotent procedure for preparing, validating, and publishing a new release for Navi.

---

## Step 1: Version Bumping
Two files define and track the release version:

1. **`src/core/version.hpp`**:
   - Update `VERSION` string:
     ```cpp
     constexpr const char* VERSION = "<TARGET_VERSION>";
     ```

2. **`res/version`**:
   - Update `Version:` line to match `<TARGET_VERSION>` (e.g., `beta-v1.2.0`).
   - Update `Changelog: |` block with a concise release summary and bulleted `Highlights:`.
   - Maintain the `DownloadUrl` template line:
     ```yaml
     DownloadUrl: https://github.com/TropicalFrog3/navi/releases/download/${version}/navi.zip
     ```

---

## Step 2: Build & Production Bundle Packaging
The auto-updater and release distribution rely on `prod-release/` and `navi.zip`.

1. **Execute Release Command**:
   Run `./install.sh release` from the repository root:
   ```bash
   ./install.sh release
   ```
   > [!NOTE]
   > The command runs `scripts/lib/release.sh`, which builds via `make` (falling back to Docker via `docker/build.sh` if host 32-bit curl or compiler headers are unavailable), verifies that `updates.yaml` hash count does not decrease, stages runtime files into `prod-release/navi/`, generates `prod-release/update-manifest.yaml`, and packages `navi.zip`.

2. **Verify Generated Artifacts**:
   - Check that `prod-release/update-manifest.yaml` contains `version: "<TARGET_VERSION>"` and valid SHA-256 hashes for all included files.
   - Confirm that `navi.zip` was updated in the repository root.

---

## Step 3: Test Suite Verification
Never publish a release without running the test suite:

```bash
./install.sh test
```

Ensure all test stages pass with zero errors:
- GoogleTest Unit Tests (`bin/unit_tests`): Core, Model, Presenter, and View tests
- Python Integration Tests (`tests/integration`): Migrations, update staging, tamper detection, and installer safety
- Code Coverage (`./install.sh coverage`): `gcovr` coverage report generation

For live UI verification, run:
```bash
./install.sh test-flow
```
*(or `pytest -q --live tests/integration/test_steam_ui_flow.py`)*

---

## Step 4: Git Commit & Tagging
1. **Commit Bumps**:
   ```bash
   git add res/version src/core/version.hpp
   git commit -m "feat: Bump version to <TARGET_VERSION> and update changelog with new features and improvements"
   ```

2. **Create Git Tag**:
   Create a lightweight tag pointing to the bump commit:
   ```bash
   git tag <TARGET_VERSION>
   ```

---

## Step 5: Remote Synchronization
Push commits and tags to GitHub:

```bash
git push https://<TOKEN>@github.com/TropicalFrog3/navi.git main <TARGET_VERSION>
git fetch https://<TOKEN>@github.com/TropicalFrog3/navi.git main:refs/remotes/origin/main
```
*(Avoid storing personal access tokens permanently in `.git/config`.)*

---

## Step 6: GitHub Release Creation & Asset Upload
Because `gh` CLI may not be installed, use Python with `urllib` to create the release and upload the binary asset:

```python
import os
import json
import urllib.request

TOKEN = "<GITHUB_TOKEN>"
REPO = "TropicalFrog3/navi"
TAG = "<TARGET_VERSION>"
ZIP_PATH = "./navi.zip"

# 1. Create Release
payload = {
    "tag_name": TAG,
    "target_commitish": "main",
    "name": TAG,
    "body": "<CHANGELOG_BODY>",
    "draft": False,
    "prerelease": False  # Use True only if user explicitly requests pre-release
}

req = urllib.request.Request(
    f"https://api.github.com/repos/{REPO}/releases",
    data=json.dumps(payload).encode("utf-8"),
    headers={
        "Authorization": f"token {TOKEN}",
        "Accept": "application/vnd.github.v3+json",
        "Content-Type": "application/json"
    },
    method="POST"
)

with urllib.request.urlopen(req) as resp:
    release_data = json.loads(resp.read().decode("utf-8"))
    upload_url = release_data["upload_url"].split("{")[0]

# 2. Upload Asset
file_size = os.path.getsize(ZIP_PATH)
with open(ZIP_PATH, "rb") as f:
    zip_bytes = f.read()

upload_req = urllib.request.Request(
    f"{upload_url}?name=navi.zip",
    data=zip_bytes,
    headers={
        "Authorization": f"token {TOKEN}",
        "Accept": "application/vnd.github.v3+json",
        "Content-Type": "application/zip",
        "Content-Length": str(file_size)
    },
    method="POST"
)

with urllib.request.urlopen(upload_req) as resp:
    asset_data = json.loads(resp.read().decode("utf-8"))
    print("Release published:", release_data["html_url"])
    print("Asset published:", asset_data["browser_download_url"])
```

---

## Step 7: Verification & Security Cleanup
1. Verify the release publicly:
   ```bash
   curl -s https://api.github.com/repos/TropicalFrog3/navi/releases/tags/<TARGET_VERSION>
   ```
2. Remove any temporary scripts containing authentication tokens.
