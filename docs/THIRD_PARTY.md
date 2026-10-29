# Third-Party Software Notices & Provenance

This document records third-party components, attribution, licensing, verification digests, and provenance metadata for prebuilt binaries bundled with Navi.

---

## 1. CloudRedirect Prebuilt Binaries

Navi incorporates prebuilt 32-bit Linux binaries from the CloudRedirect project to power cloud save redirection for Steam games.

### 1.1 Component Metadata

| Property | Value |
|---|---|
| **Component Name** | CloudRedirect (Unified Linux Engine & CLI) |
| **Upstream Authors** | Selectively11, Gustavo-PGM, and CloudRedirect Contributors |
| **Upstream Homepage** | <https://github.com/Selectively11/CloudRedirect-Unified> |
| **Upstream Source Mirrors** | <https://github.com/Gustavo-PGM/CloudRedirect> |
| **Packaging Distribution** | Flatpak `org.cloudredirect.CloudRedirect` (branch: `x86_64/master`) |
| **Flatpak Commit** | `db8e1da723b1bf15307d616180f392c71da7a1c5d7c89083ea4947902715f185` |
| **Upstream Release Version** | `2.6.5` (Release date: 2026-08-18) |
| **Embedded Build Identifier** | `2.6.5+870afdb-dirty` |
| **License** | MIT License (CC0-1.0 Metadata) |

---

### 1.2 Binary Hashes & Technical Specifications

Both binaries must remain strictly **co-located** in the same directory (`res/` in the repository, `~/.local/share/navi/cloudredirect/` at runtime). `cloud_redirect_cli` resolves its own binary path via `/proc/self/exe` and loads `%scloud_redirect.so` from that directory.

| File Path | Size (Bytes) | SHA-256 Digest | ELF Architecture / Type | Symbols / Export Details |
|---|---|---|---|---|
| `res/cloud_redirect.so` | **2,192,860** | `c55ea63a02643e0df83b50241fc36a95c2a47fef8f71416479767f3b57d7c64d` | ELF 32-bit LSB shared object, Intel 80386, dynamically linked, stripped | Exports `CloudRedirect_CliMain` (`0010cc90 T CloudRedirect_CliMain`), SONAME `cloud_redirect.so` |
| `res/cloud_redirect_cli` | **13,660** | `839398e7da55bd8830dabf623542c46e74d0f1fea4cf551601a244965f89782d` | ELF 32-bit LSB pie executable, Intel 80386, dynamically linked, stripped | Entry wrapper; dynamically `dlopen`s `cloud_redirect.so` and calls `dlsym(..., "CloudRedirect_CliMain")` |

*(Note on file size reconciliation: Earlier plan iterations contained a drafting typo stating `cloud_redirect.so` was 1,234,456 bytes. The verified exact size of the artifact matching SHA-256 `c55ea63a0...` is 2,192,860 bytes.)*

---

### 1.3 Provenance & Build Identifier Reconciliation

- **Upstream Tag vs. Embedded Build Identifier:**
  - Upstream official release tag for v2.6.5 on GitHub points to clean commit `bc5e38a`.
  - The bundled prebuilt binaries report `2.6.5+870afdb-dirty` via `CR_GetVersion()` / strings inspection.
  - **Explicit Non-Identity Notice:** The embedded build identifier (`2.6.5+870afdb-dirty`) does **not** match the clean upstream tagged-source commit (`bc5e38a`). No claim of reproducible clean-tag provenance is made.
- **Root Cause & CMake Build Mechanism:**
  - Upstream `CMakeLists.txt` (lines 15–40) constructs the embedded version definition dynamically:
    ```cmake
    file(READ "${CMAKE_SOURCE_DIR}/Version.props" VERSION_PROPS_CONTENT)
    string(REGEX MATCH "<ReleaseVersion>([^<]+)</ReleaseVersion>" _ "${VERSION_PROPS_CONTENT}")
    set(CR_RELEASE_VERSION "${CMAKE_MATCH_1}") # "2.6.5"
    execute_process(COMMAND git rev-parse --short=7 HEAD ... OUTPUT_VARIABLE GIT_SHA) # "870afdb"
    execute_process(COMMAND git status --porcelain ... OUTPUT_VARIABLE GIT_STATUS)
    if(GIT_STATUS)
        set(GIT_SHA "${GIT_SHA}-dirty")
    endif()
    set(CR_SO_VERSION "${CR_RELEASE_VERSION}+${GIT_SHA}")
    ```
  - The packaging process for the Flatpak bundle built the 32-bit Linux binaries from a tree at commit `870afdb` with dirty working tree status (untracked packaging scripts or build files present during Flatpak packaging).
  - Consequently, these binaries represent the **official Flatpak distribution deployment artifacts** extracted verbatim from the upstream Flatpak package (`org.cloudredirect.CloudRedirect` at deployment commit `db8e1da723b1bf15307d616180f392c71da7a1c5d7c89083ea4947902715f185`), rather than an independently reproduced compilation from the clean upstream `bc5e38a` release tag. The binaries are incorporated verbatim without patching, stripping, or recompilation.

---

### 1.4 Redistribution Permission

Redistribution of these binary artifacts is permitted under the terms of the MIT License granted by Selectively11 and contributors, as well as the public release and distribution of the software packages.

In accordance with project governance requirements, no private correspondence, developer chat logs, or personal documents are requested, stored, or required to substantiate this permission.

---

### 1.5 Safety Blocker: Upstream Issue #211 (Interrupted Upload Recovery)

- **Issue Reference:** Upstream Community Issue #211
- **Severity / Risk:** Critical — Potential Save Data Loss
- **Description:** Issue #211 reports confirmed save-data loss on v2.6.5 during interrupted upload recovery when reconciling local saves against remote provider state.
- **Platform Scope:** The reported incident was observed on Windows; Linux reproduction has not been confirmed or established.
- **Navi Governance & Implementation Blocker:**
  - Preservation of save data is the primary invariant of the Navi cloud save subsystem.
  - Therefore, Upstream Issue #211 constitutes an explicit **Safety Blocker** for production save exposure.
  - Before `CloudSaves.Enabled` is permitted to manage live user saves, an isolated regression test must be designed and executed under Bubblewrap sandbox isolation.
  - The test must simulate network interruption during active save upload, followed by provider recovery, and verify that local game save files are neither overwritten by incomplete remote state nor deleted during conflict resolution.

---

### 1.6 MIT License Text

```text
MIT License

Copyright (c) 2026 Selectively11 and CloudRedirect contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
