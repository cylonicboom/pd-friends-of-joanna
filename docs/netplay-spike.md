# Netplay for Team Missions — the spike where everyone's in a different apartment

Getting a fojo Team Mission (operative + co-operatives + counter-operatives, up
to `MAX_PLAYERS` humans) to run with each human on their own machine, by taking
the Kai fork's netplay layer and putting fojo's player model on top of it — and
making the whole thing testable without anyone holding a controller.

Branch: `fojo-net` in this repo (the port fork). The umbrella's `wt/fojo-net`
carried the first cut; this is where the work lives now.

Reference checkout: `pd-kai/` in the `pd-fojo-collab` umbrella = `cylonicboom/perfect-dark`
@ `kai` (`be46717`), committed there as plain files. Line anchors below are into it.
Baseline for three-way thinking: `pd-upstream/` @ `32a1cb9f2`
(`perfect-dark-pc-port/perfect_dark`, 2026-08-13).

This is a spike: the deliverable is knowledge and a cut list, not code. Every
claim is marked **measured** (a command was run) or **by reading** (inferred
from source and docs).

---

## 1. What Kai has (measured)

`pd-kai/port/src/net/` is 23.5k lines across `net.c` (8301), `netmsg.c` (6054),
`netmenu.c` (2984), `netmaster.c`, `netupnp.c`, `netbuf.c`, `netkillcam.c`,
`netprop.c`, `playlist.c`, plus `port/src/headless.c`, `det.c` (758),
`dedicated_stubs.c`, `spectator.c`, `possess.c`. Transport is bundled ENet
(`port/external/enet.c`). The long-form reference is `pd-kai/port/src/net/CLAUDE.md`
(653 lines) and the `docs/PORT_*.md` family; this section is the digest that
matters for fojo.

**Architecture.** Host-authoritative. One local combatant per machine. Remote
players are `PROPTYPE_PLAYER` chrs driven by `SVC_PLAYER_MOVE`; NPCs and sims on a
client run no AI (`botTick` and chr AI gated to `g_NetMode != NETMODE_CLIENT`),
are position/anim/pose-driven by `SVC_PROP_MOVE` with a chr-state block, and
have `actiontype` force-set to `ACT_STAND` client-side (applying the host's
actiontype crashed on uninitialised `act_*` union data). Props carry a `syncid`
(1-based index into `g_Vars.props`, allocated at stage start by
`netSyncIdsAllocate`) and the wire references props only by it. RNG seeds are
host-chosen and applied on `SVC_STAGE_START`, which is what makes `setup.c`'s
spawn-pad / bot allocation identical on both ends. CSP with three error tiers,
8-snapshot entity interpolation, broad-phase lag compensation. Per-client
relevancy cull for chr state. Protocol version is `NET_PROTOCOL_VER` in
`port/include/net/net.h`; mismatch is `DISCONNECT_VERSION`.

**Campaign co-op already exists on it.** `netCoopEnterStage(stagenum, difficulty,
numplayers)` (`net.c:1331`) is the one entry point; the host reaches it via the
`/coop` console command (`net.c:6126`) or the Network → Cooperative menu
(`netmenu.c:2857`), the client via `netmsgSvcStageStartRead`'s `NETSTAGEMODE_COOP`
branch (`netmsg.c:1435`). It sets `iscoop=1, isanti=0, bondplayernum=0,
coopplayernum=1, antiplayernum=-1` and `setNumPlayers(NET_COOP_MAX_SLOTS)` (= 4,
`net.h:808`, `= MAX_LOCAL_PLAYERS`). Unbound slots park dormant
(`player.isdormant`) and are claimed on join (`SVC_COOP_CLAIM 0x54`, drop-in /
rejoin, `docs/PORT_COOP_DROPIN.md`). Objectives are host-authoritative with a
client-reported path for things the host cannot witness (`CLC_OBJECTIVE_DONE`),
pickups are host-granted (`CLC_PICKUP_REQUEST`), stage completion is a handshake
(`CLC_STAGE_COMPLETE` → `SVC_STAGE_END`), stage flags and alarm are mirrored
(`SVC_STAGE_FLAGS`, `SVC_ALARM`), scripted `CHR_BOND`/`CHR_COOP` resolution is
transposed per machine (`chrResolveCoopSlotPlayer`, chraction.c — see §4.3).

**No counter-op over the wire.** `docs/PORT_COOP_8P.md`: "In net co-op there is
**no counter-op** (`antiplayernum < 0`), so every player is a co-op player", and
the whole N-player widening is the `PLAYER_IS_NOT_ANTI(plr)` predicate swapped
in at the shared-logic sites. Kai never had to make an anti player work on a
client. For fojo that is the one genuinely new problem (§4.2).

**Kai's own player-count shape.** `constants.h:41-49`: `MAX_LOCAL_PLAYERS 4`,
`MAX_PLAYERS 16` on the port (4 on N64). Local splitscreen, pads, binds and file
formats stay at four; `MAX_PLAYERS` is "humans in the room". Note this is the
same split Dab took independently (12, `coop.c`) — board `d-coop-player-model`
(#387). Fojo is `MAX_PLAYERS 4` only (`pd-fojo/src/include/constants.h:26`).

## 2. What Kai has for automation (measured)

This is the part worth the most to her "as automated as possible" ask, and it
all exists already:

| piece | what it is | where |
|---|---|---|
| `DEDICATED_SERVER=ON` | cmake option; headless build with no SDL / GL / audio / input. Hosts, or joins. | `pd-kai/CMakeLists.txt:37`, `port/src/headless.c`, `dedicated_stubs.c` |
| `--dedicated` | boot straight into hosting, no agent file, no menus (`netDedicatedBootTick`) | `netmenu.c` |
| `--headless-client <addr>` | the same headless runtime but **joins** as a combatant. Tick path runs, `lvRender` is skipped. Neutral input; spawns at round boundaries only. | `docs/PORT_NET_SOAK.md` "Headless client" |
| `/audit` + `netPropAuditTick` | in-engine prop-sync invariant auditor, once a second, both roles, emits a parseable `audit:` line to the diag CSV (`--netdiag <path>`) | `netprop.c` |
| `tools/netsoak.py` | offline PASS/FAIL verdict over one or more diag CSVs, including **server↔client manifest parity**. Exit 0/1/2. Self-tested by `tools/test_netsoak.py` on synthetic logs. | `pd-kai/tools/` |
| `tools/soak/` | `run_server.sh`, `run_client.sh`, `parity_latest.sh`, `soak_oneclick.bat`, `playlist_soak.txt`, a `save/` dir whose `pd.ini` sets `Game.MemorySize=256` | `pd-kai/tools/soak/` |
| `det.c` | determinism harness: canonical decomposable **state hash**, fixed-step pin (netplay forces 60 Hz fixed step), **input record/replay** through the joy raw-ring seam with per-tick hash compare | `port/src/det.c`, `port/include/det.h` |
| `/lag <ms>`, `/loss <N>` | artificial latency and packet loss, console-only | `net.c` `netConsoleCommand` |
| `--netdiag`, `Net.Debug.LogPath/LogRate` | the diagnostic CSV: `tick,realtime_s,event,k=v …` | `net.c` |

Kai's status line on the soak (`PORT_NET_SOAK.md`): "RUNTIME-PROVEN on Windows
(2026-06-10): repeated unattended two-process soaks (headless server + headless
client, one box) ran matches, rotated the playlist, and produced `OVERALL: PASS`".
That is Combat Sim, 8 bots, one headless client. **Nobody has soaked campaign
co-op headless** — the co-op launch is console/menu only (§3, gap C1).

## 3. Gaps between Kai's harness and a Team Missions soak (measured unless noted)

- **C1. No command-line way to start a co-op stage.** `main.c` flags are
  `--boot-stage --dedicated --dedicated-windowed --headless-client --mpprofile
  --no-crash-handler --no-sound --profile --skip-intro`; the net flags (`--host
  --connect --port --maxclients --playlist --netdiag --svcrate …`) live in
  `netInit`. Co-op starts only from `/coop` (console) or the menu, and headless
  builds have no console. The playlist (`playlist.c`) drives Combat Sim rotations
  only. A Team Missions soak needs a `--team <stage> <difficulty> --roles …`
  (or a playlist `[team]` block) that the dedicated host consumes on boot.
- **C2. The dedicated server is "never a client / pawnless"** (`CMakeLists.txt:37`).
  In co-op the host normally plays. Whether `netCoopEnterStage` works with a
  pawnless dedicated host (host spectator sentinel `NET_PLAYERNUM_SPECTATOR 0xFE`)
  is **by reading** unknown — `PORT_HEADLESS_BLIND_SERVER.md` (607 lines) never
  mentions co-op. For the soak the shape that avoids the question: dedicated
  host + N headless clients, the host holding no role. For real play the host
  is a player. Both must work.
- **C3. Headless clients take neutral input**, so a mission never progresses
  past the first room. Options, in order of cost: (a) accept it — the soak's job
  is sync invariants, not finishing missions; (b) `det.c` input replay — record a
  real run's joy ring once, replay it into the headless pawn; (c) a Lua driver
  through fojo's `pd.*` layer (does the API expose input injection? **not
  checked**). (a) first, (b) is the lever that makes "the mission completes
  headless" possible.
- **C4. The auditor checks prop sync.** Team Missions add state Kai's co-op
  does not have: per-slot roles, fojo's per-player lives/tuning, anti
  respawn-by-takeover. The verdict tool wants a second line class —
  `team:` with roles-as-seated, `COOPPLAYERCOUNT()`, anti count, objective
  statuses — emitted host and client, compared for parity like `manifest`.
  Fojo's `playermgrAllocatePlayers` already logs roles and the four pointers
  on entry/exit (`playermgr.c:168,236`) — that log line is halfway to it.
- **C5. The det state hash is unused in the soak.** `det.c` hashes positions /
  angles / health / RNG / counts in deterministic entity order. Emitting it as a
  per-second diag line on both roles gives a divergence detector for *players*,
  not just props. Clients run no AI so full-state equality is not expected;
  a player-only sub-hash is the useful one. **By reading** — needs a look at
  which fields `detHash` folds.
- **C6. Platform.** Kai soaks on Windows/MSYS2; the VPS is Debian. Fojo ships
  arm64 clang on her Mac. The headless build has **no SDL**, which means it
  also builds and runs in the Linux VM with no Xvfb (see `linux_build_vm`
  memory: the windowed binary needs a real GL context; the dedicated one does
  not). So the soak can run as a cheap gate in the VM (GCC, aarch64) and as
  the real verdict through `tools/buildq` on the Mac. `buildq` today runs
  `cmake --build build` with no option plumbing (`tools/buildq/buildq:105`) —
  a second configure dir for `DEDICATED_SERVER=ON` is a small chore.

## 4. The collision: two N-player co-op models

### 4.1 Shapes

| | Kai net co-op | fojo Team Missions |
|---|---|---|
| who is a co-op player | every player; `PLAYER_IS_NOT_ANTI(plr)` | `g_Vars.playerroles[slot]` ∈ NONE/COOP/ANTI/BOND; `coopplayers[]`, `antiplayers[]` sparse by slot (`fojo-coop-players`) |
| counter-op | none (`antiplayernum = -1`) | any number of slots, mixed with co-op |
| entry | `netCoopEnterStage` → `iscoop=1`, `coopplayernum=1`, `setNumPlayers(4)` | `menuhandlerAcceptMission` → `playermgrResetTeamPlayers` → `isteam` → `playermgrAllocatePlayers(-1)` (`port/src/pdmain.c:1210-1218`) |
| `g_Vars.coop` | the slot-1 pointer, kept for splitscreen | "the *current* coop", copied from the arrays by `setCurrentCoopNum` |
| local player slot | **swapped to slot 0** on clients (`netPlayersAllocate`), with `chrResolveCoopSlotPlayer` transposing scripted `CHR_BOND`/`CHR_COOP` back | local players are `players[0..LOCALPLAYERCOUNT)`, roles sparse by slot |
| ceiling | `MAX_LOCAL_PLAYERS 4` / `MAX_PLAYERS 16` / `NET_COOP_MAX_SLOTS 4` | `MAX_PLAYERS 4` |
| widened sites | `objectives.c`, `player.c` revive + all-dead, `chr.c` autoaim, `bondgun.c`, `endscreen.c` `COOP_ALLDEAD()`, `mplayer.c` `MP_COOP_ALLDEAD()`, `chraicommands.c` `aiToggleP1P2` / `aiChrSetP1P2` / `aiClearInventory` (table in `PORT_COOP_8P.md` "Bucket B") | the same sites, iterated over `coopplayers[]` / `antiplayers[]`; 98 `isteam` sites across 17 files (measured) |

**Reading:** fojo's model is strictly more general (roles per slot, anti
allowed). Kai's `PLAYER_IS_NOT_ANTI` widenings are the *degenerate case* of
fojo's arrays (all slots COOP, no ANTI). So at every Bucket-B site the merge
resolution is **keep fojo's**, and the thing to port from Kai is the transport
and the host-authority plumbing, not its co-op model. `netCoopEnterStage`
becomes a fojo `netTeamEnterStage(stagenum, difficulty, roles[MAX_PLAYERS])`
that sets `isteam` and `playerroles[]` per wire slot and lets
`playermgrAllocatePlayers(-1)` do what it already does. `NET_COOP_MAX_SLOTS`
= fojo's `MAX_PLAYERS` = 4, so the dormant-slot layout is unchanged.

### 4.2 Counter-op over the wire — the new problem (by reading)

Vanilla counter-op is a player prop wearing a guard body; on death it takes
over another guard (the anti respawn branch in `player.c`, `antiplayernum`
sites at `player.c:1628,3404,4952,6105,6765,7064`). Over Kai's wire:

- A live anti is a `PROPTYPE_PLAYER` and rides `SVC_PLAYER_MOVE` like any
  player. The guards it fights alongside are host-driven props on every client.
  Nothing new there.
- The **takeover** is the seam: host picks a guard chr, that chr stops being an
  NPC and a player spawns in its place. On the client that guard is a synced
  chr (`ACT_STAND`, wire-driven). The host must reap it (`SVC_PROP_RECONCILE`
  already lists chr syncids in co-op) and force-snap the anti player's pawn
  into its position/rooms (`UCMD_FL_FORCEMASK` path carries wire rooms). That
  is a composition of two existing mechanisms, not a new message — but it has
  never been exercised.
- Anti-side rules — team damage, who the guards shoot, objective visibility,
  `p1p2` target cycling — fojo already does per slot. On the client those are
  host decisions mirrored through existing state, except anything a client
  *renders* based on role (HUD, team colours) which reads `playerroles[]`
  locally and so needs the roles manifest (§4.4).

Smallest proof: a 2-machine session, one co-op one anti, anti dies once. If
the takeover lands, mixed Team Missions are plumbing; if not, that is the
card.

### 4.3 The slot-0 swap vs sparse role arrays (by reading)

Kai's clients move the local player to `g_Vars.players[0]` and keep a
self-inverse wire-slot↔local-slot map (`netCoopRemapWirePlayernum`,
`s_netSlot0SwapOccupant`) so that scripted `CHR_BOND`/`CHR_COOP` land on the
same physical player everywhere. Fojo's role arrays are **sparse by slot** —
`playerroles[2] = ANTI` means wire slot 2 is the anti on every machine. Swapping
the local player to slot 0 would relocate roles per machine and every
`coopplayers[slot]` lookup would disagree across the wire.

Kai itself has moved off the swap for drop-in: "NO mid-mission slot-0 swap — the
claimant stays at wire slot N (lv.c render rollback keys on the local order
now)" (`PORT_COOP_DROPIN.md`). So the render path already copes with a local
player at slot N. **Proposed:** fojo's net clients never swap; the local player
sits at its wire slot, `LOCALPLAYERCOUNT()` is 1, and `chrResolveCoopSlotPlayer`'s
transposition is not ported (the thing it corrects for is gone). Fojo's
"`players[]` is dense from 0" invariant (`fojo-coop-players`) is the casualty —
with one local player at wire slot 2, `players[0]` and `[1]` are dormant remote
pawns, not NULL, so `PLAYERCOUNT()` still counts 4. **Hers to rule** —
`d-net-slot-identity` below. The alternative (keep Kai's swap, transpose
`playerroles[]` on read) touches every one of the 23 `playerroles[` sites.

### 4.4 The wire needs a roles manifest

Kai's `SVC_STAGE_START` co-op branch carries `{id, playernum}` per player and
the lives/body-type blocks. Fojo adds `role` per wire slot (and later its
per-player tuning). Kai's own follow-up note — "remote partner appearance at
N>2: the co-op manifest carries only `{id, playernum}`, not per-client
`settings`" — is the same hole; fix once as a per-slot `{id, playernum, role,
head, body, name}` record. New field ⇒ `NET_PROTOCOL_VER` bump; fojo starts its
own protocol lineage anyway (its builds will never auth to a Kai server —
`CLC_AUTH` already compares ROM filename and mod dir).

## 5. Intake size (measured, with a caveat)

Files outside `port/src/net/` that reference `g_NetMode` / `NETMODE_` /
`g_NetLocalClient` / `syncid` in Kai: **58**. Diff line counts (`diff | grep
-c '^[<>]'`) of Kai and fojo each against `pd-upstream` @ `32a1cb9f2`. Caveat:
Kai forked earlier than that upstream, so "kai_vs_up" includes upstream drift
Kai never took, and chraction.c's Kai number is mostly the Lua/chaos layer, not
netplay. Treat as a heat map, not a merge estimate.

| file | kai net refs | kai vs upstream | fojo vs upstream | kai vs fojo |
|---|---|---|---|---|
| `src/game/chraction.c` | 93 | 6705 | 1585 | 7866 |
| `src/game/propobj.c` | 63 | 1659 | 985 | 1912 |
| `src/game/prop.c` | 45 | 1014 | 252 | 1166 |
| `src/game/lv.c` | 34 | 1205 | 334 | 1301 |
| `src/game/player.c` | 33 | 1495 | 2613 | 3786 |
| `src/game/mplayer/scenarios.c` | 14 | 654 | 0 (fojo untouched — Kai's hunks apply clean) | 654 |
| `src/game/chr.c` | 13 | 745 | 636 | 979 |
| `src/game/mplayer/mplayer.c` | 10 | 1340 | 8133 | 9335 |
| `src/game/menutick.c` | 9 | 333 | 370 | 683 |
| `src/game/bondmove.c` | 9 | 878 | 823 | 1317 |
| `src/game/bondgun.c` | 8 | 1871 | 1119 | 2318 |
| `src/game/objectives.c` | 7 | 187 | 23 | 188 |
| `src/game/bot.c` | 7 | 467 | 243 | 702 |
| `port/src/pdmain.c` | 6 | 848 | 1874 | 2620 |
| `src/game/playermgr.c` | 4 | 114 | 502 | 594 |
| `src/include/types.h` | 3 | 705 | 10124 | 10445 |
| `src/game/mplayer/setup.c` | 3 | 2165 | 12273 | 14380 |
| `src/game/mainmenu.c` | 1 | 1169 | 11286 | 12239 |

Six of the 58 are untouched by fojo (`scenarios.c`, `padhalllv.c`, `proptick.c`,
`wallhitreset.c`, `varsreset.c`, `include/game/lv.h`) and take Kai's hunks as-is.
Both trees have rewritten `player.c`, `mplayer.c`, `setup.c`, `mainmenu.c`,
`pdmain.c`, `types.h` heavily. **The intake is a three-way merge against
upstream, hunk by hunk, exactly like the Lua intake was** (`kai-integration-plan.md`,
"Intake is a three-way merge, not a pull"). The `port/src/net/` directory
itself and `enet` drop in clean (no fojo counterpart). `det.c`, `headless.c`,
`dedicated_stubs.c`, `spectator.c` are new files (fojo has its own
`c-spectator` from Dab's — a name collision to resolve, not a merge).

Fojo already carries Kai's `chrai.c` split (`chraiPrepare`/`chraiRunLoop`,
landed `e8c3e9462`) and the Lua core, so the chraction/chrai side is partly
walked.

## 6. The cut list — what comes over, what stays

Shape only; which of these she wants is hers (`d-net-cut-list`).

**Comes over (the spine):** `net.c`, `netmsg.c`, `netbuf.c`, `netprop.c`,
`enet`, `headless.c`, `dedicated_stubs.c`, the `DEDICATED_SERVER` cmake path,
`det.c`, the game-loop hooks in `pdsched.c` (`netStartFrame`/`netEndFrame`,
120 fps cap), the `#ifndef PLATFORM_N64` engine sites for prop/chr/player
sync, co-op drop-in, objectives/pickup/stage-complete handshakes, the diag log,
`/audit`, `tools/netsoak.py` + `test_netsoak.py` + `tools/soak/`.

**Probably not, or later:** `netmaster.c` (public master server, heartbeats to
a baked-in IP `204.152.192.106` by default — must at minimum default
`Net.Master.Advertise=0`), `netupnp.c`, "Host Online Game" (the master spawns
you a VPS instance), `netkillcam.c`, host spectator panels (`spectator.c`),
`possess.c` (Lua toy), server weapon bans / `playlist.c` (Combat Sim), the
`/wireframe /octree /gyro /tonal /padtest` console commands that ride along in
`netConsoleCommand`. Kai's `netmenu.c` is std-menu; her stated preference is
imgui/fojOS for new UI, so the lobby is a fojOS window, not a port of
`netmenu.c`.

**Fojo-side additions:** `netTeamEnterStage`, the roles manifest, the
`--team`/`--roles` boot path, the `team:` audit line, the anti takeover
composition (§4.2).

## 7. The chunks, in dependency order

Named per `catherine-episode-naming`; no phase numbers.

- **the chore where the dedicated server builds on a mac** — `DEDICATED_SERVER`
  cmake option in `pd-fojo`, a `buildq` lane that configures it, and a VM GCC
  build to prove it needs no GL. Nothing net yet; it is the harness's floor.
  *needs:* nothing.
- **the feature where kai's wire comes to visit** — the spine intake (§6), as
  a three-way merge against `pd-upstream`, Combat Sim only, no Team Missions.
  Done when a dedicated host + one headless client pass Kai's own soak
  (`netsoak.py` exit 0) on her Mac. *needs:* the chore above.
- **the feature where a team mission has a wire manifest** — `netTeamEnterStage`,
  roles per wire slot in `SVC_STAGE_START`, `isteam` on both ends, Bucket-B
  sites kept fojo's. Done when 2 machines seat BOND + COOP from the manifest
  and `playermgrAllocatePlayers`'s log lines match on both. *needs:* the wire,
  and `d-net-slot-identity` ruled.
- **the chore where the host starts a team mission from the command line** —
  `--team <stage> <diff> --roles b,c,c,a` for the dedicated host (C1); a
  headless client gets `--role` or takes the slot the host hands it. *needs:*
  the manifest.
- **the spike where the counter-op dies and comes back as someone else** — the
  §4.2 experiment, two machines. *needs:* the manifest. Its outcome decides
  whether mixed roles are a slice or a feature.
- **the feature where the soak runs itself** — `tools/netsoak/run.sh <minutes>
  <stage> <roles>`: dedicated host + N headless clients on one box, timed,
  `--netdiag` each, `netsoak.py` + the `team:` parity check, one exit code;
  the VM as the cheap gate, buildq as the real one; wired into the 03:00
  build+smoke brief rather than a tenth scheduled task (the rig's
  `device_absent` history says fewer tasks, not more). Review step per her
  automation rule: the run writes the verdict to the board card and the
  check-in page; nothing is "passing" until she has read it. *needs:* the
  command-line start.
- **the feature where the headless client takes direction** — the headless
  client gets a *pad source*, three rungs. (a) neutral: today's zeroed
  `OSContPad` (`dedicated_stubs.c:22`). (b) replay: `--detplay <file>`;
  `det.c` already injects a frame of pad state through `joySetRawSample` /
  `joySetCurWindow` (`joy.c:736`) in `detFrameBegin`, so this is a CLI flag on
  top of the console-only `/detrec` / `/detplay` — deterministic, not adaptive.
  (c) scripted: a Lua pad driver, a per-frame hook that writes an `OSContPad`
  into the ring; Kai's `pd.*` has no input injection (measured: only
  `input_source`, `input_delay` in `luaai_api.c`), so it is a fojo binding.
  (c) is what makes a mission *complete* headless. Caveat from Kai's own soak
  doc: a local pawn on a headless build is unproven for tick code that expects
  `lvRender` to have primed state — proven for Combat Sim, not for a stage
  with cutscenes and objective HUD. *needs:* the wire for (a), the
  command-line start for (b) and (c).

## 8. Decisions that are hers

- **`d-net-slot-identity`** — no slot-0 swap, local player at its wire slot,
  `players[]` dense invariant relaxed (§4.3)? Or keep the swap and transpose
  roles on read?
- **`d-net-cut-list`** — §6: master server / UPnP / killcam / host spectator /
  playlist in or out; lobby in fojOS or `netmenu.c` ported.
- **`d-coop-player-model`** (#387, already open) — this spike adds a third
  shape to the two already there: Kai also split `MAX_LOCAL_PLAYERS 4` from
  `MAX_PLAYERS 16`, the same split Dab took. Two forks independently landing on
  the local/room split is evidence for option (b) on that card.
- **host plays or host serves** — for play, the host is a player; for the soak,
  a pawnless dedicated host (C2). Both, or only one? Decides whether C2's
  unknown has to be answered.

## 9. Perforation

- **Generic:** everything in this document. The transport, the manifest shape,
  the dedicated build, the harness, the anti-takeover composition, the
  `team:` audit line — all of it is engine, none of it knows which roster is
  seated or what the slots are called.
- **Fanfic-coupled:** nothing yet. The first fojo-coupled surface will be
  whatever the lobby *shows* for a slot — operative model lists, names — and
  that lives in the fojOS lobby window, not in `port/src/net/`. Keep it there.

## Measured this session

- `pd-kai` line counts, `main.c` flag list, `netCoopEnterStage` body, the
  `/coop` handler, `constants.h` ceilings, `CMakeLists.txt:37`: read from the
  tree 2026-10-20.
- The 58-file reference list and all diff counts: `grep -rl` / `diff | grep -c`
  against `pd-upstream` @ `32a1cb9f2`, run 2026-10-20.
- Fojo `isteam` sites: 98 across 17 files; `playerroles[` sites: 23.
- Nothing was built. Nothing was run. No Kai netplay binary exists on this
  side yet.
