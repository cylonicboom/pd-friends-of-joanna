# handoff: the crash where load player meets a name it can't draw

For a fresh session picking this up cold. Engine facts only; nothing here
is story. Everything below was read from source or measured on the Mac
clone unless it says otherwise.

## the symptom (hers, 2026-10-22 ~19:40 PT)

Title screen → **Load Player** → `EXC_BAD_ACCESS` at address 0:

```
langGetJpnCharPixels(codepoint=2)            lang.c:467   g_JpnCacheCacheItems[i] with the table NULL
textRender(text="\xc8-0:06\n", ...)          game_1531a0.c:2523
textRenderProjected ← menuitemListRender ← dialogRender ← menuRender
← titleRenderProfileSelect ← titleRender ← lvRender ← mainTick
```

The row being drawn is the file manager's MP-player entry,
`"<name>-<playtime>"` (`filemgr.c:380-405`, `mpplayerfileGetOverview`):
a profile whose **name decodes to the single byte 0xc8** and whose play
time is 0:06. `textRender`'s non-N64 path treats any byte ≥ 0x80 as the
first of a JPN pair (`game_1531a0.c:2509`), and on a non-JPN build
`g_JpnCacheCacheItems` is never allocated (`langinit.c:33`, gated on
`g_Jpn`). So: a corrupt or unencodable profile name on the pak, not
netplay code as such — but the pak was written during the netplay
session, so something in that session produced the name.

## what is already done

- `0179efdde` **textRender survives a byte >= 0x80 with no JPN cache** —
  draws `?`, skips the pair, logs once:
  `textRender: byte 0xNN in "<first 16 bytes>" with no JPN cache - skipped`.
  The file manager no longer crashes; the bad entry shows as `?-0:06`.
  Built on the Mac clone (gcc, `$HOME/fn-build`); her `pdt build-port`
  (clang) not yet rerun on it.

## what is not known

1. **Which file carries the name.** Two fresh paks exist, both from that
   evening:
   `fojo-net/basedir/data/saves-1/paks/page-0000.mpk` (19:31) and
   `fojo-net/paks/page-0000.mpk` (19:37, clone root — she ran
   `basedir/pd.arm64` from the clone). `saves-1/pd.ini` lists one real
   profile section, `[MpPlayer.dda-1c]`, plus the four seat defaults; the
   display name itself lives in the pak header (15 bytes, `savebuffer`
   packed), not in pd.ini.
2. **Who wrote it.** Candidates, in the order I'd check:
   - **The remote seat's config slot saved to a real file.** The host
     writes the wire name into `g_PlayerConfigsArray[playernum].base.name`
     for every remote client (`netmsg.c` ~1611, `"%s\n"` of
     `ncl->settings.name`) and sets `OPTION_ASKEDSAVEPLAYER`. If that slot
     still carried a `fileguid` of a real pak file from an earlier seating,
     a profile flush (`mpProfilesFlushDirty`, `mplayer.c:4610`, dirty via
     `mpProfileMarkDirty`) would write the remote's name over that file.
     Grep whether anything marks a remote seat dirty; nothing in
     `port/src/net/` does today, so this needs a stage-end / menu path.
   - **A name the 6-bit packer can't hold.** `savebufferWriteString` has a
     fixed charset; a name typed in the fojOS Profiles window with a char
     outside it would round-trip as garbage. Check `imgui_profiles.cpp`'s
     create/rename against the charset, and whether she created or renamed
     a profile that evening.
   - **The direct-boot stand-ins.** `netDedicatedBootTick` (`netteam.c`)
     names empty seats `"Player %d\n"` and binds them with
     `iniRegisterPlayerSave(&fileguid, 1, i)` — CLI boots only, not her
     flow, but a stand-in guid colliding with a real file would do this.
   - **Two instances on one pak.** If both game instances wrote the same
     `page-0000.mpk` the header could be torn. The two paks are in
     different dirs, which argues against it, but confirm which instance
     used which (`saves-1` looks like a second-instance save dir).

## how to reproduce / inspect

- Rebuild (`pdt build-port` at `0179efdde` or later), Load Player, read
  `pd.log` for the `textRender:` line — it prints the raw bytes of the row.
- Decode the pak header directly: `mpplayerfileGetOverview(file->name, …)`
  reads 15 bytes with `savebufferReadString` (`mplayer.c:4396`); a tiny
  harness around `savebuffer*` on the 32 KB `.mpk` would list every
  MP-player file's name bytes without the game.
- Make a copy of both paks before anything: they are the evidence. Never
  delete her saves; move them.

## the fix shape, once the writer is known

- If it's the remote-seat slot: never flush a seat whose `client` is
  remote (`g_Vars.players[i]->isremote` / `g_NetLocalClient->playernum !=
  i`), and clear `fileguid` on the remote configs when `netPlayersAllocate`
  binds them, so a stale guid can't point at a real file.
- If it's the packer: validate names at the Profiles window and at
  `CLC_SETTINGS` read (`netmsgClcSettingsRead`, `netmsg.c` ~918) against
  the savebuffer charset; replace what won't encode.
- Either way the `?` guard stays.

## environment for a session

- Container clone `/home/claude/net/fojo-net` (branch `fojo-net`, commits
  authored `Catherine Reprobate <catherine.reprobate@pm.me>`, no tool
  trailers, never push). Syntax checks: `/home/claude/net/cc1.sh <file>`
  (C), `/home/claude/net/cxx1.sh` (C++, run from
  `/home/claude/net/build-base`). Full link: `ninja pd` in `build-base`.
- Mac clone `~/src/pd/friends-of-joanna/fojo-net` (device_bash `$HOME/mnt/fojo-net`),
  build `$HOME/fn-build` (gcc arm64, `-g`). Helpers in `$HOME/nt/`:
  `am.sh <patches>` (git am + lock sweep + build), `wip.sh` (reset
  changed tracked files, apply `_inbox/wip.diff`, build). Patches go over
  with `device_commit_files` into `_inbox/NNNN.patch` (last used 0043).
  **Gotcha:** a `device_commit_files` to a path written earlier in the
  session can land stale content — write to a fresh name and `cat` it
  into place, then `md5sum` both sides.
- The mount denies unlink: `tar` can't overwrite, `git am` leaves
  `.git/REBASE_HEAD.lock` (swept into `.git/_stale/`), `rm` fails. She
  clears `.git/_stale/` and `_inbox/` from Terminal.
- Harness: `tools/nettest/nettest run <scenario> --build $HOME/fn-build
  --basedir $HOME/mnt/pd-fojo-collab/pd-fojo-basedir/data`; see
  `tools/nettest/README.md`. The file-manager crash is outside what it
  exercises (CLI boots skip the agent file select).

## board

Fojo Situation Board (Artifact "Fojo Situation Board"): `c-fojo-net`
#471, `c-net-lobby-window` #477, `c-nettest` #476. Add the resolution
there when it lands.
