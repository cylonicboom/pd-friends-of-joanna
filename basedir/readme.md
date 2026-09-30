# Friends of Joanna basedir

this is the data dir the game runs off. `--basedir basedir/data`, or drop it next to the exe as `data/`.

what's in it:

- `data/mods/mod_fojo` — the mod. setups, the friends' heads, mikado's heads and bodies cued off your jpn rom
- `data/mods/mod_gex_characters` — the GoldenEye X cast, cued off your gex.z64. ships no bytes of its own
- `data/ext_tex` — png / skin overrides for a few vanilla joanna bodies

what's not:

- roms. `*.z64` is ignored. `pd.ntsc-final.z64` goes in `data/`, `pd.jpn-final.z64` and `gex.z64` too if you have them
- saves, `pd.ini`, `eeprom.bin`, `mpsetups.bin` — all ignored, all yours
- the AIO stage packs. those live in the porting basedir until they can be sourced from a rom or a player's own AIO download

this used to be its own repo (`pd-friends-of-joanna-basedir-private`) with `data/mods` pushed out as a subtree. it isn't anymore; it's just a folder in the port now.
