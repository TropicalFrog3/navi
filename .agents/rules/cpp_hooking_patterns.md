---
description: Guidelines for implementing memory hooks and byte patterns in the Navi C++ codebase.
---
# C++ Memory Hooking & Pattern Resolution

When adding or fixing hooks in `Navi`, adhere to the following rules:

## 1. Explicit Module Targeting for Patterns
`MemHlp::searchSignature` defaults to searching within `g_modSteamClient` (`steamclient.so`). 
If you are defining a `Pattern_t` for a function that resides in `steamui.so` (such as UI controllers, CEF interactions, or `CSteamUIAppController`), you **MUST** explicitly pass `&g_modSteamUI` as the final argument to the `Pattern_t` constructor.

**Example:**
```cpp
Pattern_t RunFrame
{
    "CSteamUIAppController::RunFrame",
    "66 0F EF C0 66 0F 7E 85 40 EF FF FF 66 0F 73 D0 20 66 0F 7E 85 44 EF FF FF 89 F3",
    SigFollowMode::PrologueUpwards,
    std::vector<int16_t> { 0x56, 0x57, 0xE5, 0x89, 0x55 },
    &g_modSteamUI // CRITICAL: Explicitly target steamui.so
};
```
*Failure to include the module pointer will result in `Failed to find pattern` warnings at runtime because the engine will scan `steamclient.so` instead.*

## 2. Linux Function Virtualization Differences
Do not assume functions are virtual on Linux just because they are virtual on Windows. 
If `VFTIndexes` lookups fail or cause crashes for specific `steamui.so` classes, verify via binary analysis (e.g., using `readelf` or python scripts) if the function is statically resolved. If non-virtual, remove the `VFTIndexes` lookup and implement a `Pattern_t` signature instead.

## 3. Pattern Pointer Lifecycle
It is safe to pass `&g_modSteamUI` or `&g_modSteamClient` during the global static initialization of `Pattern_t`. The global `lm_module_t` variables are populated at runtime in `main.cpp` via `LM_FindModule` before `Pattern_t::find()` dereferences them.

## 4. Prevent Detour Collisions
Never assume a pattern resolves to a unique function without verification. If two patterns resolve to the same address, `LM_HookCode` will corrupt the trampoline of the second hook (it copies the first hook's relative jump), leading to a crash. Always guard hook placement with a check against already-hooked addresses using a static `std::unordered_map<lm_address_t, std::string>`.

## 5. Beware GCC Local Clones and `regparm`
Linux GCC frequently generates local function clones that deviate from standard `cdecl`. 
- Check the assembly of the target function. If it reads arguments from `eax`, `edx`, and `ecx` at the very beginning instead of `[ebp+0x8]`, it is using `__attribute__((regparm(3)))`.
- Your `typedef` for the hook MUST match this calling convention exactly, or you will read garbage stack memory and cause corruption.

## 6. Verify Function Identity via GOT Strings
Do not trust pattern anchors implicitly. Verify you have the correct function by finding assertions, logging, or VPROF strings inside it. In Linux shared objects, these strings are often referenced as GOT-relative offsets (e.g., `lea eax, [ebx - offset]`).

## 7. CloudRedirect Prebuilt Binaries & Runtime Injection
- **Prebuilt Binaries**:
  - `res/cloud_redirect.so` (32-bit x86 Linux ELF shared library)
  - `res/cloud_redirect_cli` (32-bit x86 Linux ELF PIE executable)
  - Both binaries must remain strictly co-located in the same directory (`res/` in the repository, `~/.local/share/navi/cloudredirect/` at runtime). `cloud_redirect_cli` dynamically loads `%scloud_redirect.so` relative to its own executable path (`/proc/self/exe`).
- **Dynamic `LD_PRELOAD` Wrapper Pattern (`scripts/lib/install.sh`)**:
  - The launcher wrapper script conditionally injects `cloud_redirect.so` into the 32-bit Steam client runtime:
    ```sh
    CR_SO="$HOME/.local/share/navi/cloudredirect/cloud_redirect.so"
    if [ -f "$HOME/.config/navi/cloud_enabled" ] && [ ! -f "$HOME/.config/CloudRedirect/disable" ] && [ -f "$CR_SO" ]; then
        case ":${LD_PRELOAD:-}:" in
            *":$CR_SO:"*) ;;
            *) export LD_PRELOAD="${CR_SO}${LD_PRELOAD:+:${LD_PRELOAD}}" ;;
        esac
    fi
    ```
  - **Gate Conditions**: Requires existence of `$HOME/.config/navi/cloud_enabled` and absence of killswitch `$HOME/.config/CloudRedirect/disable`.
  - **Dedup & Precedence**: Prepends `cloud_redirect.so` to `LD_PRELOAD` using POSIX parameter expansion with `:` delimiter deduplication, preserving any user-defined `LD_PRELOAD` paths.
- **Out-of-Process Idle Helper Execution**:
  - Queue processing and save directory modifications (`bin/navi-cloud-apply-pending --process-all`) must execute strictly **before** Steam launches.
  - In-process save resets, file overwrites, or pending queue processing while `steam` or `steamwebhelper` processes are active are strictly prohibited to avoid runtime dynamic linker (`rtld`) lock deadlocks, SQLite lockups, and file corruption.

## 8. CLI Exit Code Semantics & stdout Parsing
- **Non-Standard Exit Code Semantics**:
  - `cloud_redirect_cli` does not follow traditional POSIX conventions where non-zero strictly signifies an execution failure:
    - Exit code `0`: Indicates active, logged-in, or redirected state (e.g. `status` when authenticated and enabled, `app-status` when actively redirected).
    - Exit code `1`: Represents an informational status (e.g. `status` when not logged in or disabled, `app-status` when an app is not currently redirected).
  - True execution errors typically return other error codes (e.g. 127 for not found or OS signals).
- **Caller Guardrails (`CloudManager`)**:
  - Callers like `CCloudManager` must never treat exit code `1` as a fatal failure.
  - Callers must capture `stdout` (and `stderr`), parse the structured JSON payload or text substrings (e.g., using `ExtractJsonSubstring`), and evaluate fields (such as `authenticated` or `redirected`) to determine application state.
