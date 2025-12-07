# Modloader & Overlay Architecture Review

**Date:** December 6, 2025  
**Subject:** Review of `port/src/mod.c`, `port/src/fs.c`, `port/src/romdata.c` against Legacy Pipeline

## 1. Executive Summary

The Perfect Dark port implements a **hybrid overlay system** that successfully bridges the gap between the N64's static ROM-based pipeline and a modern, dynamic filesystem. It achieves this by intercepting the low-level `fileLoad` function and redirecting it through a new "ROM Data" abstraction layer (`romdata.c`), which in turn utilizes a priority-based filesystem overlay (`fs.c`).

This design allows the original game code to remain largely untouched (requesting assets by ID) while enabling powerful modding capabilities like per-mode asset replacements and texture overrides.

## 2. Architectural Breakdown

### 2.1 The Legacy Pipeline (`src/game/file.c`)
*   **Original Design**: The N64 game references assets via a static table of `extern void *` pointers (e.g., `_file_bg_sev_seg`), which originally pointed to physical addresses in the ROM.
*   **The Hook**: The port modifies the `fileLoad` function in `src/game/file.c`. Instead of DMAing from a ROM address, it now calculates the file ID (index in `g_FileTable`) and calls `romdataFileLoad`.

### 2.2 The Bridge Layer (`port/src/romdata.c`)
This is the critical component that translates "N64 File IDs" into "PC Filenames".
*   **Mapping**: It maintains `fileSlots`, a table mapping File IDs to filenames (loaded from `files/` or `segs/`).
*   **Context Awareness**: It implements a unique "Context Prefix" system (`romdataResolvePath`). It checks the game state (Solo, Co-op, Multiplayer, Boot, Intro) and looks for prefixed files (e.g., `SOLO:files/bg_sev.z64` vs `CS:files/bg_sev.z64`). This allows mods to alter assets for specific game modes without code changes.
*   **Fallback**: If an external file is not found, it falls back to reading the data from the `pd.z64` ROM image, ensuring compatibility.

### 2.3 The Filesystem Overlay (`port/src/fs.c`)
*   **Priority System**: Implements a "Mod Stack" approach. When a file is requested via `fsFullPath`, it iterates through active mod directories (`$M`) before checking the base directory (`$B`).

## 3. Strengths

1.  **Non-Invasive Integration**: The game logic (missions, setup, AI) continues to use `fileLoadToNew(FILE_BG_SEV)` without knowing that the file is coming from `mods/MyMod/files/bg_sev.z64`.
2.  **Granular Overrides**: The system supports replacing individual files. You don't need to repack a whole ROM; you just drop a file in the right folder.
3.  **Context Sensitivity**: The `romdataResolvePath` logic is a standout feature. Allowing a mod to change a level only for "Counter-Op" or "Combat Simulator" is a powerful capability derived from the port's awareness of the game state.

## 4. Weaknesses & Risks

1.  **Performance Overhead**: The overlay system relies on `fsFileSize` (which likely uses `stat`) to check for file existence in mod directories. For every asset load, the system may perform multiple disk checks.
2.  **Hardcoded Paths**: `romdata.c` contains hardcoded references to `files/` and `segs/`.
3.  **Data Safety**: The pipeline generally trusts data loaded from disk. If a mod provides a corrupted file (e.g., bad compression header), it can crash the game deep in the decompression logic (`rzip`).

---

## 5. Critical Analysis: "Fail-Through" Loading Proposal

**Proposal**: If a file fails to load (or fails validation), treat it as "non-existent" and continue searching the priority stack (Mod -> Base -> ROM).

### 5.1 Current Behavior
Currently, `romdataFileLoad` in `port/src/romdata.c` performs the following:
1.  Constructs a path for the *current active mod* (`g_ModNum`).
2.  Checks if the file exists (`fsFileSize > 0`).
3.  If yes, attempts to load it (`fsFileLoad`).
4.  If load succeeds (returns pointer), it uses it.
5.  If load fails (returns NULL), it falls back to `SRC_ROM`.

**Critique**: The current implementation for `fileSlots` (game assets) appears to check **only the active mod** and then the ROM. It does not seem to iterate through *all* available mod directories (the "Mod Stack") like `fs.c` does for generic files.

### 5.2 Feasibility of Proposal

Implementing a robust "Fail-Through" system requires two components: **Iteration** and **Validation**.

#### A. Iteration (The Mod Stack)
To truly "continue searching," `romdataFileLoad` needs to be updated to iterate through `modDirs` (like `fsModFullPath` does).
*   **Benefit**: Allows for "Texture Packs" or "Audio Packs" to be stacked. If Mod A doesn't have the file, check Mod B.
*   **Risk**: Increased I/O. Checking 5 mod folders for every asset could slow down loading times.

#### B. Validation (The Safety Net)
"Failed loads" can mean two things:
1.  **IO Failure**: File exists but cannot be read. (Already handled: falls back to ROM).
2.  **Content Failure**: File reads successfully but contains garbage (e.g., truncated, bad header).

To support falling back on *Content Failure*, we must validate the data *before* returning it.
*   **Challenge**: For compressed files (`rzip`), validation often requires full decompression. This is CPU intensive.
*   **Strategy**:
    *   **Light Validation**: Check headers (magic numbers, file size sanity checks).
    *   **Heavy Validation**: Attempt decompression into a scratch buffer. If it fails, discard the buffer and try the next source.

### 5.3 Recommendation

I agree with the proposal, with the following caveats:

1.  **Silent Failures are Dangerous**: If a modder makes a mistake and their file is skipped silently, they will be confused why their mod isn't working.
    *   **Requirement**: Any fallback triggered by a validation failure MUST log a visible warning (e.g., `[WARNING] Mod file 'bg_sev.z64' corrupted. Falling back to ROM.`).

2.  **Define "Validation"**:
    *   Implement a `bool romdataValidate(void* data, u32 size)` function.
    *   For `rzip` files, check the header bytes.
    *   For `modconfig` or text files, check for null terminators or basic syntax.

3.  **Refactor `romdataFileLoad`**:
    *   Change the logic to loop through the Mod Stack (if multi-mod support is desired for assets).
    *   Inside the loop: `Load -> Validate -> If Valid, Return -> Else, Log & Continue`.

### 5.4 Proposed Logic Flow

```c
// Pseudo-code for robust fail-through
for (each mod_dir in priority_stack) {
    path = resolve_path(mod_dir, file_id);
    if (exists(path)) {
        data = load_file(path);
        if (data) {
            if (validate_asset(data)) {
                return data; // Success
            } else {
                log_warning("Corrupt asset in %s, skipping...", mod_dir);
                free(data);
                // Continue to next mod / base / ROM
            }
        }
    }
}
// Fallback to ROM
return load_from_rom(file_id);
```
