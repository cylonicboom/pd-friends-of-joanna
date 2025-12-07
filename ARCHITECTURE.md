# Friends of Joanna - Architecture Documentation

## 1. Introduction

**Friends of Joanna** is a modern port of the *Perfect Dark* decompilation project. It aims to bring the classic N64 title to modern platforms (Windows, Linux, macOS, Switch) with enhanced features such as 4-player co-op, counter-op, high framerates, and modern controls, while preserving the original game logic.

## 2. High-Level Architecture

The project is structured into three main layers:

1.  **Port Layer (`port/`)**: This layer acts as the abstraction bridge between the modern operating system/hardware and the game engine. It handles window management, input, audio output, and file I/O using libraries like SDL2 and OpenGL.
2.  **Game Engine (`src/game/`)**: This contains the core game logic, largely decompiled from the original N64 ROM. It implements the gameplay rules, AI, physics, and game state management.
3.  **Library Layer (`src/lib/`)**: This layer provides reimplementations or wrappers for the Nintendo 64 SDK (LibUltra) functions, allowing the original game code to run on modern architectures.

## 3. Directory Structure

*   **`port/`**: Platform-specific code and the abstraction layer.
    *   `src/`: Source code for the port layer (video, audio, input, etc.).
    *   `include/`: Headers for the port layer.
    *   `fast3d/`: Implementation of the Fast3D graphics microcode (HLE).
*   **`src/`**: Core game source code.
    *   `game/`: Game logic (player, AI, menus, props, etc.).
    *   `lib/`: LibUltra reimplementation and other utility libraries.
    *   `include/`: Shared headers, including N64 SDK headers.
    *   `assets/`: Asset definitions.
*   **`include/`**: Global include files.
*   **`cmake/`**: CMake modules and build scripts.
*   **`tools/`**: Utilities for asset extraction and conversion.

## 4. Key Subsystems

### 4.1. Rendering
The rendering pipeline translates N64 display lists (Gfx) into modern graphics API calls.
*   **Fast3D**: The project uses a High-Level Emulation (HLE) approach for the Reality Signal Processor (RSP) graphics microcode. This is located in `port/fast3d/`.
*   **Backend**: The port layer (`port/src/video.c`) initializes an OpenGL context via SDL2.
*   **Flow**: The game logic generates display lists -> Fast3D interpreter processes them -> OpenGL draw calls are issued.

### 4.2. Audio
*   **Mixing**: Audio mixing is performed in software, similar to the original hardware but adapted for modern CPUs (`port/src/mixer.c`).
*   **Output**: The mixed audio buffer is sent to the audio hardware using SDL2's audio subsystem (`port/src/audio.c`).
*   **Formats**: Supports MP3 and other formats via `src/lib/mp3/` and `src/lib/naudio/`.

### 4.3. Input
*   **Abstraction**: SDL2 events are captured in `port/src/input.c`.
*   **Mapping**: These events are mapped to the internal N64 controller state structure (`OSContPad`).
*   **Enhancements**: The port adds support for mouse look (mapping mouse delta to analog stick inputs) and dual-analog setups, which are handled before passing the state to the game logic.

### 4.4. File System & Assets
*   **ROM Loading**: The game requires an original N64 ROM (`pd.*.z64`) to function.
*   **Asset Loading**: Assets are loaded from the ROM or from external files for modding support (`port/src/fs.c`).
*   **Modding**: `port/src/mod.c` provides mechanisms to override game assets with custom files.

### 4.5. Game Logic
The game logic is split into several modules within `src/game/`:
*   **Player (`bond*.c`)**: Handles Bond's movement, weapons, and state.
*   **AI (`bot*.c`, `chrai.c`)**: Handles enemy and bot behavior.
*   **Props (`prop*.c`)**: Manages interactive objects in the world.
*   **Setup (`setup.c`, `lv.c`)**: Handles level loading and initialization.

### 4.6. Modding System
The project includes a comprehensive modding system (`port/src/mod.c`) that allows for extensive customization without recompiling the game.
*   **Configuration**: Mods are defined using `modconfig.txt` files, which can override stage properties, music, weather, and more.
*   **Asset Overrides**: Supports loading custom textures (`textures/`), animations (`animations/`), and audio sequences (`sequences/`) from external files.
*   **Multiplayer Customization**: Allows defining custom heads and bodies for multiplayer characters via the configuration file.
*   **Dynamic Loading**: The system can switch active mods dynamically based on the stage being loaded.

## 5. Build System

The project uses **CMake** as its build system.
*   **Targets**: It supports cross-compilation for various architectures (x86_64, i686, arm64) and platforms (Windows, Linux, macOS, Switch).
*   **Configuration**: `CMakeLists.txt` defines the build targets and dependencies (SDL2, ZLib, OpenGL).
*   **Asset Generation**: Custom tools in `tools/` are used to process assets during the build or setup phase.

## 6. Friends of Joanna Specifics

This fork introduces specific features:
*   **Co-op/Counter-op**: Logic for 4-player co-op and counter-op modes (experimental).
*   **Netplay**: (If applicable, details on how networking is handled for these modes would go here).
*   **Configuration**: Enhanced configuration options via `pd.ini` handled in `port/src/config.c`.
