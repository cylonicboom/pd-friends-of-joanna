# MOD: Friends of Joanna

Fork / Mod of the Perfect Dark PC Port, with extra cheese.
## 4-Player Counter + Co Operative

Re-experience the magic of Rare's Perfect Dark, vicariously through your friends, in 4-player split screen.
                                           


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

- 4-player co-op / counter-op mode: `Team Missions`
- 4 playable CI Combat agents in Team Missions / Solo Missions: `Perfect Dark`, `Velvet Dark`, `Mikado Dark`, `Poplin Dark`
- Play as Combat Simulator character in Team Missions / Solo Missions [*](#sometimes-the-best-man-for-the-job-is-a-woman-and-her-friends)
- Eyelid toggling (`BACK`)
- Classic sights are first-class citizens and can be color-themed
- Drop / Throw Item  (`RS_CLICK` / `RS_CLICK + A`)

**The following platforms are officially supported and tested:**
* Windows 7+: i686, x86_64
* Linux:  x86_64
* MacOS:  arm64 (OS 11.0+)

Other platforms may work but are not tested or guaranteed to work.


## Running


Requirement: Powershell

````
# windows only: one-time step to enable scripts
Set-ExecutionPolicy Unrestricted -Scope CurrentUser

# the actual launcher:
.\run-fojo.ps1
````

Drop rom in data dir

Optionally, you can also put your Perfect Dark for GameBoy Color ROM named `pd.gbc` in the `data` directory if you want to emulate having the Nintendo 64's Transfer Pak and unlock some cheats automatically.

Optionally, you can move the data folder to `~/.local/share/perfectdark` on Linux or `~/Library/Application Support/perfectdark` on MacOS.

Additional information can be found in the [wiki](https://github.com/fgsfdsfgs/perfect_dark/wiki).

A GPU supporting OpenGL 3.0/ES3.0 or above is required to run the port.

## Controls

1964GEPD-style and Xbox-style bindings are implemented.

N64 pad buttons X and Y (or `X_BUTTON`, `Y_BUTTON` in the code) refer to the reserved buttons `0x40` and `0x80`, which are also leveraged by 1964GEPD.

Support for one controller, two-stick configurations are enabled for 1.2.

Note that the mouse only controls player 1.

Controls can be rebound in `pd.ini`. Default control scheme is as follows:

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


Because Friends of Joanna requires setup file changes, the n64 rom must also be built (`fojo-ailists`) For the sake of convinience, these instructions have you clone two work trees, one for the PC port and one for the N64 version.

This project has two main branches:

#### `fojo`
Based on `port`, this is where all the engine changes, aicmd changes, other mod code goes.
#### `fojo-ailists`
Based on `master`, this is where setup / ailist changes for stages go
#### [`docker-caroll`](https://github.com/cylonicboom/docker-caroll/tree/fojo)

The two must be built seperately and the setup files copied to the data directory.

For my own sanity, I cobbled together some helper build scripts for building Perfect Dark projects.

Place this in your profile or shell rc:
You'll want to adjust for your platform. This is my MacOS setup:
````
# perfect-dark is forever

# ryan dwyer's pdtools
# technically only needed for mouse-injector pipelines but 
# docker-caroll will yell at you if these aren't defined
export PATH="$PATH:$HOME/src/pd/pdtools/bin"
#define PDTOOLS for docker-caroll scripts
export PDTOOLS="$HOME/src/pd/pdtools"

# reference n64 decomp workspace
export PD="$HOME/src/pd/perfect-dark"
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


### Build Friends of Joanna PC Port

[Follow PC port instructions as below.](#building)

### Build Friends of Joanna N64 Rom

- Clone the N64 repo: `https://github.com/cylonicboom/pd-friends-of-joanna -b fojo-ailists fojo-ailists`
- [Setup and build the PD tree as you'd normally setup an N64 rom](https://github.com/N64decomp/port/tree/n64-friends-of-joanna?tab=readme-ov-file#installation-requirements)
- Copy the built rom to your data dir.

Assuming you're using `docker-caroll`, you can use this oneliner to rebuild Friends of Joanna, the setup files, and a mod layout

`pd build-port --clean --root $FRIENDSOFJOANNA && pd psake --tasklist foj --root $FRIENDSOFJOANNA64`


## Building

### Build instructions follow upstream decomp / pc port.

### Windows

1. Install [MSYS2](https://www.msys2.org).
2. Open the `MINGW64` prompt if building for x86_64, or the `MINGW32` prompt if building for i686. (**NOTE:** _do not_ use the `MSYS` prompt)
3. Install dependencies:  
   `pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-SDL2 mingw-w64-x86_64-zlib mingw-w64-x86_64-cmake mingw-w64-x86_64-python3 mingw-w64-i686-toolchain mingw-w64-i686-SDL2 mingw-w64-i686-zlib mingw-w64-i686-cmake mingw-w64-i686-python3 make git`
4. Get the source code:  
   `git clone --recursive https://github.com/fgsfdsfgs/perfect_dark.git && cd perfect_dark`
5. Run `cmake -G"Unix Makefiles" -Bbuild .`.
   * Add ` -DROMID=pal-final` or ` -DROMID=jpn-final` at the end of the command if you want to build a PAL or JPN executable respectively.\
6. Run `cmake --build build -j4 -- -O`.
7. The resulting executable will be at `build/pd.x86_64.exe` (or at `build/pd.i686.exe` if building for i686).
8. If you don't know where you downloaded the source to, you can run `explorer .` to open the current directory.

### Linux / OSX

1. Ensure you have gcc, g++ (version 10.0+), make, cmake, git, python3 and SDL2 (version 2.0.12+), libGL and ZLib installed on your system.
   * If you wish to crosscompile, you will also need to have libraries and compilers for the target platform installed, e.g. `gcc-multilib` and `g++-multilib` for x86_64 -> i686 crosscompilation.
2. Get the source code:  
   `git clone --recursive https://github.com/fgsfdsfgs/perfect_dark.git && cd perfect_dark`
3. Run the following command:
   * ```cmake -G"Unix Makefiles" -Bbuild .```
   * Add ` -DROMID=pal-final` or ` -DROMID=jpn-final` at the end of the command if you want to build a PAL or JPN executable respectively.
   * Add ` -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32` at the end of the command if you want to crosscompile from x86_64 to x86.
4. Run `cmake --build build -j4`.
5. The resulting executable will be at `build/pd.<arch>` (for example `build/pd.x86_64`).

### MacOS

1. Set up Homebrew.
2. Install dependencies:
   * Execute command: `brew install cmake gcc python3 zlib git`
3. Install SDL2:
   * Execute commands:
     ```
     wget http://libsdl.org/release/SDL2-2.30.9.dmg -O SDL2.dmg
     hdiutil mount SDL2.dmg
     sudo cp -vr /Volumes/SDL2/SDL2.framework /Library/Frameworks
     hdiutil detach /Volumes/SDL2
     ```
   * This installs SDL2 system-wide and this is how the automatic builds are done. The game will also look for it in the executable path, so you could
     download it locally instead.
4. Get the source code:  
   `git clone --recursive https://github.com/fgsfdsfgs/perfect_dark.git && cd perfect_dark`
5. Configure:
   * Execute command: `cmake -G"Unix Makefiles" -Bbuild -DCMAKE_OSX_ARCHITECTURES=x86_64 .`
   * Replace `x86_64` with `arm64` if building for an ARM64 Mac.
   * Add ` -DROMID=pal-final` or ` -DROMID=jpn-final` at the end of the command if you want to build a PAL or JPN executable respectively.
6. Build:
   * Execute command: `cmake --build build --target pd -j4 --clean-first`
7. The resulting executable will be at `build/pd.<arch>` (for example `build/pd.x86_64`).
   * You might need to execute `chmod +x build/pd.x86-64` before you can run it.

### Nintendo Switch


WARNING: Friends of Joanna is completely untested on Nintendo Switch

1. Set up the [devkitA64 environment](https://devkitpro.org/wiki/Getting_Started).
   * On Windows you can do it under MSYS2 or WSL, usually MSYS2 is recommended.
   * If using MSYS2, make sure to use the **MSYS2** shell, **not** MINGW32 or MINGW64.
2. Install host dependencies:
   * On MSYS2: execute command `pacman -Syuu && pacman -S git make cmake python3`
   * On Linux: use your package manager as normal to install the above dependencies.
3. Install Switch toolchain and dependencies:
   * Execute commands:
     ```
     dkp-pacman -Syuu
     dkp-pacman -S devkitA64 libnx switch-zlib switch-sdl2 switch-cmake dkp-toolchain-vars
     ```
   * If in MSYS2 or `dkp-pacman` doesn't work, replace it with just `pacman`.
4. Get the source code:  
   `git clone --recursive https://github.com/fgsfdsfgs/perfect_dark.git && cd perfect_dark`
5. Ensure devkitA64 environment variables are set:
   * Execute command: `source /opt/devkitpro/switchvars.sh`
   * If your `$DEVKITPRO` path is different, substitute that instead or set the variables manually.
6. Configure:
   * Execute command: `aarch64-none-elf-cmake -G"Unix Makefiles" -Bbuild .`
   * Add ` -DROMID=pal-final` or ` -DROMID=jpn-final` at the end of the command if you want to build a PAL or JPN executable respectively.
7. Build:
   * Execute command: `make -C build -j4`
8. The resulting executable will be at `build/pd.arm64.nro`.

### Notes

Alternate compilers or toolchains can be specified by passing `-DCMAKE_TOOLCHAIN_FILE=whatever` as normal. The port does not build with Visual Studio.

You will need to provide a `jpn-final` or `pal-final` ROM to run executables built for those regions, named `pd.jpn-final.z64` or `pd.pal-final.z64`.

It might be possible to build and run the game on platforms that are not specified in the supported platforms list (e.g. Linux on armv7), but this has not been tested.


## Friends of Joanna Credits

#### Catherine Reprobate
concept / developer

#### Raine Stoltenberg
co-writing / editing

#### iamgreaser 
concurrent 4-player counter-op effort I borrowed some patches from

#### Foslerfer
Poplin Dark likeness

#### Johnny Thunder
Poplin Dark model / imported from Silvo

#### fgsfdsfgs
Upstream Perfect Dark PC Port: https://github.com/fgsfdsfgs/perfect_dark
#### Ryan Dwyer
Perfect Dark Decomp: https://gitlab.com/ryandwyer/perfect-dark

#
###### * Sometimes, the best man for the job is a woman... and her friends.
