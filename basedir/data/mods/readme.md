# mods

one folder per mod. a mod is a `modconfig.txt` plus a `filetable.dat` built from its `*_filetable.json` by `tools/mkfiletable`; `files/` and `textures/` only if it actually ships bytes.

the two here load by default (`port/src/fs.c`). anything else goes on with `--moddir`.

authoring: see the wiki. the short version is that a mod names files, heads, bodies and stages, and the engine gives them ids at boot — you don't pick rows.
