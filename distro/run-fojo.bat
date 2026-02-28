@echo off
REM Perfect Dark Friends of Joanna launcher for Windows
set DISTRO=%~dp0
"%DISTRO%pd.x86_64.exe" --moddir "%DISTRO%data\mods\mod_fojo" --savedir "%DISTRO%data" --basedir "%DISTRO%data" --rom-file "%DISTRO%data\pd.ntsc-final.z64" %*
