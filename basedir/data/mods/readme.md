# mods

one folder per mod. a mod is a `modconfig.txt` plus a `filetable.dat` built from its `*_filetable.json` by `tools/mkfiletable`; `files/` and `textures/` only if it actually ships bytes.

which mods mount is a modlist in `modlists.txt`: `--modlist NAME` picks one, otherwise `ModList` under `[Game]` in pd.ini (starts as `fojo+aio`), otherwise the file's `default`. `--moddir` on the command line still overrides both for a one-off.

friends of joanna is four mods:

- `mod_fojo-stages-port4p` — setups with only what four players need
- `mod_fojo-stages-fojo` — the same plus fojo's own stage changes. mounted *instead of* stages-port4p, never beside it; this is the default
- `mod_fojo-characters-port4p` — the heads and bodies, each one a Combat Simulator character with its carousel name (`label`)
- `mod_fojo-characters-fojo` — fojo's bios for them (`chrbios.md` → `tools/mkchrbios build-mod`)

both stage mods build from `src/setups`: the plain build is port4p, `tools/mksetups -D FOJO_FANFIC` is fojo.

authoring: see the wiki. the short version is that a mod names files, heads, bodies and stages, and the engine gives them ids at boot — you don't pick rows.
