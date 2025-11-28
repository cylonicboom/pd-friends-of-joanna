#!/usr/bin/env pwsh
# Perfect Dark Friends of Joanna runner script (PowerShell)
# Cross-platform: Windows, Linux, MacOS
#

# Get script directory
$PD = Split-Path -Parent $MyInvocation.MyCommand.Definition

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
        "moddir"  = "$HOME/Library/Application Support/perfectdark-friends-of-joanna/mods"
        "savedir" = "$HOME/Library/Application Support/perfectdark-friends-of-joanna/"
        "romfile" = "$HOME/Library/Application Support/perfectdark-friends-of-joanna/pd.ntsc-final.z64"
    }
    "linux" = @{
        "arch"    = "x86_64"
        "moddir"  = "$HOME/.local/share/perfectdark-friends-of-joanna/mods"
        "savedir" = "$HOME/.local/share/perfectdark-friends-of-joanna/"
        "romfile" = "$HOME/.local/share/perfectdark-friends-of-joanna/pd.ntsc-final.z64"
    }
    "windows" = @{
        "arch"    = "amd64"
        "moddir"  = "$env:APPDATA\perfectdark-friends-of-joanna\mods"
        "savedir" = "$env:APPDATA\perfectdark-friends-of-joanna\"
        "romfile" = "$env:APPDATA\perfectdark-friends-of-joanna\pd.ntsc-final.z64"
    }
}

$os = Get-OS
Write-Host "OS Detected: $os"
$platform = $platforms[$os]

# detect OS here and fill out env variables
if (-not $env:PD_MODDIR) { $env:PD_MODDIR = $platform['moddir'] }
if (-not $env:PD_SAVEDIR) { $env:PD_SAVEDIR = $platform['savedir'] }
if (-not $env:PD_BASEDIR) { $env:PD_BASEDIR = $platform['basedir'] }
if (-not $env:PD_ROMFILE) { $env:PD_ROMFILE = $platform['romfile'] }

# Sanity checks
if (!(Test-Path $env:PD_MODDIR -PathType Container)) {
    Write-Error "Mod directory not found: $env:PD_MODDIR"
    exit 1
}
if (!(Test-Path $env:PD_SAVEDIR -PathType Container)) {
    Write-Error "Save directory not found: $env:PD_SAVEDIR"
    exit 1
}
if (!(Test-Path $env:PD_ROMFILE -PathType Leaf)) {
    Write-Error "ROM file not found: $env:PD_ROMFILE"
    exit 1
}

# Path to executable
$PD_EXECUTABLE = Join-Path $PD "build/pd.x86_64"

if (!(Test-Path $PD_EXECUTABLE -PathType Leaf)) {
    Write-Error "Perfect Dark executable not found at $PD_EXECUTABLE"
    Write-Host "Please build friends of joanna"
    exit 1
}

# Default arguments
$DEFAULT_ARGS = @(
    "--moddir", "$($env:PD_MODDIR)/mod_aio",
    "--moddir", "$($env:PD_MODDIR)/mod_gex",
    "--moddir", "$($env:PD_MODDIR)/mod_kakariko",
    "--moddir", "$($env:PD_MODDIR)/mod_dark_noon",
    "--moddir", "$($env:PD_MODDIR)/mod_goldfinger_64",
    "--moddir", "$($env:PD_MODDIR)/mod_fojo",
    "--savedir", $env:PD_SAVEDIR,
    "--basedir", $env:PD_BASEDIR,
    "--rom-file", $env:PD_ROMFILE
)

# Add any extra arguments
$ARGS = $DEFAULT_ARGS + $args

# Write-Host "Starting Perfect Dark with args: $($ARGS -join ' ')"
Write-Host "Executable: $PD_EXECUTABLE"
Write-Host "ROM: $($env:PD_ROMFILE)"
Write-Host ""

# Run Perfect Dark
iex "$(Resolve-Path $PD_EXECUTABLE) $ARGS"
