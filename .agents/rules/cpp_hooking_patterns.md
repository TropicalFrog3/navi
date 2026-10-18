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
