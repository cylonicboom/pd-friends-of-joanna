# Friends of Joanna

Re-experience the magic of Rare's Perfect Dark, vicariously through your friends, in 4-player split screen.
                                           
<!-- <div style="text-align: right"> -->
<img width="215" height="120" alt="image" src="https://github.com/user-attachments/assets/78f8ebb9-a81c-4807-a27d-cd44a5440745" />
<img width="215" height="120" alt="image" src="https://github.com/user-attachments/assets/2d6f1a57-a09b-4f42-bedf-9f24dcb4c763" />
<img width="215" height="120" alt="image" src="https://github.com/user-attachments/assets/765313f7-dea6-4c4e-bde7-6ef6b8f25cd9" />
<img width="215" height="120" alt="image" src="https://github.com/user-attachments/assets/e2e31068-c254-4597-8642-d5c989fc6267" />
<img width="215" height="120" alt="image" src="https://github.com/user-attachments/assets/adf9448f-9c51-4d51-91db-ce75d699fed0" />
<!-- </div> -->




### to: Carrington Institute Perfect Agents


Rescue a dataDyne scientist from forced 'cognitive reconditioning' and save the world with up to 3 of your human friends.  Take care not to be captured and reconditioned yourself. Each agent sent into the field is issued at minimum a Replicant Engram Portable Reprogrammer. Agents are expected to recover or retire captured units and are empowered with discretion to choose their own equipment.
                                           

### to: dataDyne Employees:


Defend your workplace from meddling Carrington Institute terrorists.   The dataDyne Corporation is committed to enabling its employees to execute their service life with integrity with our first-in-class amenities: free ammunition and a Dignified Blameless Termination capsule.

                                           
### Accessible for all skill levels

- Handicap Sliders

- Universal Perfect Dark difficulty sliders  

- Configurable Respawn: vanilla shared health bar, number of lives, or brute force the level with unlimited respawns

## Status

The 2-4 player mode, Team Missions, is fully completable with any surviving Operative (ie Jo / `CHR_BOND` or co-op character) character.

Note: AI-controlled co-op buddies have been temporarily removed. 

There are minor graphics- and gameplay-related issues, and possibly occasional crashes.

**The following extra features are implemented:**
#

### 4-player co-op / counter-op mode: `Team Missions`
### 6 playable CI Combat agents in Team Missions / Solo Missions: 
#### `Joanna Dark`
#### `Velvet Dark`
#### `Mikado Dark` (needs your JPN rom, see below)
#### `Poplin Dark`
#### `Calico Dark`
#### `Willow Dark`
### Play as Combat Simulator character in Team Missions / Solo Missions [*](#sometimes-the-best-man-for-the-job-is-a-woman-and-her-friends)
### Eyelid toggling (`BACK`)
### Classic sights are first-class citizens and can be color-themed
### Drop / Throw Item  (`RS_CLICK` / `RS_CLICK + A`)

#
**The following platforms are officially supported and tested:**
* Windows 7+: i686, x86_64
* Linux:  x86_64
* MacOS:  arm64 (OS 11.0+)

Other platforms may work but are not tested or guaranteed to work.


## Running


Drop your rom in the data dir as `pd.ntsc-final.z64`. The data dir this repo ships is `basedir/data`; a release zip has it next to the exe.

Friends of Joanna doesn't ship anyone else's assets. Instead the modloader mounts them straight out of roms you already own, when it finds them in `data/` (or `data/roms/`):

- `pd.jpn-final.z64` — Mikado Dark. no JPN rom, no Mikado; the carousel just skips her.
- `gex.z64` — the GoldenEye X cast (`mod_gex_characters`). same deal.

Optionally, you can also put your Perfect Dark for GameBoy Color ROM named `pd.gbc` in the `data` directory if you want to emulate having the Nintendo 64's Transfer Pak and unlock some cheats automatically.

Optionally, you can move the data folder to `~/.local/share/perfectdark-friends-of-joanna` on Linux or `~/Library/Application Support/perfectdark-friends-of-joanna` on MacOS.

### Mods

`data/mods/` is the mod layout. Two mods ship and load by default: `mod_fojo` (the mod) and `mod_gex_characters` (a cue sheet over your gex.z64 — no bytes of its own). Anything else in there gets mounted with `--moddir`. The AIO stage packs are on the bench until they can be sourced the same way.

Additional information can be found in the [wiki](https://github.com/fgsfdsfgs/perfect_dark/wiki).

A GPU supporting OpenGL 3.0/ES3.0 or above is required to run the port.

## Controls

1964GEPD-style and Xbox-style bindings are implemented.

N64 pad buttons X and Y (or `X_BUTTON`, `Y_BUTTON` in the code) refer to the reserved buttons `0x40` and `0x80`, which are also leveraged by 1964GEPD.

Support for one controller, two-stick configurations are enabled for 1.2.

Note that the mouse only controls player 1.

Controls can be rebound in `pd.ini`. Default control scheme is as follows:

Set `Game.DefaultProfile` in `pd.ini` to an MP player profile name such as
`catherine` to auto-load it at boot. Set `Game.DefaultReality` to a reality file
name to auto-load the matching single-player game file. If either name is empty
or missing, the normal selection menu for that step is shown.

| Action           | Keyboard and mouse     | Xbox pad                 | N64 pad                   |
| -                | -                      | -                        | -                         |
| Fire / Accept    | LMB/Space              | RT                       | Z Trigger                 |
| Aim mode         | RMB/Z                  | LT                       | R Trigger                 |
| Use / Cancel     | E                      | N/A                      | B                         |
| Use / Accept     | N/A                    | A                        | A                         |
| Crouch cycle     | N/A                    | LS Click                 | `0x80000000` (Extra)      |
| Half-Crouch      | Shift                  | N/A                      | `0x40000000` (Extra)      |
| Full-Crouch      | Control                | N/A                      | `0x20000000` (Extra)      |
| Toggle Eyelids   | G + E                  | RS Click + A             | `0x00400000` (Extra)      |
| Drop Item        | G                      | RS Click                 | `0x00800000` (Extra)      |
| Throw Item       | G + E                  | RS Click + A             | `0x00800000 \| A` (Extra) |
| Reload           | R                      | X                        | X `(0x40)`                |
| Previous weapon  | Mousewheel forward     | B                        | D-Left                    |
| Next weapon      | Mousewheel back        | Y                        | Y `(0x80)`                |
| Radial menu      | Q                      | LB                       | D-Down                    |
| Alt fire mode    | F                      | RB                       | L Trigger                 |
| Alt-fire oneshot | `F + LMB` or `E + LMB` | `A + RT` or  `RB + RT`   | `A + Z`     or `L + Z`    |
| Quick-detonate   | `E + Q`   or `E + R`   | `A + B`  or  `A + X`     | `A + D-Left`or `A + X`    |

## Building / Setup Friends of Joanna


Everything builds out of this tree now. Setup files (`src/setups`, ailists included) are compiled here with clang + lld by `tools/mksetups` / `pdt build-setups` and land in `mod_fojo/files` — no N64 build, no docker.

This project has two branches:

#### `fojo`
Based on `port`. engine changes, aicmd changes, setups, mod code, the modloader — all of it.
#### `fojo-ailists`
Based on `master`. Where the setup / ailist changes used to live. Reference only now; the setups were ported into `fojo` and that's where they're edited.
#### [`docker-caroll`](https://github.com/cylonicboom/docker-caroll/tree/fojo)
build helpers. optional.

For my own sanity, I cobbled together some helper build scripts for building Perfect Dark projects.

Place this in your profile or shell rc:
You'll want to adjust for your platform. This is my MacOS setup:
````
# perfect-dark is forever

export PATH=$PATH:${HOME}/src/pd/tools/docker-caroll

# docker-caroll / pc-port uses these to setup Friends of Joanna mods
export PD_MODDIR="$HOME/Library/Application Support/perfectdark-friends-of-joanna/mods"
export PD_SAVEDIR="$HOME/Library/Application Support/perfectdark-friends-of-joanna/"
export PD_BASEDIR="$HOME/Library/Application Support/perfectdark-friends-of-joanna/"
export PD_ROMFILE="$HOME/Library/Application Support/perfectdark-friends-of-joanna/pd.ntsc-final.z64"
````

My invocation to rebuild looks like this:

```
# run this from inside friends of jo project
pdt build-port --root $(realpath .)
```

...and a clean rebuild:
```
# run this from inside friends of jo project
pdt build-port --root $(realpath .) --clean
```

### Setup files

The stage setups (`src/setups/*.c`, ailists included) are data, and they build here. A modified setup doesn't ship as a setup — that's Rare's data with my edits in it — it ships as an xdelta of the change, a few hundred bytes in `basedir/data/mods/mod_fojo/patches/`, applied against the file inflated out of your own rom.

`cmake --build` (or `pdt build-port`) does it: the `pd_setups` target rebuilds the patches for any setup you touched and rewrites `filetable.dat`. By hand:

```
tools/mksetups --patches --mod-dir basedir/data/mods/mod_fojo setupame
tools/mkfiletable/mkfiletable mod_fojo --workspace basedir/data/mods/mod_fojo --output basedir/data/mods/mod_fojo --rom-dir basedir/data
```

Needs `brew install llvm lld xdelta` (or your distro's clang, lld, xdelta3) and your roms in `basedir/data` (or `basedir/data/roms`) — the ntsc rom for the setups, and the jpn rom too, because Mikado's rows are looked up by name in it when the table is written. The build reads roms from its own tree and nowhere else; it doesn't care where your installed game keeps them. No MIPS gcc, no docker, no N64 tree; the compiled setup is byte-identical to what the old gcc pipeline made, and mkfiletable proves every patch applies before it writes the table. Without those tools the game builds against the committed patches, which is what you want on a machine that isn't editing setups.

There is no N64 rom to build anymore.

### Build Friends of Joanna PC Port without `docker-caroll`

Follow PC port instructions as below.

#### [`fgsfdsfgs/perfect_dark@port`](https://github.com/fgsfdsfgs/perfect_dark) vanilla pc port build instructions


## Friends of Joanna Credits

#### Catherine Reprobate
concept / developer / Calico Dark Likeness

#### Raine Stoltenberg
co-writing / editing

#### iamgreaser 
concurrent 4-player counter-op effort I borrowed some patches from

#### Foslerfer
Poplin Dark likeness

#### Johnny Thunder
Poplin, Willow, Calico model / imported from Silvo

#### DabDavis
third person camera / the camera rig on top of it / the stance system / jump, roll, flinch, melee / damage reactions / hit locations / the console-patch importer the rom patcher grew out of  — Dab's Mod: https://github.com/DabDavis/perfect-dark-dabs-mod

#### Murk
light glare occlusion, by way of perfect_dark_netplay

#### jonaeru
modloader base / gex model scale table / arena names / AIO, with Atari-Dude

#### JillyJane
Willow Dark model

#### Wreck
GoldenEye X

#### Lua 5.4
PUC-Rio, MIT: https://www.lua.org — `port/lua`

#### Dear ImGui 1.92
Omar Cornut, MIT: https://github.com/ocornut/imgui — `port/third_party/imgui`. the fojo debugger / level editor lives on it

#### parson 1.5.3
Krzysztof Gabis, MIT: https://github.com/kgabis/parson — `tools/mkfiletable/vendor`. the json reader behind `mkfiletable`

#### stb_image / stb_image_write
Sean Barrett, public domain: https://github.com/nothings/stb — `port/include/external`. png texture overrides (`ext_tex`) and the skin match panel

#### minimp3
lieff, CC0 — `port/include/external`, inherited from upstream

#### fgsfdsfgs
Upstream Perfect Dark PC Port: https://github.com/fgsfdsfgs/perfect_dark

#### Ryan Dwyer
Perfect Dark Decomp: https://gitlab.com/ryandwyer/perfect-dark

#
###### * Sometimes, the best man for the job is a woman... and her friends.
