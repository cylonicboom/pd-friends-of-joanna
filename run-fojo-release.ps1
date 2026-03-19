#!/usr/bin/env pwsh
# Perfect Dark Friends of Joanna runner script (PowerShell)
# Cross-platform: Windows, Linux, MacOS
#

# Get script directory
$PD = Split-Path -Parent $MyInvocation.MyCommand.Definition
$DATA_DIR = Join-Path $PD "data"

# Set up environment variables
#
function Get-OS {
    if ($PSVersionTable.PSEdition -eq "Core") {
        if ($IsWindows) { return "windows" }
        if ($IsLinux)   { return "linux" }
        if ($IsMacOS)   { return "macos" }
        throw "Unsupported OS"
    } else {
        # Windows PowerShell 5.x is only on Windows
        return "windows"
    }
}

$platforms = @{
    "macos" = @{
        "arch"    = "arm64"
        "moddir"  = (Join-Path $DATA_DIR "mods")
        "savedir" = $DATA_DIR
        "basedir" = $DATA_DIR
        "romfile" = (Join-Path $DATA_DIR "pd.ntsc-final.z64")
    }
    "linux" = @{
        "arch"    = "x86_64"
        "moddir"  = (Join-Path $DATA_DIR "mods")
        "savedir" = $DATA_DIR
        "basedir" = $DATA_DIR
        "romfile" = (Join-Path $DATA_DIR "pd.ntsc-final.z64")
    }
    "windows" = @{
        "arch"    = "x86_64"
        "moddir"  = (Join-Path $DATA_DIR "mods")
        "savedir" = $DATA_DIR
        "basedir" = $DATA_DIR
        "romfile" = (Join-Path $DATA_DIR "pd.ntsc-final.z64")
    }
}

$os = Get-OS
Write-Host "OS Detected: $os"
$platform = $platforms[$os]

# Set paths from platform configuration
$PD_MODDIR = $platform['moddir']
$PD_SAVEDIR = $platform['savedir']
$PD_BASEDIR = $platform['basedir']
$PD_ROMFILE = $platform['romfile']

# Sanity checks
if (!(Test-Path $PD_MODDIR -PathType Container)) {
    Write-Error "Mod directory not found: $PD_MODDIR"
    exit 1
}
if (!(Test-Path $PD_SAVEDIR -PathType Container)) {
    Write-Error "Save directory not found: $PD_SAVEDIR"
    exit 1
}
if (!(Test-Path $PD_ROMFILE -PathType Leaf)) {
    Write-Error "ROM file not found: $PD_ROMFILE"
    exit 1
}

# Path to executable
$PD_EXECUTABLE = Join-Path $PD "pd.$($platform['arch'])"
# if windows, append .exe
if ($os -eq "windows") {
    $PD_EXECUTABLE = "$PD_EXECUTABLE.exe"
}

if (!(Test-Path $PD_EXECUTABLE -PathType Leaf)) {
    Write-Error "Perfect Dark executable not found at $PD_EXECUTABLE"
    Write-Host "Please build friends of joanna"
    exit 1
}

# Default arguments
$DEFAULT_ARGS = @(
    "--moddir",    "$PD_MODDIR/mod_fojo"
    "--savedir",   $PD_SAVEDIR
    "--basedir",   $PD_BASEDIR
    "--rom-file",  $PD_ROMFILE
)

# Add any extra arguments
$ALL_ARGS = $DEFAULT_ARGS + $args

Write-Host "Executable: $PD_EXECUTABLE"
Write-Host "ROM: $PD_ROMFILE"
Write-Host ""

# Run Perfect Dark using call operator to properly handle paths with spaces
& $PD_EXECUTABLE @ALL_ARGS

