# Steam Gamepad Layout Rules

When writing logic to capture Steam Gamepad events (such as `vgp_ondirection`, `vgp_onbuttondown`, `vgp_onok`, `vgp_oncancel`), use the following button codes. These codes are mapped to the `e.detail.button` property.

## Button Mapping
* A = 1  (`vgp_onok` is also fired)
* B = 2  (`vgp_oncancel` is also fired)
* X = 3
* Y = 4
* LB = 5
* RB = 6
* LT = 7
* RT = 8
* D-pad Up = 9
* D-pad Down = 10
* D-pad Left = 11
* D-pad Right = 12
* Left Stick Button (L3) = 15
* Right Stick Button (R3) = 16

## Notes
- To manually trap gamepad focus inside injected Reactless modals without hacking the Steam `GamepadNavigationTree`, you should capture events globally (`window.addEventListener('vgp_ondirection', handler, true)`) and block propagation using `e.stopPropagation()`.

## Focus Node Stability During Live Polling
When writing injected UI components that poll background APIs (e.g. download progress, status badges, telemetry):
* **NEVER wipe or rebuild button elements via `innerHTML` during poll ticks.**
* Replacing a `<button>` DOM element unmounts Steam's registered `GamepadNavigationNode` (`registerSteamFocusNode`), destroying active focus and trapping controller navigation.
* **Mount Once, Update In-Place**:
  * Create button elements and register focus nodes (`registerSteamFocusNode`, `vgp_onfocus`, `vgp_onblur`, `vgp_onok`) **once** at mount.
  * On subsequent poll ticks, update dynamic state strictly in-place (e.g. `btn.textContent`, `btn.className`, `btn.title`, `btn.disabled`).
  * For toggle actions (e.g. Pause <-> Resume), toggle classes/text on the same persistent DOM node.

## Universal Input Modality Tracking
In Steam Big Picture Mode / Steam Deck, controller navigation does NOT emit `mousedown`:
* When implementing interaction tracking or cache cooldown bypasses, listen to:
  * `vgp_onok` and `vgp_ondirection`
  * `keydown`
  * `pointerdown` and `touchstart`
  * `mousedown`
