# Changelog

## [friends-of-joanna-v0.3.9.0] - 2026-10-03

Pre-release. This is what was going to be 0.4.0, put out as a public test first, and it covers everything since 0.3.1.

@DabDavis built the third person camera and made getting shot and dying look like something actually happened. The Lua scripting layer comes from Murk's Perfect Dark Kai netplay fork. jonaeru did the GoldenEye X model scale table and the arena names, and @JillyJane made the Willow Dark model. Thank you.

### What you need

- Your own `pd.ntsc-final.z64` in `data/` (or `data/roms/`). Friends of Joanna doesn't ship anyone else's assets.
- Optional: `pd.jpn-final.z64` for Mikado Dark, and `gex.z64` for the GoldenEye X cast (`mod_gex_characters`). Without them the carousel just skips those characters.
- Builds: Windows and Linux (x86_64 and i686), macOS (x86_64 and arm64). NTSC only — the PAL and JPN builds are out of CI for this one.

### Coming from 0.3.x

- **The save folder moved** to `perfectdark-friends-of-joanna-v0.4` (`~/.local/share/…` on Linux, `~/Library/Application Support/…` on macOS). Copy your old `-v0.3` folder over if you want your saves.
- **The launchers are gone.** Run the exe next to `data/`; no environment variables needed.
- **`use_mod_files`, `force_vanilla` and `MpArenaGroup` are removed** from modconfig. A config still naming one loses that key and keeps everything else.

### New Features

- **Third-person camera** — Press **V** (`CK_0400`) and the camera steps back behind Joanna. It sits over a shoulder and pulls in when a wall comes up, can hang off a fixed-length tether instead of the aim, and the eye and gun bob, sway and lean with the step. The body fades out when the camera is inside her, and light glares stay behind the gun and the body. (Thank you @DabDavis, and Murk for the glare occlusion)
- **Combat roll, flinch and melee for players and simulants** — Actions the engine only ever gave to solo Joanna are now available to players and simulants alike: the combat roll, flinching when shot, throwing with an arm, and the punch/kick melee chain. A roll now goes the way she is going, and a punch reaches from her rather than from the camera. (Thank you @DabDavis)
- **Simulants and players react to damage the way solo guards do** — An explosion now throws a simulant or player across the room instead of leaving them standing, a death plays the animation for where the hit landed, and a simulant's body stays solid for a second before it fades instead of going see-through as soon as it lands. (Thank you @DabDavis)
- **Drop-in, drop-out Team Missions** — A friend can join a team mission that is already running, and the last player can leave one. A leaver's results are tallied as if they had aborted and their profile is saved; anything that was watching them looks at the operative instead. A team mission can also be started with one player. For now both are on the Players panel of the debugger overlay.
- **Mikado Dark wears her own outfits** — The seven JPN Joanna bodies are mounted from your JPN rom the same way her head is, she spawns in them, and they are selectable in the Combat Simulator. `bodyheadname` in a modconfig pairs a body with a mod-registered head.
- **HUD tint per player** — In Team Missions the HUD green, the crosshair and the menu starfield take the colour of the friend you are playing. Guests and the Combat Simulator keep their own crosshair.
- **Slow starfield menu background** — The profile picker and the Team Missions menus get a slower success starfield.
- **Mipmaps and anisotropic filtering** — Ported from upstream. `Video.MipmapFilter` and `Video.AnisotropicFilter` in `pd.ini`, and an anisotropy slider in the extended video options. On by default (linear mipmaps, 4×).
- **Saves happen in the background** — Pak and ini writes are queued and flushed on a frame that can afford it, instead of a block at a time. A profile remembers its head by name and a saved multiplayer setup remembers its level by name, so neither changes when the roster does.
- **WIP Character proportions editor** — Per-character height, a per-axis matrix scale, and per-joint scale overrides, with an editor panel that names body parts from the model rather than from a hardcoded table. Type a height and it happens.
- **The no-pause mode** — A one-player multiplayer match no longer pauses, the pause option is hidden where it does not apply, and the solo menu can keep the world running behind it. The **Allow Pausing** cheat restores the old behaviour. Pause menu music drops to a fifth rather than cutting.
- **Drug blur as a dose** — Blur ramps out instead of dropping, and time spent in the pause menu accumulates a dose that decays normally once you unpause. Both curves are exposed on their own ImGui slider window, and `Blur.DoseEnabled=0` stops menu time from charging the dose.
- **Lua scripting** — A Lua 5.4 runtime, a transpiler that turns an AI list into a Lua chunk, and the `pd.*` engine API (269 functions across chrs, weapons, world, fx, player, menus and core), ported from Perfect Dark Kai. A transpiled list runs the same handlers in the same order as the bytecode path. Lua AI is on only when a script is detected (`Game.LuaAiMode=2`, the default); `0` and `1` force it, and `--lua-ai` / `--no-lua-ai` force it for one run. The sandbox is hardened: instruction budgets per call and per event handler, a failing list is quarantined for the stage rather than for good, `__gc` metatables and binary chunks are refused, and every bridge range-checks its numbers and rejects non-finite floats. See `docs/luascripting.md`. (Thank you Murk)
- **ImGui debugger overlay, now a level editor** — A single-window overlay with panels for runtime state, memory, stage and time, entity lists with search and prop jump, a frame profiler with a flame graph and top view, an audio panel, a proportions panel, and a texture pipeline inspector that can compare a source texture against what the engine rendered, overlay UV triangles on a model, and export the result as PNG. New this release:
  - a search bar that finds props, pads, stages, textures and file slots by name or number
  - ctrl-click a chr, object or pad in the world to latch it, with vim-style named registers
  - noclip and teleport, and a switch that pauses every AI list while the rest of the world keeps ticking
  - the level's collision tiles drawn as geometry, shaded or solid, and pad markers with names (`pads/<file>.names` beside a pad file names them)
  - a Saves panel that shows what is about to be written and what is in `pd.ini`
  - a skin match panel for painting a body's skin and tights masks and tagging a head, previewed live on the chr
  - windows come back where they were left, and collapsed windows minimise to a stack
- **Setups ship as patches** — A modified stage setup is shipped as an xdelta of the change, a few hundred bytes, applied after the file is inflated out of your own rom. The filetable format (PDFT v5) can carry a patch on any file, and a rom source can be a base rom plus a patch. xdelta (VCDIFF), BPS and IPS decoders come from Dab's Mod. (Thank you @DabDavis)
- **Stages for mods** — A modconfig can declare a stage by name and be given a free row, which is remembered in `pd.ini` across boots and roster changes. A `kind` key (solo, mp, both, none) and `arenaname` feed a stage registry that adds a mod's arenas to the Combat Simulator list, a `gfxscale` key carries a level's world scale onto its new row, and a mod's solo levels appear in the mission list under Mod Missions, with best times kept per reality in `pd.ini`.
- **Mod texture ownership** — Each mod's texture slots start above the slots the previous mod used, so heads and bodies from different mods no longer render each other's textures. PNG overrides, props and room textures are scoped to the mod that owns them. Mod texture slots now start at 4096 and PNG overrides reach up to 32767. GEX heads load entirely from the GEX ROM, and a mod can declare a conditional JPN ROM source.
- **Modconfig is more forgiving** — An unknown key is stepped over and counted instead of throwing away the whole file. A modconfig can name a file another mod shipped, and `source: self` plus `slugmod` give a mod its own file names that nothing else can claim.
- **`mkfiletable` builds a mod's filetable** — A PDFT writer to pair with the reader, resolving `replaces` and `byId` entries, and resolving names against a source ROM so a mod can refer to vanilla files by name.
- **`mkfiletable` needs nothing but the ROM** — `replaces:` used to resolve against a snapshotted name map that lived outside this repo, so a plain clone could not build a mod's filetable at all. The base ROM carries its own file names and a file's slot in that table is its id, so the ROM you already have to supply is enough. `--vanilla` still works and still wins when given. Verified by building every shipped mod both ways and comparing bytes.
- **`modsetcheck` looks at a whole set of mods** — Checks an assembled mod set the way a release assembler would, rather than one mod at a time.
- **The game as a file tool** — `--asset ls|stat|test|get|link|find|count|info <path>` answers from the live modloader and exits, across rom, mod, file-slot and pad drives. `--textrace` records a ring buffer of every texture load and what it uploaded.
- **`pdsym` resolves a Windows crash log** — Turns the RVAs in a `pd.crash.log` into function names and source lines against the `pd*.exe` that produced it, and refuses the pairing when the binary does not explain the report. Builds are now stamped with the commit they were built from.
- **Audio pool sizes are knobs** — The pool sizes Rare picked for a 1999 cartridge can be raised from `pd.ini`. Every default is the old number; the audio panel shows what the pools actually came up as.
- **Character assets** — Willow Dark and Ace have head entries, and character bios move to a markdown source compiled by `tools/mkchrbios`. The end credits name this release's contributors.

### Bug Fixes

- **Combat Simulator crashed on entry on Windows** — A stage whose setup carries no `INTROCMD_SPAWN` left the room list in `playerReset` uninitialised and handed it to the collision code anyway. Same defect on every platform; only the Windows build's stack residue indexed far enough out to fault. See the wiki's Spawn Pad Crash page.
- **Textures were misaligned on stock content** — A texture's uploaded dimensions and the divisor its UVs were normalised by had drifted apart: the import path sized from the tile rect while the renderer still divided by the TMEM stride, which `G_SETTILE` rounds up to a multiple of 8 bytes. Any texture whose row was not a whole number of 8-byte words was scaled slightly and slid across the surface. The uploaded size is now recorded on the texture cache entry and used as the divisor, so the two cannot disagree — including on a cache hit and for PNG override textures, whose size the tile never knew.
- **Odd-width 4-bit textures sheared into diagonals** — The I4/IA4/CI4 decoders advanced `width / 2` bytes per row, so an odd width overlapped rows by a nibble. Every row is now `ceil(width * bpp / 8)` bytes.
- **A head past slot 127 came back as a different head** — Profiles stored the head and body as 7-bit indices, so a mod head above 127 truncated on reload with no warning. Profiles now store the head by name.
- **The first profile's extended settings were reset on every load and save** — Slot 0 was treated as unregistered, so the operative carousel snapped back a few frames after each press.
- **Settings for guest players vanished from `pd.ini`** — A key whose owner had not registered it by the time of a save was dropped. Unbound keys now survive a save.
- **The camera went through Joanna's head** at the end of the Perfect Menu typing scene; the body now steps aside for the swoop. (Thank you @DabDavis)
- **Out-of-bounds reads across the collision and mod paths** — Room numbers are now checked at both ends rather than only against the upper bound, the weather's room walk is bounded by the array it is walking, and a mod-replaced segment is no longer read past its end. (Thank you @DabDavis for the latter two)
- **Runaway and malformed AI lists** — `chraiGetAilistLength` and `stageLoadAllAilistModels` compare the full opcode rather than a truncated one, the `CMD_PRINT` terminator scan is bounded, null aicmd handlers are no longer called, and an ailist that never ends is capped at 100k iterations.
- **Tagged file ids were truncated** — Mod file ids kept their full width through the lookup path, with accessors for the owner and local halves instead of hand-rolled masking.
- **Pink mod textures** — The per-mod texture slot allocator started inside the JPN ROM's vanilla texid range, so the renderer conflated a mod's slot with a vanilla one.
- **Menu volume lagged a step behind** the slider, and the menu model now buys its own memory instead of borrowing.
- **Stage pool and filetable context fixes** — Host stage pool selection, Fojo setup filetable contexts, and default-profile boot loading when the intro is skipped.

### Refactors and Cleanup

- **The all-solos switch is retired, and `use_mod_files`, `force_vanilla` and `MpArenaGroup` are removed.**
- **Everything builds out of one tree** — The stage setups (ailists included) compile in the port tree with clang and lld via `tools/mksetups`, and `cmake --build` rebuilds the patches for any setup you touched. No N64 build, no docker.
- **The basedir lives in the repo** — `pd-fojo/basedir` is what ships. The default roster is `mod_fojo` and `mod_gex_characters`.
- **Character bios moved to markdown** — `src/game/chrbios.md` is the source of truth; `tools/mkchrbios` compiles it into `training.c`, which should never be hand-edited inside the sentinels.
- **Wider fields where mods ran out of room** — `tex.texturenum` is 16 bits, the gdl texture slot is 15, and `chrdata.headnum` is an `s16`.
- **`tools/wt`** gives each working session its own sparse worktree and sweeps the lock files that otherwise wedge the repository.
- **Releases come from CI** — Builds run when triggered, and a release is filled from a chosen run's artifacts, which carry the mod files.

### Renamed / Identified Functions

- **`mpPauseIsAllowed` renamed to `pauseIsAllowed`** and moved to `cheats.c`, since it is no longer multiplayer-specific.
- **`chraiExecute` split into prepare and loop**, so Lua can take over as the driver.
- **`CK_1000` identified as the jump button**, and `CK_0400` as the third-person toggle.
- **`bond2.unk1c` renamed to `bond2.look`** — it is the look vector.
- **`g_Stages[].unk18` identified as the row's world-to-graphics scale**, now the `gfxscale` stage key.

### Known Limitations

- **Two-stance movement, reload animations and jumping are switched off** for this release.
- **No stage packs in this release.** The AIO and GoldenEye X level mods are on the bench until they can be sourced from roms you own, the way the GEX characters are.
- **A Lua script loads from one path**, `scripts/init.lua` in the working directory. Per-mod script discovery and per-mod scope are designed but not wired up, and there is no `pd.register_aicmd` yet.
- **Drop-in is overlay-only** for now; there is no in-game menu for it yet.

### Full Changelog

<https://github.com/cylonicboom/pd-friends-of-joanna/compare/friends-of-joanna-v0.3.1.1...friends-of-joanna-v0.3.9.0>

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
