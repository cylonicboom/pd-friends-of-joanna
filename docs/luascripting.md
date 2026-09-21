# Lua level scripting

How a stage's AI lists can be driven, replaced or wrapped from Lua in the
PC port, what a script is allowed to touch, and what has to be true before
the feature is considered working. Read [ailists.md](ailists.md) first: Lua
does not replace the ailist model, it sits on top of it.

> Status, 2026-09-21: the runtime, transpiler, dispatch, containment and the
> full `pd.*` API are on `main`. **Script discovery is not.** A script still
> loads from one hardcoded path relative to the working directory, and the
> per-mod scope described in [Scope](#scope-what-a-script-may-override) is
> ruled but not enforced. Each section says which side of that line it is on.

## What it is, and what it is not

Every ailist still runs through the original C command handlers
(`g_CommandPointers`). With Lua on, the bytecode loop in `chraiExecuteBytecode`
is replaced as the *driver* by `luaaiExecute` (`src/game/luaai.c`): each list
is transpiled once into a Lua chunk that dispatches on the program counter and
calls `ctx:exec(off)` for every command. Same opcodes, same handlers, same
order. A transpiled list is behaviourally identical to the bytecode path.

Two consequences an author needs before writing anything:

- **Registering an override swaps a whole list, not a command.** The unit of
  scripting is the ailist id. There is no `pd.register_aicmd` yet — a mod
  cannot add a new AI verb from Lua. The length registry it needs
  (`chraiSetModCommandLength`, `src/game/chraicmdlen.c`) exists and is empty.
- **The `pd.*` table is a general engine API** — 269 functions across chrs,
  weapons, world, fx, player, menus and core. Level scripting is the smallest
  use of it. This document covers only the ailist side; the per-group smoke
  scripts under `tools/luaai_test/smoke/` are the working inventory of the
  rest.

## Turning it on

Decided once at startup in `port/src/main.c`. First match wins:

| Source | Effect | Persists |
|---|---|---|
| `--no-lua-ai` | off this run | no |
| `--lua-ai` | on this run | no |
| `Game.LuaAiMode = 0` / `1` in `pd.ini` | forced off / forced on | yes |
| `Game.LuaAiMode = 2` (default) | on iff a script is detected | yes |

"Detected" is `luaaiScriptDetected()`: today it means `scripts/init.lua`
exists in the working directory. Startup logs one line saying which rule
fired (`lua ai on: script detected`, `lua ai off: no script`,
`lua ai forced on: --lua-ai`). Read that line before debugging anything else.

## Where a script loads from

### Today (built)

```
<working directory>/scripts/init.lua
```

`LUAAI_INIT_SCRIPT` in `luaai.c`. One file, CWD-relative, no mod awareness.
`init.lua` may `dofile` further files (text-only; see Containment). It runs
**once per stage**, on the stage's first AI tick, and again after every stage
load, because the whole Lua state is closed and rebuilt by `luaaiReset` from
`lvReset`. Nothing a script registers survives a stage change; the script is
expected to re-register.

### As a mod asset (ruled, not built)

The plan is a `LuaScript` block in `modconfig.txt`, with the file resolved
through the mod's filetable fragment like every other referenced file, so the
script inherits the mod's owner tag, resolution ladder and diagnostics:

```
LuaScript {
    file "level.lua"
}
```

Until that lands, `luaaiScriptDetected()` and `luaai_load_external_scripts()`
are the two functions that change; callers, logging and the switch stay.
`mod.c` has no parser for the block today, so writing it into a modconfig
does nothing except (once unknown-block skipping lands) get skipped.

## Level scripting: overriding a list

```lua
-- scripts/init.lua
-- Wrap the stage's own list 0x0407: run it as-is, but log the first time
-- each chr enters it.
local seen = {}

pd.register_ailist(0x0407, function(ctx)
    local me = ctx:self()
    if me and not seen[me.chrnum] then
        seen[me.chrnum] = true
        pd.log(string.format("chr %d entered 0x0407 at room %d", me.chrnum, me.room))
    end
    -- pass-through: drive the original commands exactly as the transpiled
    -- chunk would
    while true do
        local r = ctx:exec(ctx:cur())
        if r ~= 0 then
            return r
        end
    end
end)
```

`pd.register_ailist(id, fn)` takes precedence over the transpiled chunk for
that id. Which ids exist and when they start is [ailists.md](ailists.md):
`04xx` stage lists (assigned to chrs by the setup file), `10xx` background
lists (started automatically in gameplay — the natural home for stage logic),
`14xx` environment lists, `00xx` globals. A `10xx` override is how a script
gets "run something for this level each frame". Background lists do run on a
chr — `ctx:self()` in a `10xx` override returned chrnum 4005 on stage 0x1d —
so `self()` is only `nil` for object-driven lists (vehicles).

Because `init.lua` reruns on every stage load, a script that only wants one
stage branches on `pd.stage()`:

```lua
if pd.stage() == 0x1d then
    pd.register_ailist(0x1000, my_background_list)
end
```

### The `ctx` object

`ctx` is a read-only userdata (writes raise `ctx is read-only`; there is one
shared `ctx`, so a writable one would let a script change how every other
list runs).

| Call | Meaning |
|---|---|
| `ctx:cur()` | current command offset (program counter), C-maintained |
| `ctx:exec(off)` | run the original command at `off`; `0` continue, `1` yield, `2` list switched or ended |
| `ctx:run(opcode, b0, b1, ...)` | build a synthetic command and run its handler; max 60 operand bytes, each masked to 8 bits; returns the same 0/1/2 |
| `ctx:self()` | snapshot of the chr running this list — `chrnum`, `x`, `y`, `z`, `room`, `health`, `maxhealth`, `shield`, `alertness`, `target_chrnum` / `target_playernum` — or `nil` for object-driven lists |

`ctx:run` is the escape hatch for hand-written lists: any engine command with
explicit operand bytes, so a Lua list can compose existing verbs. Kai's
generated `scripts/ai.lua` wrapper library (one named function per opcode)
and its `aicommands.md` reference were **not** carried over; opcodes and
operand layouts come from `src/game/chrai.c` (`g_CommandLengths`, the
handlers) and the `AICMD_*` constants in `src/include/constants.h` for now.

### The yield contract — read this twice

**Yielding is a return, not a suspend.** When your function returns `1`, the
chunk is done for this frame. Next frame the driver calls your function again
**from its first line**. `ctx:cur()` re-reads the C program counter, which is
the only continuation state that exists.

So:

- Lua locals do not persist across a yield. Keep per-chr state in a table
  keyed by `ctx:self().chrnum`, and expect it to be wiped on stage load.
- Do not use coroutines to "wait a frame". The library is opened, but the
  driver never resumes a coroutine; a yielded coroutine is just dropped.
- A hand-written list that never returns runs until the instruction budget
  kills it (see Containment). Every loop needs a return.
- For an override, `0` and `1` both mean "done for this frame"; the driver
  calls you again next frame either way, with the PC wherever you left it.
  `2` promises the list changed — returning `2` when it did not is treated
  as a broken override and quarantines the list (`luaai.c`, the
  `returned 2 (list changed) but the list did not change` check).

This is inherited from the transpiled shape and is the reason the layer
scales to every chr in a stage without per-entity Lua state.

## Scope: what a script may override

**Ruled 2026-09-21 (Catherine), not enforced.** A mod's script may override
ailists only while a stage that mod owns is loaded.

The ownership already exists in the engine: `g_ModStageNums[stagenum]`,
read through `modNumFromStage()` (`port/src/mod.c`), is the mod whose
`stage NNN { }` block claimed that stage, or `-1` for a vanilla stage nobody
declared. The rule is one comparison at `pd.register_ailist` time — the
registering script's mod against `modNumFromStage(chraiLuaGetStageNum())` —
and a logged refusal when they differ. It depends on discovery landing first,
because until a script has an owning mod there is nothing to compare.

Consequences for an author, once enforced:

- To script a vanilla stage, declare it. A `stage 0x1d { }` block with no
  keys is enough to claim ownership; the guide for that block is the
  `pd-fojo-modconfig-authoring` skill.
- Two mods cannot fight over one stage's lists, because one stage has one
  owner. Precedence is the roster's, not the script's.
- A gameplay mod that wants to change vanilla AI everywhere cannot do it
  through ailist overrides. That is a `pd.on` event handler or a future
  `pd.register_aicmd`, not a level script.

**Today**, before discovery: the registry is one flat table keyed on ailist
id. Any script may override any id in any stage; a second registration for
the same id silently replaces the first; and a quarantined list is blamed by
list, not by owner. Nothing in the engine knows which mod a script came from.

## Containment: what happens when a script misbehaves

| Guard | Catches | What you see |
|---|---|---|
| Sandbox | `os`, `io`, `package`, `require`, `debug` are never opened | `attempt to index a nil value (global 'io')` |
| Text-only chunks | `load`/`loadfile`/`dofile` are text mode; `string.dump` is removed | a precompiled chunk fails to load |
| Instruction budget | 10,000,000 instructions per entity call, 20,000,000 for `init.lua` (`LUAAI_INSTRUCTION_BUDGET`) | `instruction budget exceeded`, then quarantine |
| `__gc` refusal | `setmetatable` rejects a metatable with `__gc` | error at the `setmetatable` call |
| Per-list quarantine | any Lua error while running a list, or an override that returns `2` without switching | `luaai: run error in list N (…): …` then `luaai: list N runs as bytecode until the next stage`, once; every other list stays on Lua |
| Suspend | out of memory, a VM that cannot be created, or a 65th quarantined list in one stage (`LUAAI_QUARANTINE_MAX`) | `… Lua suspended until the next stage`; every script path stops, including `pd.on` handlers, until `lvReset` |

Errors go to the game log through `sysLogPrintf`, prefixed `luaai:`. `pd.log`
writes there too. There is no in-game console; the ImGui overlay's Lua
section has a reload button and a one-line run box.

An event handler registered with `pd.on` gets its own budget per call and its
own `pcall`; one handler erroring does not stop the others. Events available
today: `tick`, `draw`, `weaponfire`, `chrfire`, `punch`, `alert`, `kill`,
`damage`, `headshot`, `spawn`, `roomenter`, `missioncomplete`, `firingrange`,
`weaponfound`, `weaponpickup`, `objective`, `cheatunlock`,
`challengecomplete`. All carry integer arguments only. There is no
`stageload` / `stageunload` / `modload` event; `init.lua` rerunning per stage
is the stand-in.

## Verifying it

What has to be true for "level scripting works and is scoped the way we
want", how each is checked, and where each stands. **Measured** means a
command or a run showed it; **reasoned** means it follows from the code but
no run has exercised it; **owed** means the mechanism is not built.

| # | Claim | How to check | Stands |
|---|---|---|---|
| 1 | A registered override runs instead of the transpiled chunk | `smoke/level.lua`: register a wrapper on a list a live chr is running, count entries | measured — stage list `0x401` on 0x1d, 40 entries, VM GCC build; the background list `0x1005` alongside it |
| 2 | Pass-through is behaviour-preserving | the wrapper's `ctx:exec` loop; the chr keeps doing what vanilla does | reasoned per frame; no command-for-command trace against the bytecode path has been taken |
| 3 | Yield is a return; the function re-enters from the top next frame | `smoke/level.lua` counts re-entries; every `ctx:exec` return seen was `0` or `1` | measured |
| 4 | Per-chr state survives across yields when kept in a table keyed by `chrnum` | same script | measured |
| 5 | `ctx:self()` is a fresh snapshot each call | same script: position changes between frames for a moving chr | not shown headless — the stage idles in its cutscene and nothing moved; a Mac run past the cutscene answers it |
| 6 | A `10xx` background override runs | register on the stage's background list id | measured — `0x1005` on 0x1d, 40 entries; `ctx:self()` was chrnum 4005, not `nil` |
| 7 | An erroring override quarantines only its own list | `core.lua` already proves handler isolation; the list-level case was proven in the containment slice | measured (containment slice) |
| 8 | Registrations rebuild on stage change; no cached chunk survives a reused list pointer | enter a stage, leave, re-enter, watch `luaai: loaded scripts/init.lua` twice and no stale override | **not measured** — no headless path reaches a stage change; manual on the Mac |
| 9 | Scripts load from the owning mod, in mod order, with no CWD dependence | `LuaScript` block + filetable | **owed** — discovery not built |
| 10 | A `LuaScript` naming a file absent from the fragment fails loudly at parse | parser | **owed** |
| 11 | An override for a stage the mod does not own is refused and logged | register from mod A while mod B's stage is loaded | **owed** — depends on 9 |
| 12 | Two mods' scripts cannot see each other's globals | per-mod `_ENV` | **owed** |
| 13 | With no script present, the run is identical to `--no-lua-ai` | log diff | measured (API port verification) |
| 14 | Clang builds it | Catherine's build queue | every VM figure here is GCC; clang is the Mac's |

### Running the smoke script

```
cp tools/luaai_test/smoke/level.lua <working dir>/scripts/init.lua
pd.arm64 --boot-stage 0x1d --no-sound
grep 'level smoke' <log>
```

It ends with `level smoke done N/M`. Headless on the Linux VM it needs
`xvfb-run` and software GL; a stage takes about 100 s to load and then idles
in its opening cutscene, so anything gated on `TICKMODE_NORMAL` is out of
reach there. The stage-cycle check (#8) is a Mac check.

## Not built yet

- **`LuaScript` discovery** — the block, the filetable rule, mod-order load.
- **Per-mod `_ENV`** — one shared environment today.
- **The scope rule** — depends on both of the above.
- **`pd.register_aicmd`** — new verbs from Lua. The opcode window
  (`0x0800`–`0x0fff`) and length registry are in place; the hook at
  `l_ctx_exec` is not.
- **Lifecycle events** — `stageload` and friends, and a string-capable
  dispatch.
- **`ai.lua` wrappers** — Kai generates them from the engine source with
  `tools/gen_aicommands.py`; fojo has neither the generator nor the output.
- **O(1) dispatch** — the transpiled chain is re-walked after every command.
  No real list is long enough for it to matter yet.

## Files

| File | Role |
|---|---|
| `src/game/luaai.c` | state lifecycle, `ctx`, chunk cache, override registry, containment |
| `src/game/luaai_transpile.c` | ailist bytecode → Lua chunk; no engine dependencies |
| `src/game/luaai_api*.c`, `luaai_bridge_*.c` | the `pd.*` table, one pair per group |
| `src/game/chraicmdlen.c` | command-length registry for mod opcodes |
| `port/lua/` | vendored Lua 5.4.7, built as `pd_lua` |
| `tools/luaai_test/` | transpiler unit test (`build.sh`) and the smoke scripts |
