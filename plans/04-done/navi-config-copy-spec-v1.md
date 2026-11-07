# Navi Config Page — UX Copy & State Spec (v1)

Outcome: **shipped — 2026-11-05.** All eight screens (S1–S8) plus the loading skeleton,
load-error retry panel, search no-match panel, dirty-count pill, disabled-while-clean
Save, leave guard, styled confirm dialogs, inline validation and the new Danger zone are
implemented in `res/inject-scripts/shell/import-lua-script.js`. Backend gaps are
documented, not faked (§7 deviations). Gates: `./install.sh build` ✓ ·
`make test` 98/98 ✓ · `make test-gate07` 7/7 ✓ · `pytest -q tests/integration/ui/`
42 passed ✓ (`tests/reports/2026-11-05-ui-config-copy-spec.md`). Manual device
reflow/controller checks remain open (§9 Manual row).

- **Status:** implemented (v1)
- **Date:** 2026-11-05
- **Owner:** Navi CEF UI (`res/inject-scripts/shell/import-lua-script.js`)
- **Scope:** visible language, hierarchy, state handling and validation of the Navi
  Configuration page (`#sls-config-page-container`). No backend contract changes.
- **Supersedes:** —

---

## 1. Problem

The page is functionally complete but written for its author, not its user. A first-time
user meets internal nouns (`CloudRedirect`, `UseWhitelist`, `LogLevels`, `ManifestIds`)
as labels, the save state is a coloured pill with no count, `Save Changes (Ctrl+S)` is
always enabled, destructive controls sit next to the primary action, there is no
leave-guard, no per-field error, and the load state is a single line of text.

Goals: discoverability, one primary action, explicit save state, recoverable errors,
destructive actions isolated behind confirmation, keyboard + controller parity.

## 2. User and success

A new Navi user who does not know what a depot or a manifest is, and who is afraid of
breaking their Steam install. They need to find one setting, understand what it changes,
change it, and know it was saved — without opening a YAML file or a log.

Success: they changed 1–3 settings, saw `Saved.`, learned whether a Steam restart is
needed, and never had to ask what a control does.

## 3. Decisions locked for v1

| # | Decision | Source |
|---|---|---|
| 1 | **Plain label, technical key demoted to helper text.** Never delete the real name (search must still find `CloudRedirect`, `UseWhitelist`, `LogLevels`). | user answer 1(a) |
| 2 | Copy plus minimal structural additions are in scope (disabled Save, danger zone, leave guard, inline errors, skeleton). | user answer 2 |
| 3 | Danger zone: reset all settings, remove all save redirections, disconnect provider (link only). Type-to-confirm `RESET` for reset. | user answer 3 (default) |
| 4 | Only real states: backend unreachable, invalid field, OAuth expired, offline provider, no managed games. Read-only is `[assumption]` and is **not** shipped in v1 (no backend signal). | user answer 4 (default) |
| 5 | Breakpoints 375 / 768 / 1280; 1280 and 768 are the shipping targets, 375 is a completeness exercise. | user answer 5 (default) |

## 4. Screen map

| Screen | Section id | Goal | Primary action | Way out |
|---|---|---|---|---|
| S1 Configuration (shell) | — | Orient, search, save | Save changes | Section tabs / Steam tab bar |
| S2 Cloud saves | `sls-cfg-sec-cloudsaves` | Keep saves out of Steam Cloud | Save changes | S1 |
| S3 Provider keys | `sls-cfg-sec-api` | Add a key only when asked | Save changes | S1 |
| S4 Unlocking | `sls-cfg-sec-general` | Choose what unlocks | Save changes | S1 |
| S5 Account | `sls-cfg-sec-profile` | Change what only you see | Save changes | S1 |
| S6 Library | `sls-cfg-sec-overrides` | List games, DLC, files | Save changes | S1 |
| S7 Diagnostics | `sls-cfg-sec-logging` | Get a log when troubleshooting | Save changes | S1 |
| S8 Danger zone | `sls-cfg-sec-danger` | Reset / revoke deliberately | Reset settings | S1 |

## 5. Copy deck

Sentence case throughout. `→` separates label from helper. Ellipsis is `…`.

### S1 — Configuration (shell)

| Element | Copy |
|---|---|
| H1 | `Configuration` |
| Description | `Change how Navi unlocks games, redirects saves and adds features to Steam. You'll see it when a change needs a restart.` |
| Context | `Navi beta-v1.2.0 · Saves to config.json on this computer` (version read from `[data-navi-version]`) |
| Sidebar wordmark | `Navi` |
| Search label | `Search settings` |
| Search placeholder | `Try "backup" or "depot"` |
| Search clear | `Clear search` |
| No results | `No settings match "<query>". Try "backup", "saves" or "reset".` |
| Tabs | `Cloud saves`, `Provider keys`, `Unlocking`, `Account`, `Library`, `Diagnostics`, `Danger zone` |
| Save status — clean | `All changes saved` |
| Save status — dirty | `<n> unsaved change` / `<n> unsaved changes` |
| Save status — saving | `Saving…` |
| Save status — just saved | `Saved just now` |
| Primary | `Save changes` (disabled while clean) |
| Shortcut hint | `Ctrl+S` |
| Secondary | `Discard changes` (only when dirty) |
| Discard dialog | `Discard your changes?` / `Your settings will go back to how they were before you started. This can't be undone.` / `Discard changes` · `Keep editing` |
| Leave warning | `You have unsaved changes. Leave without saving?` (native confirm; `Cancel` aborts the hide) |
| Restart button | `Restart Steam` |
| Restart dialog | `Restart Steam now?` / `Games will close and anything you haven't saved will be lost.` / `Restart Steam` · `Not now` |
| Save success | `Saved. Your new settings are active.` |
| Save error | `We couldn't save your changes. Nothing was lost — they're still on this page. <server message>` |
| Validation banner | `<n> settings need attention before saving.` |
| Loading | `Loading your settings…` + skeleton |
| Load error | `We can't reach Navi right now. Your settings didn't change.` / `Try again` / `If this keeps happening, restart Steam.` |

### S2 — Cloud saves

| Element | Copy |
|---|---|
| H2 | `Cloud saves` |
| Description | `Keeps saves for unlocked and family-shared games outside Steam Cloud. Choose this computer, or connect an account to sync between devices.` |
| Card | `Status` |
| Master row | `Redirect save files` → `Also called CloudRedirect. Turning this on or off needs a Steam restart.` |
| Master action | `Turn off` / `Turn on` |
| Service row | `Navi service` · `Running` / `Not running` · `<n> games redirected` |
| Pending | `<n> change waits for a restart` · `Restart Steam` |
| Tip | `Steam Cloud can overwrite redirected saves. Turn off Steam Cloud under Unlocking to stop that.` · `Open Unlocking` |
| Card | `Where saves are stored` |
| Provider row | `Where saves are stored` → `Pick this computer for offline play, or connect a cloud account to sync between devices.` |
| Options | `This computer (offline)`, `Google Drive`, `Microsoft OneDrive`, `Cloudflare R2`, `Amazon S3` |
| Auth badges | `Connected` / `Not connected` / `Offline` |
| Auth actions | `Reconnect` / `Connect <provider>` / `Disconnect` |
| Local info | `Saves are kept on this computer, inside Steam's cloud_redirect folder. No account or internet needed.` |
| Offline notice | `Local offline storage does not use a cloud sign-in.` |
| Sign-in error | `Provider sign-in failed. <reason>` |
| Card | `Backup & safety` |
| Backup toggle | `Back up before resetting` → `Saves a copy before Navi resets or restores a save. Recommended.` |
| Retention | `Backups to keep` → `From 1 to 100. The oldest backup is deleted first.` |
| Card | `Your Steam Cloud` |
| Cloud heading | `Your Steam Cloud` · `Games and saves stored for your account.` |
| Breadcrumb | `Home > Account > Your Steam Cloud` |
| Search | `Search saves` · `Game name or AppID` |
| Buttons | `Refresh list`, `Back to games`, `Show files` |
| Table headers | `Game`, `Files`, `Size`, `Action` |
| Loading | `Loading your saves…` |
| Empty | `No games have cloud saves yet. Unlock a game and play it once — its saves will appear here.` |
| Error | `We couldn't load your cloud saves. Check your connection, then refresh.` |
| Drill-down subtitle | `These saves are in your Steam Cloud.` |
| Toolbar actions | `Sync now`, `Back up`, `Restore`, `Reset`, `Remove redirection` |
| Drill-down empty | `No files saved for <game> yet. Play the game, then choose Sync now.` |
| Drill-down loading | `Loading saves for <game>…` |

### S3 — Provider keys

| Element | Copy |
|---|---|
| H2 | `Provider keys` |
| Description | `Some providers ask for a key before they'll send game files. Keys stay in config.json on this computer and are only sent to the provider that asked.` |
| Card | `Download providers` → `Optional. Navi works without keys unless a provider blocks your downloads.` |
| Ryuu | `Ryuu key` → `Used for Ryuu manifests and depot lists.` · `Paste your Ryuu key` |
| DepotBox | `DepotBox key` → `Used for DepotBox manifests and depot lists.` · `Paste your DepotBox key` |
| HubcapDB | `HubcapDB key` → `Used for HubcapDB manifests and depot lists.` · `Paste your HubcapDB key` |
| Reveal | `Show` / `Hide` |
| Privacy | `Keys are never shown in logs.` |
| Cleared key | `Remove key` |

### S4 — Unlocking

| Element | Copy |
|---|---|
| H2 | `Unlocking` |
| Description | `Choose which games Navi unlocks and how it behaves with Steam.` |
| Card | `Library access` |
| Allow list | `Only unlock the games I list` → `On: just the games in Library. Off: every game except the ones you list.` |
| Family lock | `Release family sharing lock` → `Lets you and your family play at the same time.` |
| Updates | `Stop updates for extra games` → `Applies to games you don't own. Games you own still update.` |
| Card | `Steam Cloud` |
| Steam Cloud | `Turn off Steam Cloud` → `Stops Steam from syncing saves. Recommended if you use Navi cloud saves.` |
| Conflict note | `Cloud saves are on. Steam Cloud can overwrite them.` · `Open Cloud saves` |
| Card | `Compatibility` |
| Safe mode | `Safe mode` → `Turns Navi off when this Steam version isn't known to work. Recommended on Steam Deck.` |
| Hash warning | `Warn me about untested Steam updates` → `Shows a notification when Steam updates to a version Navi hasn't been tested with.` |
| Card | `Automation` |
| Ready notification | `Tell me when Navi is ready` → `Shows a notification when Navi finishes loading.` |
| Command API | `Allow commands from other apps` → `Lets tools send commands to Navi through /tmp/navi.API. Only turn this on if you use such a tool.` |
| Schema retries | `Achievement detail retries` → `How many times Navi asks Steam for achievement details. 0 uses only the saved copy.` |

### S5 — Account

| Element | Copy |
|---|---|
| H2 | `Account` |
| Description | `These changes are only visible in your Steam client. They are not sent to Steam.` |
| Card | `Profile` |
| Display name | `Display name` → `Replaces your Steam name in this client. Leave empty to use your real name.` · `TropicalFrog3` |
| Email | `Email` → `Shown in this client only. Leave empty to hide it.` |
| Wallet | `Wallet balance` → `Shown in this client only. Set 0 to hide it.` |
| Card | `Spoofing` → `Spoofing makes a game or Steam see different account details. Most people don't need this.` |
| Idle status | `Idle status` → `Shows a game as running while you're away. Set the game to 0 to turn this off.` · `Game` · `Title shown` |
| Unowned status | `Unowned status` → `Shown while an unlocked game runs. Set the game to 0 to turn this off.` · `Game` · `Title shown` |

### S6 — Library

| Element | Copy |
|---|---|
| H2 | `Library` |
| Description | `Games, DLC and download files Navi manages. Lists take one entry per line.` |
| Card | `Games` |
| Unlocked | `Unlocked games` → `One AppID per line. Adding a game also includes its DLC.` |
| Extra | `Extra games` → `Games that aren't in your library. Use this only for games you don't own — downloads of shared games can break.` |
| Offline | `Show as offline` → `These games think Steam is offline.` |
| Card | `Downloads` |
| Versions | `Lock to a version` → `Keeps a game on one file version. Format: AppID: ManifestID.` |
| Skipped depots | `Skip these depots` → `Navi won't download these depots.` |
| Card | `Game names & play-as` |
| Names | `Game names` → `Changes the title shown in your library. Games you own only.` |
| Play as | `Play as another game` → `Makes a game report a different AppID so multiplayer works. Never run two games as the same AppID at once.` |
| Card | `Advanced maps` |
| Tokens | `App tokens` → `Product info tokens from Steam. Rarely needed.` |
| CD keys | `Legacy CD keys` → `Required by some older games.` |
| Purchase dates | `Purchase dates` → `Changes the date a game shows as bought. Games you own only.` |
| Steam ID | `Steam ID override` → `Only needed when a game reads your Steam ID before Navi can stand in.` |
| Card | `Raw YAML` |
| DLC data | `DLC details` → `Extra DLC mapping, only needed when a game reaches Steam's 64-DLC limit.` |
| Denuvo | `Denuvo account locks` → `Blocks these games from unlocking on other accounts.` |

### S7 — Diagnostics

| Element | Copy |
|---|---|
| H2 | `Diagnostics` |
| Description | `Logs help you and the Navi team find problems. Leave these on their defaults unless you're troubleshooting.` |
| Card | `Log detail` |
| Presets | `Normal`, `Fewer messages`, `Problems only`, `Silent` |
| Raw value | `Log levels` → `Bitmask value. Only change this if you know the flags.` |
| Flags | `Trace`, `Once`, `Debug`, `Warn`, `Error`, `Info`, `Short notifications`, `Long notifications` |
| Card | `Debug tools` |
| Extended | `Log every Steam call` → `Makes the log file very large. Only for debugging.` |
| Interfaces | `Write Steam's interface list to the log` → `Used when you report a problem.` |

### S8 — Danger zone

| Element | Copy |
|---|---|
| H2 | `Danger zone` |
| Description | `These actions change or delete your Navi settings. You can't undo them.` |
| Reset | `Reset all settings` → `Puts every setting back to its default: unlocked games, cloud provider, keys and logs. Your Steam account and save files are not touched.` · `Reset settings` |
| Reset dialog | `Reset all Navi settings?` / `Every setting goes back to its default. Your Steam account and save files are not touched. This can't be undone.` / field `Type RESET to confirm` / `Reset settings` (disabled until match) · `Cancel` |
| Mismatch | `That doesn't match. Type RESET exactly.` |
| Redirections | `Remove all save redirections` → `Stops redirecting saves for <n> games. Files already redirected stay where they are.` · `Remove redirections` |
| Redirections empty | `No games have redirected saves right now.` · `Remove redirections` disabled |
| Nothing to remove | `No save redirections to remove.` (info banner, defensive) |
| Redirection dialog | `Remove redirections for <n> games?` / `Steam Cloud manages these saves again. No files are deleted.` / `Remove redirections` · `Cancel` |
| Success | `<n> settings reset. Restart Steam to finish.` / `Redirections removed for <n> games.` |
| Redirection failure | `Redirections removed for <x> of <n> games. <y> could not be removed.` / `We couldn't remove any redirections. Nothing was lost — try again.` |
| Failure | `Reset didn't finish: <reason>. Nothing was lost — your settings are unchanged.` |

## 6. Reflow

| Width | Nav | Content | Save area |
|---|---|---|---|
| 375 | Horizontal scrollable pill strip under the title | 1 column; rows stack label over full-width control; tables become cards | Sticky bottom bar, full-width primary |
| 768 | 200 px icon rail | 1 column, rows side-by-side when control ≤ 280 px | Footer inside content column |
| 1280 | 240 px sidebar, search on top, save block at the bottom | 980 px max, 2-column rows | Status, Save, Discard, hint |

Controller (Big Picture, 1280×800): single vertical focus column per card, 2 px
`#1A9FFF` ring with 2 px offset, A activates, B closes the top dialog, D-pad moves rows,
text fields raise Steam's on-screen keyboard, no hover-only affordances, targets ≥ 24×24.

## 7. Implementation plan (structural)

All work is inside `renderConfigPage()` / `showConfigPage()` of
`res/inject-scripts/shell/import-lua-script.js`. `src/view/assets` is a **symlink** to
`res/inject-scripts`, so `src/view/assets/shell/import-lua-script.js` is the same
physical file — there is no second copy to keep in sync.

1. **Shell** — H1 + description + version context; sidebar wordmark, search label,
   7 pills (Danger zone added), status line, Save (disabled while clean), hint,
   Discard, Restart.
2. **Dirty tracking** — count changed fields by diffing the serialized form against
   `initialFormState`; pill text `n unsaved changes`; `window.__naviConfigDirty` for the
   leave guard; Save disabled when clean.
3. **Leave guard** — `hideConfigPage()` prompts when dirty and the page is visible.
4. **Validation** — `validateForm()` blocks save for numeric list fields and the schema
   retry count; inline `aria-invalid` messages; retention is advisory (server clamps to
   1–100, existing contract).
5. **States** — skeleton on load; explicit load-error panel with retry; empty/error copy
   for the cloud table.
6. **Danger zone** — reset posts a defaults payload to `POST /config/full`
   (no `ManagedApps`, so redirections are preserved), then reloads and re-renders;
   remove-redirections loops `toggleAppRedirect(id,false)`; typed `RESET` gate.
7. **Dialogs** — one reusable `slsConfirmDialog()` built with `createElement`/
   `textContent` (no untrusted `innerHTML`), focus-trapped, B/Escape closes.

### Invariants touched

- `.agents/rules/steam_cef_ui_patterns.md` §4: typed `RESET` for resets, safe DOM
  construction for dynamic values, no modal recursion, single modal state source.
- Element IDs consumed by `tests/integration/ui/pages/import_config_page.py` are
  preserved (`#sls-cfg-save-btn`, `#sls-cfg-discard-btn`, `#sls-cfg-dirty-pill`,
  `#sls-cfg-status-banner`, `#sls-cfg-search`, and every field id).
- Agent rule: `res/` is packaged verbatim; `src/view/assets` symlinks into it, so a
  single edit covers both paths (verified with `cmp`, which is trivially identical).

### Deviations from the copy deck (backend gaps, documented not faked)

| Deck item | v1 behaviour | Reason |
|---|---|---|
| Read-only banner | not shipped | no read-only signal from `/config/full` |
| `Delete all backups` | not shipped | no delete-backups endpoint |
| `Copy diagnostics` | not shipped | no export endpoint |
| Steam Web API key row | not shipped | `autocrack` is not in `applyFullConfigJson` |
| AutoCrack card in Library | not shipped | same payload gap |
| Log detail as a 3-option select | presets relabelled instead | avoids re-mapping `LogLevels` semantics |
| `Open Library` in cloud empty state | omitted | no in-page library route |
| Account/version context | version from `[data-navi-version]`, no account name | no account name available to the script |
| `AdditionalApps` on reset | reset writes `[]`, not the template's `3934270` | `res/config.yaml` / `config_default.hpp` still ship a leftover template entry; a reset must not inject a game the user never added |

## 8. State matrix

| Screen | Loading | Empty | Error | Success |
|---|---|---|---|---|
| S1 | skeleton shell | search no-match panel | backend unreachable + retry | `Saved.` + status |
| S2 | skeleton table | no managed games + no search match | OAuth expired / list load | per-action toast |
| S3 | shell skeleton | "no keys" helper, valid state | rejected key (advisory) | field-level "saved" via status |
| S4 | skeleton cards | n/a (booleans) | save failure in S1 | status line |
| S5 | skeleton cards | empty fields = defaults | field validation | status line |
| S6 | skeleton cards | empty lists + example | line-level format error | status line |
| S7 | skeleton cards | n/a | oversized log (unchanged) | status line |
| S8 | n/a | n/a (counts shown) | reset failure with reason | reset banner + restart |

## 9. Verification

| Gate | Command | Expectation |
|---|---|---|
| Build | `./install.sh build` | 32-bit engine builds, assets synced |
| Unit | `make test` | green |
| UI journeys | `make test-ui-p0` | UJ-05/07/09 green (tests updated to the new save contract) |
| UI P1 | `pytest -q tests/integration -m ui_p1` | green |
| Gate 0.7 | `make test-gate07` | green |
| Manual | `cef-cdp-injection` on a live client | 375/768/1280 reflow, controller focus, dirty → leave guard |

## 10. Risks

1. **Save-button contract change** (`disabled` while clean) is asserted by UJ-05; the test
   is updated to the new contract, not weakened.
2. **Retention stays server-clamped**; client validation is advisory to avoid duplicating
   the clamp in two places.
3. **Reset writes defaults for page-managed keys only**; `ManagedApps` and `PrefixSync`
   are intentionally untouched, so "reset" cannot silently drop redirections.
4. **Asset paths**: `src/view/assets` symlinks to `res/inject-scripts`, so drift is not
   possible; the `cmp` check in the verification set is a cheap guard against the
   symlink being replaced by a real copy.
5. **Native `confirm()`** for the leave guard is a deliberate low-risk fallback; a styled
   dialog is a follow-up once the history interception point is validated.

## 11. Assumptions / open questions

1. Danger-zone endpoints: reset uses `/config/full`; bulk redirection removal loops the
   per-app toggle. Validate on a live client that 12 sequential toggles complete.
2. Read-only mode and backup deletion need backend support before their copy ships.
3. Typed `RESET` is enforced client-side only; a server-side `{"confirm":"RESET"}` guard
   would be required for defence in depth (out of scope here).
4. 375 px is not a shipping target for the Steam client; validated at 1280 and 768.
5. Helper-text contrast (`#8B929A` on `#23262E`, ≈4.6:1) is `[to validate]` with a
   contrast checker before sign-off.
