# Changelog

## [friends-of-joanna-v0.3.1] - 2026-08-01

### Refactors and Cleanup

- **Removed duplicated player-2 mission option menu sections** — Consolidated the separate player-2 control/display menu blocks in `mainmenu.c` into shared handlers that resolve the active player at runtime.
- **Removed hardcoded global subtitle/mission-time toggles in menu flow** — Replaced direct global option wiring with per-player profile-backed accessors in the options and multiplayer setup path.

### Renamed / Identified Functions

- **`menudialog00103608` renamed to `endscreenAcceptMissionHandleDialog`** to clarify that it handles Accept Mission dialog flow for the endscreen retry/accept path.
- **Player-scoped subtitle helpers introduced**: `optionsGetInGameSubtitlesForPlayer`, `optionsGetCutsceneSubtitlesForPlayer`, `optionsSetInGameSubtitlesForPlayer`, `optionsSetCutsceneSubtitlesForPlayer`.

## [friends-of-joanna-v0.2.3] - 2026-03-14

### New Features

- **Classic Sight option in multiplayer settings** — Classic Sight and Show Lives are now configurable per-player in the multiplayer setup screen. Settings are stored in the extended INI rather than in the save file, so they no longer risk corrupting the save format.
- **Team lives HUD display** — The HUD now shows team lives remaining during team game modes, giving players a clear at-a-glance view of how many respawns the team has left.
- **Improved respawn logic for team missions** — Respawn behaviour in team modes now correctly accounts for the number of player lives remaining, so the game handles out-of-lives situations properly instead of allowing infinite respawns.
- **CITRAINING stage support for team modes** — Mission stage handling for team game modes now includes the CITRAINING stage.

### Bug Fixes

- **AI null-pointer crash on respawn** — AI characters could retain a stale reference to a player that was respawning, leading to a null-pointer access and potential crash. References are now cleared when a player respawns.
- **Keyboard input fix + vi insert shortcut** — Corrected a regression in keyboard input handling in the menu system and added a vi-style insert-mode shortcut for text entry fields.

### Full Changelog

<https://github.com/cylonicboom/pd-friends-of-joanna/compare/friends-of-joanna-v0.2.2...friends-of-joanna-v0.2.3>
