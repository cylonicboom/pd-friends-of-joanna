# tools/aio — the All In One drop as fojo mods

Lays Atari-Dude's *Perfect Dark: All In One* mod pack out as six fojo mods.
The pack's bytes are his and are **not in this repository**; get the zip from
the link he and Jonaeru share, unzip it, and point the tool at it.

    make -C tools/mkfiletable                     # slugmod + mkfiletable, once
    python3 tools/aio/layout.py --src ~/Downloads/pd-aio-6.0

That writes, under `basedir/data/mods/`:

| mod                   | carries                                                        |
|-----------------------|----------------------------------------------------------------|
| `mod_aio_characters`  | the C*/G* models and sequences every pack ships, and their textures; the HeadsAndBodies roster |
| `mod_aio_pdplus`      | `mod_allinone`'s levels (PD Plus / All Solos / Suburb / Training Day) |
| `mod_aio_gex`         | `mod_gex`'s levels                                             |
| `mod_aio_kakariko`    | `mod_kakariko`'s                                               |
| `mod_aio_darknoon`    | `mod_dark_noon`'s                                              |
| `mod_aio_goldfinger`  | `mod_goldfinger_64`'s                                          |

Each mod gets `files/`, `textures/` (and `sequences/`) filled from the drop —
**byte-identical, same relative paths, gitignored** — plus the three tracked
metadata files: `modconfig.txt`, `<mod>_filetable.json`, `filetable.dat`.
(There used to be a `<mod>_texmap.json` too, mapping texture ids to slots in
a global partition; the engine retired the partition and texture ids stay
the mod's own.) The fojo names (`Carmourguard_aio_charactersZ`,
`Cheadcaseydark_aio_charactersZ/0246.bin`) live only in the manifest and the
PDFT, which carry name and path apart; `slugmod --keep-paths` writes them
without renaming anything on disk. So the tracked metadata is an overlay: a
fresh checkout plus the drop plus one run of this tool is the whole thing,
and re-running it after a new drop re-attributes every file by hash and
reports what it could not place.

Mount them in this order, after `mod_fojo`:

    --moddir mod_aio_characters --moddir mod_aio_darknoon --moddir mod_aio_gex
    --moddir mod_aio_goldfinger --moddir mod_aio_kakariko --moddir mod_aio_pdplus

`mod_gex_characters` is a separate distribution and is never mounted with these.

## What is in here

- `layout.py` — the generator. Hashes the packs, splits cast from levels,
  carries the pack modconfigs' credits verbatim, builds the HeadsAndBodies
  block from `roster.json`, names from the drop's own text banks (as
  `label`s — the banks themselves overwrite live vanilla text slots and are
  not carried), censuses texture ids from both the texconfig table and the
  display-list binds, scopes single-owner textures to their model by name,
  then runs `slugmod --keep-paths --apply` and `mkfiletable`.
- `roster.json` — `g_HeadsAndBodies`, `g_MpHeads`, `g_MpBodies`,
  `g_MpFemaleHeads` and the `HEAD_*`/`BODY_*` constants from Jonaeru's
  `port-mods/all-in-one` branch of `perfect_dark`, pulled by
  `extract_roster.py <checkout> roster.json`. Re-pull when he adds rows.
- `stages.json` — his `stagetable.c` / `g_MpArenas` / env / lang / pdmain
  rows for the 6.0 levels, used when the stage blocks were written.
- `stages/<mod>.txt` — the stage declarations, fojo-authored, pre-slug.
  `stage-renames.json` and `overrides/` record the rulings already folded
  into them (arena names, stage renames, dropped blocks, blocks added for
  levels the 6.0 drop ships that the older configs never declared).
- `stage-proposals.txt` — which pack each 6.0 level came from, by hash, with
  the stagetable row it maps to.

## Checking a set

    tools/modsetcheck/modsetcheck basedir/data/mods/mod_aio_characters basedir/data/mods/mod_aio_pdplus ...

should end `0 error(s), 0 warning(s)`. The run also writes
`basedir/data/mods/aio-layout-report.md` — hashes, what was held back and
why, texture scoping counts.
