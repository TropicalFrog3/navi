# Session Handoff: Family Sharing Simultaneous Multiplayer & Lock Bypass (AppID 951440)

**Date:** 2026-11-16  
**Status:** Corrected implementation built; live verification pending  
**Component:** `src/presenter/apps.cpp`, `src/presenter/fakeappid.cpp`, `src/presenter/hooks.cpp`, `src/model/config.*`, `res/config.yaml`  
**Related Specs & Reports:**  
- [2026-11-13-handoff-family-share-lock.md](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/plans/04-done/2026-11-13-handoff-family-share-lock.md)  
- [2026-11-13-family-share-lock-verification.md](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/tests/reports/2026-11-13-family-share-lock-verification.md)  
- Upstream rejection: [AceSLS/SLSsteam PR #175](https://github.com/AceSLS/SLSsteam/pull/175) (closed unmerged) and [commit 6cba3e52](https://github.com/AceSLS/SLSsteam/commit/6cba3e52b1fbd0c8f4e6897bd2e8d702197c875b) ("Stop choking k_EMsgClientSharedLibraryStopPlaying")  

---

## 1. Problem Definition & User Report

In commit `457636a`, Navi attempted to fix the recurring 180-second countdown kick (*"This shared game is already in use and will close in 2 minutes"* / *"This account logged in on another computer"*) in New Steam Families (`FamilyGroups`) by:
1. Detecting borrowed family games via `!usr->ownsAppDirectly(appId)`.
2. Completely deleting the borrowed app from outbound `CMsgClientGamesPlayed` (`src/presenter/apps.cpp:1506-1513`).
3. Choking outbound `k_EMsgClientRichPresenceUpload` in `Apps::shouldChokeSendPacket`.

### The Core Flaw Reported by User:
> *"the fix doesn't work, example: both account must be able to play together with the same game (951440), right now it is only saying to steam that it is playing nothing so it cannot join online matches."*

- **Example Game:** AppID `951440` (*Volcanoids*).
- **The Failure:** When AppID 951440 is deleted from `CMsgClientGamesPlayed`, Valve's Connection Manager (CM) registers the borrower account as **idle (playing nothing)**.
- **Consequences:**
  - Steamworks refuses to issue valid authentication tickets (`ISteamUser::GetAuthSessionTicket` / `BeginAuthSession`).
  - `ISteamMatchmaking` lobby joins and matchmaking search fail because Steam validates them against active CM game sessions.
  - P2P networking and server connections are rejected.
  - Friends list displays the user as "Online" instead of "In-Game: Volcanoids".
  - Two accounts in the same family **cannot play together online**.

---

## 2. Live Host Reproduction & Log Evidence

Log inspect from `~/.local/share/Steam/logs/librarysharing_log.txt` on the test machine (2026-11-16):
```text
[2026-11-16 12:27:42] Shared lock for app 4324480 changed : available owner 0, preferred owner 303003632, current player 303003632
[2026-11-16 12:27:42] Shared lock for app 4324480 changed : available owner 1254383366, preferred owner 1254383366, current player 303003632
[2026-11-16 12:29:18] Launching shared app 4324480 : using available owner 1254383366 (preferred owner 1254383366)
[2026-11-16 12:29:23] Stop playing appID 4324480, ownerID 0, seconds left 180
[2026-11-16 12:29:59] Stop playing appID 4324480, ownerID 0, seconds left 180
[2026-11-16 12:30:59] Stop playing appID 4324480, ownerID 0, seconds left 120
[2026-11-16 12:32:00] Stop playing appID 4324480, ownerID 0, seconds left 60
[2026-11-16 12:32:59] Stop playing appID 4324480, ownerID 0, seconds left 0
...
[2026-11-16 12:40:39] Launching shared app 951440 : using preferred owner 1254383366
[2026-11-16 12:42:42] Launching shared app 951440 : using preferred owner 1254383366
```

### Key Binary Finding:
- The string `Stop playing appID %u, ownerID %u, seconds left %d` is present in `~/.local/share/Steam/ubuntu12_32/steamclient.so` at offset `13358524` (`0xcbd5bc`).
- This countdown is logged locally inside `steamclient.so` when Steam receives the kick packet from Valve's CM.
- Crucially, in commit `457636a`, the inbound packet hook in `src/presenter/hooks.cpp` **removed** the choke for `k_EMsgClientSharedLibraryStopPlaying` and only choked `FamilyGroupsClient.NotifyRunningApps#1`.

---

## 3. Protocol & Architecture Analysis

```
┌────────────────────────────────────────────────────────┐
│                      Steam Client                      │
│                                                        │
│  [Game: Volcanoids 951440]                             │
│       │                                                │
│       ▼                                                │
│  Steamworks API: GetAuthSessionTicket / Matchmaking    │
│       │ (requires CM registration)                     │
│       ▼                                                │
│  CMsgClientGamesPlayed                                 │
│  (game_id = 951440, owner_id = selfAccountId)          │
│       │                                                │
│       ├─────────────────────────────────┐              │
│       ▼                                 ▼              │
│  Valve CM Server               Steam Friends / Overlay │
│  (Registers active play)       (Shows in-game)         │
│       │                                                │
│       ▼ Conflict Detected                              │
│  k_EMsgClientSharedLibraryStopPlaying                  │
│  FamilyGroupsClient.NotifyRunningApps#1                │
│       │                                                │
│       ▼                                                │
│  [Navi RecvPkt Hook: CHOKE!] ◄── MUST SUPPRESS HERE    │
│  (Drops kick packets before steamclient.so processes)  │
└────────────────────────────────────────────────────────┘
```

1. **Why `DeleteSubrange` was wrong for multiplayer:**
   Stripping `game_id` from `CMsgClientGamesPlayed` tells the server the client is not running anything. Online multiplayer games that validate ownership or in-game state via Steam CM cannot create lobbies or join servers.

2. **The Two Family Sharing Modalities:**
   - **Legacy Family Sharing:** Uses license flag `0x4000`, `CheckAppOwnership` sets `familyShared = 1`.
   - **New Steam Families (`FamilyGroups`):** `CheckAppOwnership` returns `familyShared = 0`, `owner = self`. Both accounts have their own entitlement records under the group, but Valve CM detects concurrent play on the single shared license pool.

3. **Inbound vs Outbound Filtering:**
   - Outbound: AppID `951440` MUST remain present in `CMsgClientGamesPlayed` with `owner_id = g_currentSteamId.accountId()`.
   - Outbound: `k_EMsgClientRichPresenceUpload` must NOT be choked for New Steam Families so that friend joins and Steam lobbies work.
   - Inbound: Both `k_EMsgClientSharedLibraryStopPlaying` AND `FamilyGroupsClient.NotifyRunningApps#1` must be intercepted and choked in `hkCMInterface_RecvPkt`.

4. **Alternative/Fallback Path (`FakeAppIds`):**
   - For games requiring spoofed app IDs (e.g. Spacewar `480`), `FakeAppIds` already maps tickets and server queries. But when playing the real native game online on legitimate Steam servers without Spacewar, the game must report its real `game_id = 951440` with `owner_id = self`.

---

## 4. Corrected Findings (swarm, 2026-11-16)

The "keep the real AppId / choke `k_EMsgClientSharedLibraryStopPlaying`" plan is **wrong**:

1. **No in-message family marker exists.** The client's `CMsgClientGamesPlayed` builder never writes `owner_id`, and `game_flags` carries launch state, not a family bit (32-bit VA `0x1A2F280..0x1A31E01`; the same mask set is unique in both 64-bit clients). So no field rewrite can disarm the CM.
2. **Choking 9406 is futile.** The 180 s countdown and `StopApp` are driven entirely by inbound `k_EMsgClientSharedLibraryStopPlaying` (32-bit handler `0x12199a0`; 64-bit `0xfd2730`). Upstream removed that choke because *"Valve now keeps track on their backend, so choking it will just disconnect your session now"* (commit `6cba3e52`).
3. **Upstream already rejected this exact approach.** PR #175 ("hide borrowed FamilyGroups sessions from CM") was closed unmerged; maintainer: *"Use FakeAppIds to prevent it, choking the outgoing GamesPlayed message will break functionality."* The maintainer also states LLM-authored PRs will not be accepted.
4. **Hiding the entry is what broke online play.** With no `games_played` entry Steam reports "playing nothing"; Steamworks tickets, lobbies and matchmaking then fail. Upstream issue #92: *"Steam won't display your account playing AppIds it doesn't own."*

## 5. Implemented Correction (this working tree)

- `Apps::sendGamesPlayed` no longer deletes borrowed entries; presence is preserved.
- `Apps::shouldChokeSendPacket` removed (Rich Presence is never choked).
- New `FakeAppIds::getPresenceFakeAppId()`: explicit `FakeAppIds` entries win; otherwise a borrowed app is reported under `FamilyShareFakeAppId` (new config, shipped default `480`). Used only by presence packets (GamesPlayed + Rich Presence); server-browser/ping lookups keep the real AppId.
- `Apps::isFamilySharedApp()` is now a namespace-scope helper (external linkage) reused by the FakeAppIds fallback.
- `k_EMsgClientSharedLibraryStopPlaying` stays unchoked; `FamilyGroupsClient.NotifyRunningApps#1` stays choked.
- Tests updated to the new contract; `./install.sh build` and `make bin/unit_tests` compile clean (not executed).

## 6. Remaining Steps

1. **Live verification** (needs the user): install, restart Steam, launch `951440` with the lender active. Check `~/.local/share/Steam/logs/librarysharing_log.txt` for no new `Stop playing appID 951440` lines, and that the game can join online matches.
2. **Co-op caveat:** Steam shows the fake AppId's title. For Steam P2P/lobby games, *both* accounts must report the same AppId, so the lender needs an explicit `FakeAppIds: { 951440: 480 }` entry (the auto-fallback only fires for the borrower, whose license is the shared one).
3. **Do not open the upstream PR.** It duplicates the rejected PR #175; the corrected mechanism (automatic FakeAppIds fallback) is a Navi-side feature and would need maintainer buy-in before any upstream discussion.
4. If the user prefers real-AppId presence over the fake-AppId remap, set `FamilyShareFakeAppId: 0`; the borrowed game is then reported normally and the server-side lock returns.
