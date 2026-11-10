# CONTEXT.md — `src/` Native Engine Workspace

## Workspace Role & Workflow

This room owns the injected 32-bit engine (`libnavi.so`), strictly layered as MVC —
`src/core` (hooks/patterns/curl/zip/IPC), `src/model` (config, depotkeys, lua/manifest,
vdf, migrations), `src/presenter` (REST :9001 + IPC controllers), `src/view` (CEF
injection/wiring). Work sequence: (1) identify the owning layer and read its files +
the matching `.agents/rules/*` invariants; (2) implement minimal targeted changes with
`grep_search` and bounded `view_file` reads only; (3) for bug fixes, write the RED test
first per `.agents/rules/test-regression.md`, then fix; (4) verify `./install.sh build`
→ `./install.sh test`; (5) hand higher-level verification to the `tests/` workspace and never
run device or release side effects (deck-ctl.sh, `./install.sh release`) from here.

| Task / Stage | Read These Files | Skip These Files | Skills / MCPs / Commands Needed |
|---|---|---|---|
| New Steam client version | `.agents/skills/steam-version-update/SKILL.md` (5 gates), `res/version`, `res/updates.yaml`, `src/core/patterns.*`, `src/core/vftableinfo.*` | `include/`, `lib/`, `third_party/`, `pkg/`, `bin/`, `obj/` | Skill `steam-version-update` (+ its `scripts/audit_client_hash.py`, `scripts/audit_vftable.py`); `./install.sh build` |
| New native hook / feature | `.agents/rules/cpp_hooking_patterns.md`, `.agents/rules/steam_cef_ui_patterns.md`, owning MVC-layer files, `src/core/globals.hpp`, `src/core/mtvar.hpp` | `res/`, `tools/`, `docs/references/` | Skill `steam-native-hooking`; `./install.sh build` |
| Cloud-saves change | `docs/references/cloud-redirect-architecture.md`, `src/core/cloud_oauth.*`, `src/presenter/{cloud_manager,cloudredirect,cloud_pending_ops,prefix_bucket_sync}.*`, `tools/cloud_pending/cloud_pending_main.cpp`, `docs/api/rest.md` | `tools/{schema-grabber,ticket-grabber}/`, `bin/` | `./install.sh check-cloudredirect`; keep `bin/navi-cloud-apply-pending` semantics (exit codes, fail-closed rollback) intact |
| Config / model / migration change | `src/model/config.*`, `src/model/config_default.hpp`, `res/config.yaml`, `res/config.diff.yaml`, `res/cache.diff.yaml`, `src/model/migrations/`, `tests/unit/test_config.cpp` | `src/view/`, `res/inject-scripts/` | `./install.sh test`; migration rules use `$(OldKey)$(NewKey):` bridge syntax |
| Bug fix (all defects) | `.agents/rules/test-regression.md`, `tests/unit/test_stubs*.cpp` (stub conventions), offending `src` file | unrelated test suites | Write RED test in `tests/unit` / `tests/integration` first; `./install.sh build && ./install.sh test` |
| IPC / REST API change | `docs/api/ipc.md` or `docs/api/rest.md`, `src/presenter/*` route owner, `src/core/api.*` | `docs/references/` | Update the matching `docs/api/*.md` contract in the same change |
| CEF-facing change | `res/CONTEXT.md` first — scripts live there | `src/core/patterns.*` unless hook wiring needed | Skill `vibe-implement-annotations`; MCP `vibe-annotations` |

## File Manifest

<!-- MANIFEST:BEGIN -->
| File | Purpose |
|---|---|
| `core/api.cpp` | Implementation of /tmp/navi.API file IPC server for client management |
| `core/api.hpp` | Interface and structures for /tmp/navi.API file IPC server |
| `core/atomic_file.hpp` | Atomic file writer with temporary staging and crash-safe rename semantics |
| `core/cloud_oauth.cpp` | HTTP OAuth loopback server and token exchange for CloudRedirect providers |
| `core/cloud_oauth.hpp` | Class declaration and state definitions for OAuth loopback authentication |
| `core/curl.cpp` | Low-level libcurl wrapper functions for HTTP operations |
| `core/curl.hpp` | Declarations of libcurl wrappers and HTTP request helpers |
| `core/decompiler.cpp` | ELF disassembly and string extraction utilities for pattern matching |
| `core/decompiler.hpp` | Interface for instruction disassembly and pattern search routines |
| `core/filewatcher.cpp` | Inotify wrapper for watching configuration and IPC file changes |
| `core/filewatcher.hpp` | Declarations for file and directory filesystem change monitoring |
| `core/globals.cpp` | Global runtime state variables and module handle definitions |
| `core/globals.hpp` | Extern declarations of global engine state and module references |
| `core/log.cpp` | Logging implementation with leveled output and timestamping |
| `core/log.hpp` | Logging macros and severity levels (DEBUG, INFO, WARN, ERROR) |
| `core/memhlp.cpp` | Memory search, pattern matching, and signature scanning helpers |
| `core/memhlp.hpp` | Memory manipulation utilities and Pattern_t struct definition |
| `core/mtvar.hpp` | Thread-safe synchronized variable wrapper template with mutex lock |
| `core/patterns.cpp` | Signature pattern definitions for steamclient.so and steamui.so hooks |
| `core/patterns.hpp` | Declarations of engine memory signatures and scan offsets |
| `core/utils.cpp` | General string, filesystem, and hashing utility functions |
| `core/utils.hpp` | Utility helper declarations including SHA-256 and string splitting |
| `core/vftableinfo.cpp` | Virtual function table RTTI scanner and index resolver |
| `core/vftableinfo.hpp` | Interface and fallback tables for VFT index resolution |
| `core/zip.cpp` | ZIP archive extraction and memory buffer decompression routines |
| `core/zip.hpp` | Declarations of ZIP file operations and entry enumeration |
| `model/achievement_merger.cpp` | Merges local and remote Steam achievement schemas into unified state |
| `model/achievement_merger.hpp` | Declarations for achievement schema caching and stat merging |
| `model/appsizes.hpp` | Data structures for application installation sizes and disk usage stats |
| `model/config.cpp` | Configuration loader, YAML parser, and in-memory setting manager |
| `model/config.hpp` | Configuration options, schema definitions, and default setting keys |
| `model/depotkeys.cpp` | Decryption key manager for Steam depot decryption keys |
| `model/depotkeys.hpp` | Interface for loading and querying depot decryption keys |
| `model/lua_parser.cpp` | Parser for app Lua plugin manifests and depot metadata |
| `model/lua_parser.hpp` | Data structures and parser interface for Lua game manifests |
| `model/manifestcode.cpp` | Steam manifest decryption code storage and retrieval |
| `model/manifestcode.hpp` | Interface for managing manifest authorization codes |
| `model/migrations/cache_migration.cpp` | Migrates legacy cache formats to current schema versions |
| `model/migrations/cache_migration.hpp` | Cache migration interface and version transition definitions |
| `model/migrations/config_migration.cpp` | Declarative YAML config migration engine with transformation bridges |
| `model/migrations/config_migration.hpp` | Declarative config migration rules and key transition handlers |
| `model/migrations/legacy_migration.cpp` | Backward-compatible parser for legacy pre-v1.0 config syntax |
| `model/migrations/legacy_migration.hpp` | Interface for converting obsolete config formats |
| `model/paths.cpp` | Steam filesystem directory layout detection and path resolution |
| `model/paths.hpp` | Steam directory locations, config folders, and save path definitions |
| `model/sdk/CAPIJob.cpp` | Implementation of Steam asynchronous API job dispatcher |
| `model/sdk/CAPIJob.hpp` | Declarations for CAPIJob asynchronous completion handlers |
| `model/sdk/CCMInterface.cpp` | Steam Connection Manager protocol handlers and packet dispatch |
| `model/sdk/CCMInterface.hpp` | Declarations for CCMInterface connection and protocol routines |
| `model/sdk/CClientUnifiedServiceTransport.cpp` | Protobuf unified service RPC message transport implementation |
| `model/sdk/CClientUnifiedServiceTransport.hpp` | Declarations for unified message RPC dispatch and service callbacks |
| `model/sdk/CNetPacket.cpp` | Network packet serialization and depot manifest manipulation |
| `model/sdk/CNetPacket.hpp` | Packet buffer wrapper and depot interception structures |
| `model/sdk/CProtoBufMsgBase.hpp` | Base class definitions for serialized protobuf network messages |
| `model/sdk/CSteamClient.hpp` | Steam client interface wrapper and interface factory definitions |
| `model/sdk/CSteamEngine.cpp` | Engine singleton lifecycle and hook dispatch manager |
| `model/sdk/CSteamEngine.hpp` | CSteamEngine class declaration and core initialization methods |
| `model/sdk/CSteamMatchmakingServers.hpp` | Matchmaking server query interface definitions |
| `model/sdk/CUser.cpp` | User authentication and SteamID accessor implementations |
| `model/sdk/CUser.hpp` | CUser class declarations for active Steam account state |
| `model/sdk/CUtl.cpp` | Valve CUtl container helper implementations |
| `model/sdk/CUtl.hpp` | Valve CUtlMemory, CUtlVector, and CUtlString template definitions |
| `model/sdk/CValidator.hpp` | Validation context for integrity checking Steam client objects |
| `model/sdk/CWebSocketConnection.cpp` | WebSocket client implementation for CEF DevTools Protocol transport |
| `model/sdk/CWebSocketConnection.hpp` | WebSocket connection class and handshake interface |
| `model/sdk/IClientAppManager.cpp` | Application install state and depot download manager wrappers |
| `model/sdk/IClientAppManager.hpp` | VFT interface for Steam IClientAppManager |
| `model/sdk/IClientApps.cpp` | App ownership, depot info, and license query wrappers |
| `model/sdk/IClientApps.hpp` | VFT interface for Steam IClientApps |
| `model/sdk/IClientCompat.cpp` | Proton compatibility tool configuration wrappers |
| `model/sdk/IClientCompat.hpp` | VFT interface for Steam IClientCompat |
| `model/sdk/IClientFriends.hpp` | Friend list, persona state, and avatar interface definitions |
| `model/sdk/IClientUser.cpp` | Client user credentials and ticket retrieval implementations |
| `model/sdk/IClientUser.hpp` | VFT interface for Steam IClientUser |
| `model/sdk/IClientUtils.cpp` | Utility functions for Steam runtime environment queries |
| `model/sdk/IClientUtils.hpp` | VFT interface for Steam IClientUtils |
| `model/sdk/binarykv.cpp` | Binary KeyValues format parser and serializer |
| `model/sdk/binarykv.hpp` | Interface and binary token definitions for Binary KeyValues |
| `model/sdk/sdk.hpp` | Master include header aggregating all Steam SDK definitions |
| `model/sdk/steam.cpp` | Steam client symbol resolution and function pointer bindings |
| `model/sdk/steam.hpp` | High-level Steam API initialization and handle helpers |
| `model/sdk/types.hpp` | Primitive typedefs, AppId_t, DepotId_t, and Steam ID structs |
| `model/vdf.cpp` | Valve KeyValues (VDF) text parser and serializer |
| `model/vdf.hpp` | Interface and node tree representation for Valve Data Format files |
| `presenter/achievements.cpp` | Achievement unlock hook presenter and unlock notification dispatcher |
| `presenter/achievements.hpp` | Declarations for achievement unlock presenter handlers |
| `presenter/apps.cpp` | App state queries, unowned game registration, and launch presenters |
| `presenter/apps.hpp` | Declarations for application lifecycle presenters |
| `presenter/autocrack_manager.cpp` | Automated SteamStub DRM unpacker and payload manager |
| `presenter/autocrack_manager.hpp` | Declarations for AutoCrack execution and unpacker triggers |
| `presenter/autoupdate.cpp` | Automated background update checker and safe mode hash auditor |
| `presenter/autoupdate.hpp` | Declarations for auto-update polling and version verification |
| `presenter/cloud_manager.cpp` | CloudRedirect CLI subprocess orchestration and cloud save manager |
| `presenter/cloud_manager.hpp` | Interface for cloud save redirection, sync, and status checks |
| `presenter/cloud_pending_ops.cpp` | Out-of-process pending save operation queue manager |
| `presenter/cloud_pending_ops.hpp` | Declarations for pending cloud save operation queue serialization |
| `presenter/cloudredirect.cpp` | CloudRedirect engine presenter, config syncing, and backend coordinator |
| `presenter/cloudredirect.hpp` | Declarations for CloudRedirect lifecycle and provider routing |
| `presenter/dlc.cpp` | DLC ownership emulation presenter and DLC list synthesis |
| `presenter/dlc.hpp` | Declarations for DLC interception and dynamic unlocking |
| `presenter/fakeappid.cpp` | Presenter for FakeAppId translation and unowned game masking |
| `presenter/fakeappid.hpp` | Declarations for FakeAppId mapping and identifier conversion |
| `presenter/hooks.cpp` | Primary entrypoint for establishing native detours and VFT hooks |
| `presenter/hooks.hpp` | Declarations and forward signatures for all engine hook initializers |
| `presenter/licenses.cpp` | License grant presenter for spoofing package and app ownership |
| `presenter/licenses.hpp` | Declarations for license validation and package grant hooks |
| `presenter/luadownload.cpp` | Automated Lua manifest and depot decryption downloader presenter |
| `presenter/luadownload.hpp` | Declarations for fetching Lua manifests from external repositories |
| `presenter/luagenerator.cpp` | Generates dynamic Lua script definitions from depot metadata |
| `presenter/luagenerator.hpp` | Declarations for procedural Lua game manifest generation |
| `presenter/main.cpp` | Navi preload engine entry point (constructor/destructor init) |
| `presenter/misc.cpp` | Miscellaneous auxiliary presenters and environment fixups |
| `presenter/misc.hpp` | Declarations for miscellaneous engine presenter routines |
| `presenter/nativeinstall.cpp` | Native Linux game installer and dependency configuration presenter |
| `presenter/nativeinstall.hpp` | Declarations for native binary installation and setup |
| `presenter/prefix_bucket_sync.cpp` | Proton prefix cloud save discovery and cross-device sync engine |
| `presenter/prefix_bucket_sync.hpp` | Declarations for prefix save bucket scanning and remote push/pull |
| `presenter/process_lock.cpp` | Filesystem process lock ensuring idle Steam during cloud ops |
| `presenter/process_lock.hpp` | Declarations for fail-closed Steam idle process guardrails |
| `presenter/removelua.cpp` | Uninstaller presenter for removing Lua scripts and resetting depots |
| `presenter/removelua.hpp` | Declarations for clean Lua manifest removal routines |
| `presenter/storeinject.cpp` | Presenter bridging CEF store requests with native engine REST API |
| `presenter/storeinject.hpp` | Declarations for store and library REST bridge dispatchers |
| `presenter/ticket.cpp` | Presenter for app ticket interception and ownership validation |
| `presenter/ticket.hpp` | Declarations for Steam ticket retrieval and synthesis hooks |
| `presenter/tier0hook.cpp` | Tier0 logging library hooks for capturing Steam debug output |
| `presenter/tier0hook.hpp` | Declarations for tier0 log interception and console routing |
| `presenter/update.cpp` | Engine update staging and binary self-replacement presenter |
| `presenter/update.hpp` | Declarations for staging and applying runtime engine updates |
| `view/cdpinject.cpp` | Chrome DevTools Protocol injection engine for Steam CEF UI |
| `view/cdpinject.hpp` | Declarations for WebSocket CDP connection and script injection |
| `view/cefsizefix.cpp` | CEF window resize and rendering fixup hooks |
| `view/cefsizefix.hpp` | Declarations for CEF layout adjustment routines |
| `view/cloud_view.cpp` | Presenter/view adapter rendering CloudRedirect UI into Steam CEF |
| `view/cloud_view.hpp` | Declarations for cloud save UI state management and modals |
| `view/storeinject_view.cpp` | View component handling DOM script injection into Steam store pages |
| `view/storeinject_view.hpp` | Declarations for store DOM event listeners and action buttons |
<!-- MANIFEST:END -->
