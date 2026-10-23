# handoff: the crash where load player meets a name it can't draw

For a fresh session picking this up cold. Engine facts only; nothing here
is story. Everything below was read from source or measured on the Mac
clone unless it says otherwise.

## the symptom, twice

Title screen → **Load Player** crashes. It has shown up two ways, and
both are the same bug:

- **~19:40 PT 2026-10-22:** `EXC_BAD_ACCESS` at 0 in
  `langGetJpnCharPixels` (`lang.c:467`), via `textRender(text="\xc8-0:06\n")`
  ← `textRenderProjected` ← `menuitemListRender` ← … ← `titleRenderProfileSelect`.
  A byte ≥ 0x80 took the JPN path, and on a non-JPN build
  `g_JpnCacheCacheItems` is never allocated (`langinit.c:33`, gated on
  `g_Jpn`).
- **~20:10 PT, on a build at or after `0179efdde`:** `EXC_BAD_ACCESS` at
  `0x1` in `import_texture_ci4` (`gfx_pc.cpp:1095`) ← `gfx_dp_texture_rectangle`
  ← `gfx_run_dl`. A glyph whose `pixeldata` is `0x1`. The list row
  went through `textRenderProjected`'s `*text < 0x80` branch with a byte
  below `0x21`, so `chars[*text - 0x21]` indexed off the front of the font
  table. (Byte value inferred from the address, not observed.)

The row is the file manager's MP-player entry, `"<name>-<playtime>"`
(`filemgr.c:386-407`, `mpplayerfileGetOverview`, `mplayer.c:4396`).

## root cause

**The name on the pak is empty, and the reader turns an empty name into
one byte of stack.** `savebufferReadString_ext` (`savebuffer.c:477`)
started `index` at 0. When the first byte read is the null, `dst[0]` is
never written and the terminator lands at `dst[1]`, so the caller gets a
one-character string of whatever `namebuffer[20]` in
`filemgrGetSelectName` held. That is why the bad byte was `0xc8` once
and something below `0x21` the next time. The `0xc8` was never on the pak.

Measured: `fojo-net/paks/page-0000.mpk` (clone root, rewritten 03:09Z)
has one MP-player file, header at `0x1440`. Its ten name bytes are all
zero, and its 28-bit play time is 386 s, which is the `0:06` row.
`basedir/data/saves-1/paks/page-0000.mpk` is healthy: its MP-player file
is named `catherine`.

Same `s32 index = 0;` in `pd-upstream/src/game/savebuffer.c`, so this is
a vanilla PC-port bug, not something netplay introduced.

## what is done

- `0179efdde` **textRender survives a byte >= 0x80 with no JPN cache.**
  Guards `textRender` only. It does not cover the Load Player list, which
  draws through `textRenderProjected`, and it does nothing for a byte
  below `0x21`. Harmless; keep or drop.
- **`savebufferReadString_ext` starts `index` at -1.** An empty name reads
  back as `""` (or `"\n"` with `addlinebreak`). The row draws as `-0:06`.
  Not yet built with her `pdt build-port` at the time of writing.

## what is not known: who saved an empty name

`savebufferWriteString_ext` writes 8 bits per character and stops at the
first `\0` **or `\n`**. There is no charset; any byte round-trips. So a
name that is just `"\n"` saves as all zeros. Candidates, in the order I'd
check:

- **The remote seat's config slot saved to a real file.** The host
  writes `"%s\n"` of `ncl->settings.name` into
  `g_PlayerConfigsArray[playernum].base.name` for every remote client
  (`net.c:1613`). A client that sent an empty name gives exactly `"\n"`.
  If that slot still carried a real `fileguid`, a profile flush
  (`mpProfileFlushDirty`, `mplayer.c:4604`) would write it out. The
  `mpProfileMarkDirty(seat)` callers at `mplayer.c:5344/5441/5501` are
  where to look first. Nothing under `port/src/net/` marks a seat dirty
  directly.
- **A profile created or renamed with an empty name**, from the New…
  keyboard or the fojOS Profiles window (`imgui_profiles.cpp`). Check
  whether either accepts an empty name.
- **The direct-boot stand-ins.** `netDedicatedBootTick` (`netteam.c`)
  names empty seats `"Player %d\n"` and binds them with
  `iniRegisterPlayerSave(&fileguid, 1, i)`. CLI boots only, and the name
  is not empty, so this one is unlikely.

## the fix shape, once the writer is known

- If it's the remote-seat slot: never flush a seat whose client is
  remote, and clear `fileguid` on the remote configs when
  `netPlayersAllocate` binds them, so a stale guid can't point at a real
  file. Refuse an empty `settings.name` at `CLC_SETTINGS` read
  (`netmsgClcSettingsRead`) and substitute a default.
- If it's a menu: don't accept an empty name there.

## a gap in the probe

NAMEDIAG's `textDiagBadGlyph` (`game_1531a0.c:209`) only flags a NULL
`pixeldata`. A glyph read from outside the font table carries garbage,
not NULL, and gets past it. A range check of the fontchar pointer
against `g_TextDiagChars` (94 entries, `0x21`-`0x7e`) would catch both.

## how to reproduce / inspect

- The evidence is `fojo-net/paks/page-0000.mpk` as it stands. Copy it
  before touching anything. Never delete her saves; move them.
- Rebuild, Title → Load Player. With the fix, the row reads `-0:06`
  instead of crashing.
- Pak layout as found: PD pak files sit on 16-byte boundaries from
  `0x500`. Byte 8 of the 16-byte header is the file type (`0x20` = MP
  player). Bytes 4-7 `ffffffff` with a `2b2b…` body mark a dead file.
  The 10-byte name and the 28-bit play time follow at header+16.

## environment for a session

- Container clone `/home/claude/net/fojo-net` (branch `fojo-net`, commits
  authored `Catherine Reprobate <catherine.reprobate@pm.me>`, no tool
  trailers, never push). Syntax checks: `/home/claude/net/cc1.sh <file>`
  (C), `/home/claude/net/cxx1.sh` (C++, run from
  `/home/claude/net/build-base`). Full link: `ninja pd` in `build-base`.
  The container is per-session; a new session may not have it.
- Mac clone `~/src/pd/friends-of-joanna/fojo-net` (device_bash `$HOME/mnt/fojo-net`),
  build `$HOME/fn-build` (gcc arm64, `-g`). Helpers in `$HOME/nt/`:
  `am.sh <patches>` (git am + lock sweep + build), `wip.sh` (reset
  changed tracked files, apply `_inbox/wip.diff`, build). Patches go over
  with `device_commit_files` into `_inbox/NNNN.patch`.
  **Gotcha:** a `device_commit_files` to a path written earlier in the
  session can land stale content. Write to a fresh name and `cat` it
  into place, then `md5sum` both sides.
- The mount denies unlink: `tar` can't overwrite, git leaves its locks
  (sweep them into `.git/_stale/`), `rm` fails. She clears `.git/_stale/`
  and `_inbox/` from Terminal.
- Harness: `tools/nettest/nettest run <scenario> --build $HOME/fn-build
  --basedir $HOME/mnt/pd-fojo-collab/pd-fojo-basedir/data`; see
  `tools/nettest/README.md`. The file-manager crash is outside what it
  exercises (CLI boots skip the agent file select).

## board

Fojo Situation Board (Artifact "Fojo Situation Board"): `c-fojo-net`
#471, `c-net-lobby-window` #477, `c-nettest` #476. Add the resolution
there when it lands.
