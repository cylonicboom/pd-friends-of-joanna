#!/bin/bash

# Perfect Dark Friends of Joanna runner script
# Based on .gdbinit configuration

# Set up environment variables (use explicit paths for foj branch)
export PD="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
export PD_MODDIR="${PD_MODDIR:-$HOME/.local/share/perfectdark-friends-of-joanna/mods}"
export PD_SAVEDIR="${PD_SAVEDIR:-$HOME/.local/share/perfectdark-friends-of-joanna/}"  
export PD_BASEDIR="${PD_BASEDIR:-$HOME/.local/share/perfectdark-friends-of-joanna/}"
export PD_ROMFILE="${PD_ROMFILE:-$HOME/.local/share/perfectdark-friends-of-joanna/pd.ntsc-final.z64}"
# Sanity checks for required directories and files
if [ ! -d "$PD_MODDIR" ]; then
  echo "Error: Mod directory not found: $PD_MODDIR"
  exit 1
fi

if [ ! -d "$PD_SAVEDIR" ]; then
  echo "Error: Save directory not found: $PD_SAVEDIR"
  exit 1
fi

if [ ! -f "$PD_ROMFILE" ]; then
  echo "Error: ROM file not found: $PD_ROMFILE"
  exit 1
fi

# Path to executable
PD_EXECUTABLE="$PD/build/pd.x86_64"

# Check if executable exists
if [ ! -f "$PD_EXECUTABLE" ]; then
    echo "Error: Perfect Dark executable not found at $PD_EXECUTABLE"
    echo "Please run: cmake --build $PD/build"
    exit 1
fi

# Check if ROM file exists
if [ ! -f "$PD_ROMFILE" ]; then
    echo "Error: ROM file not found at $PD_ROMFILE"
    echo "Please ensure you have the Perfect Dark ROM file in the correct location"
    exit 1
fi

# Default arguments (based on .gdbinit)
DEFAULT_ARGS=(
    --moddir "$PD_MODDIR/mod_aio"
    --moddir "$PD_MODDIR/mod_gex" 
    --moddir "$PD_MODDIR/mod_kakariko"
    --moddir "$PD_MODDIR/mod_dark_noon"
    --moddir "$PD_MODDIR/mod_goldfinger_64"
    --moddir "$PD_MODDIR/mod_fojo"
    --savedir "$PD_SAVEDIR"
    --basedir "$PD_BASEDIR" 
    --rom-file "$PD_ROMFILE"
)

# Parse arguments for level selection and other options
ARGS=("${DEFAULT_ARGS[@]}")

# Add any remaining arguments
ARGS+=("$@")

echo "Starting Perfect Dark with args: ${ARGS[*]}"
echo "Executable: $PD_EXECUTABLE"
echo "ROM: $PD_ROMFILE"
echo ""

# Run Perfect Dark
exec "$PD_EXECUTABLE" "${ARGS[@]}"
