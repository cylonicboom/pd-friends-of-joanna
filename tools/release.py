#!/usr/bin/env python3
"""
Release packaging script for Perfect Dark: Friends of Joanna.

Downloads CI artifacts from a GitHub Actions run, renames binaries with the
release tag, merges in mod files / filetable / launcher / changelog, then
produces one zip per OS/arch ready for upload.

Requirements:
    - gh CLI (https://cli.github.com/) authenticated
    - Python 3.8+

Usage:
    python3 tools/release.py --run 23257285002 --tag v0.2.5
    python3 tools/release.py --run 23257285002 --tag v0.2.5 --skip-download --work-dir ./work
"""

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = "cylonicboom/pd-friends-of-joanna"

# Locate repo root relative to this script (tools/release.py -> repo root)
SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent

# CI artifact names -> platform label
ARTIFACTS = {
    "pd-i686-windows":    {"platform": "windows-i686"},
    "pd-x86_64-windows":  {"platform": "windows-x86_64"},
    "pd-i686-linux":      {"platform": "linux-i686"},
    "pd-x86_64-linux":    {"platform": "linux-x86_64"},
    "pd-x86_64-osx":      {"platform": "macos-x86_64"},
    "pd-arm64-osx":       {"platform": "macos-arm64"},
    "pd-arm64-nswitch":   {"platform": "nswitch-arm64"},
}

FLATPAK_ARTIFACT = "io.github.fgsfdsfgs.perfect_dark.flatpak"

# Mods bundled into every release (from mods dir)
BUNDLED_MODS = ["mod_fojo"]

# Extra files from the repo root to include at the release top level
RELEASE_EXTRAS = ["CHANGELOG.md"]

# Launcher: source file in repo, and the name it gets in the release
LAUNCHER_SRC = "run-fojo-release.ps1"
LAUNCHER_DEST = "run-fojo.ps1"

# Files that must never appear in a release (user save data, ROMs, etc.)
BLACKLISTED_NAMES = {"eeprom.bin", "mpsetups.bin", "pd.ini"}
BLACKLISTED_EXTENSIONS = {".z64", ".pak"}


def is_blacklisted(name):
    """Return True if a filename matches the release blacklist."""
    if name in BLACKLISTED_NAMES:
        return True
    suffix = Path(name).suffix.lower()
    if suffix in BLACKLISTED_EXTENSIONS:
        return True
    return False


def run_cmd(cmd, **kwargs):
    print(f"  $ {' '.join(str(c) for c in cmd)}")
    return subprocess.run(cmd, check=True, **kwargs)


def find_default_appdata():
    if platform.system() == "Darwin":
        return Path.home() / "Library" / "Application Support" / "perfectdark-friends-of-joanna"
    elif platform.system() == "Windows":
        return Path(os.environ.get("APPDATA", "")) / "perfectdark-friends-of-joanna"
    else:
        return Path.home() / ".local" / "share" / "perfectdark-friends-of-joanna"


def download_artifacts(run_id, dest_dir):
    """Download all CI artifacts from a GitHub Actions run using gh CLI."""
    print(f"\nDownloading artifacts from run {run_id}...")
    dest_dir.mkdir(parents=True, exist_ok=True)

    result = subprocess.run(
        ["gh", "api", f"/repos/{REPO}/actions/runs/{run_id}/artifacts", "--paginate"],
        capture_output=True, text=True, check=True
    )
    data = json.loads(result.stdout)
    artifacts = data.get("artifacts", [])

    if not artifacts:
        print("  No artifacts found for this run!")
        sys.exit(1)

    print(f"  Found {len(artifacts)} artifact(s)")

    downloaded = {}
    for artifact in artifacts:
        name = artifact["name"]
        if name not in ARTIFACTS and name != FLATPAK_ARTIFACT:
            print(f"  Skipping unknown artifact: {name}")
            continue

        artifact_dir = dest_dir / name
        print(f"  Downloading {name}...")
        run_cmd(["gh", "run", "download", str(run_id),
                 "-R", REPO, "-n", name, "-D", str(artifact_dir)])
        downloaded[name] = artifact_dir

    return downloaded


def copy_tree_clean(src, dest):
    """Recursively copy src into dest, skipping .DS_Store, .git, and blacklisted files."""
    for item in src.rglob("*"):
        if item.name == ".DS_Store" or ".git" in item.parts:
            continue
        if is_blacklisted(item.name):
            print(f"    SKIP (blacklisted): {item.name}")
            continue
        rel = item.relative_to(src)
        target = dest / rel
        if item.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, target)


def assemble_release_dir(artifact_dir, mods_dir, extras_dir,
                         filetable_dat, launcher_path, is_nswitch=False):
    """
    Restructure a CI artifact directory into the release layout:

        <release>/
        ├── CHANGELOG.md
        ├── run-fojo.ps1
        ├── pd.arm64                    (binaries from CI)
        ├── SDL2.framework/             (macOS only)
        ├── data/
        │   ├── filetable.dat
        │   ├── put_your_rom_here.txt
        │   └── mods/
        │       ├── readme.md
        │       └── mod_fojo/
    """
    if is_nswitch:
        # nswitch: perfectdark/, perfectdark_pal/, perfectdark_jpn/ each get mods
        targets = [d for d in artifact_dir.iterdir()
                   if d.is_dir() and d.name.startswith("perfectdark")]
        for target in targets:
            _populate_data_dir(target / "data", mods_dir, filetable_dat)
    else:
        data_dir = artifact_dir / "data"
        data_dir.mkdir(exist_ok=True)
        _populate_data_dir(data_dir, mods_dir, filetable_dat)

    # Add launcher (renamed) and extras at top level
    if launcher_path and launcher_path.is_file():
        shutil.copy2(launcher_path, artifact_dir / LAUNCHER_DEST)

    for extra in RELEASE_EXTRAS:
        src = extras_dir / extra
        if src.is_file():
            shutil.copy2(src, artifact_dir / src.name)

    # Post-assembly blacklist verification
    violations = []
    for item in artifact_dir.rglob("*"):
        if item.is_file() and is_blacklisted(item.name):
            violations.append(item.relative_to(artifact_dir))
    if violations:
        print("  ERROR: blacklisted files found in release layout:")
        for v in violations:
            print(f"    - {v}")
        sys.exit(1)


def _populate_data_dir(data_dir, mods_dir, filetable_dat):
    """Populate a data/ directory with filetable, mods, and readme."""
    data_dir.mkdir(exist_ok=True)

    # filetable.dat
    if filetable_dat and filetable_dat.is_file():
        shutil.copy2(filetable_dat, data_dir / "filetable.dat")

    # put_your_rom_here.txt (CI already creates this, but ensure it exists)
    rom_hint = data_dir / "put_your_rom_here.txt"
    if not rom_hint.exists():
        rom_hint.write_text("")

    # mods/
    mods_dest = data_dir / "mods"
    mods_dest.mkdir(exist_ok=True)

    # Copy mods readme if present
    readme = mods_dir / "readme.md"
    if readme.is_file():
        shutil.copy2(readme, mods_dest / "readme.md")

    # Bundled mods
    for mod_name in BUNDLED_MODS:
        src = mods_dir / mod_name
        if src.is_dir():
            print(f"    + {mod_name}")
            copy_tree_clean(src, mods_dest / mod_name)


def package_zip(tag, artifact_name, artifact_dir, output_dir):
    """Create a release zip from an assembled artifact directory."""
    plat = ARTIFACTS[artifact_name]["platform"]
    release_name = f"pd-{tag}-{plat}"

    print(f"  Packaging {release_name}.zip...")

    staging = artifact_dir.parent / release_name
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir()

    # Copy contents into staging dir named with release name
    for item in artifact_dir.iterdir():
        dest = staging / item.name
        if item.is_dir():
            shutil.copytree(item, dest)
        else:
            shutil.copy2(item, dest)

    archive_base = str(output_dir / release_name)
    shutil.make_archive(archive_base, "zip",
                        root_dir=str(staging.parent),
                        base_dir=release_name)

    shutil.rmtree(staging)
    return output_dir / f"{release_name}.zip"


def package_flatpak(tag, artifact_dir, output_dir):
    """Copy the flatpak bundle with tag in filename."""
    candidates = list(artifact_dir.rglob("*.flatpak"))
    if not candidates:
        print("  Warning: flatpak bundle not found, skipping")
        return None

    dest = output_dir / f"pd-{tag}.flatpak"
    print(f"  Packaging {dest.name}...")
    shutil.copy2(candidates[0], dest)
    return dest


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser(
        description="Package a Perfect Dark FoJ release from CI artifacts"
    )
    parser.add_argument("--run", required=True,
                        help="GitHub Actions run ID")
    parser.add_argument("--tag", required=True,
                        help="Release tag for filenames (e.g. v0.2.5)")
    parser.add_argument("--appdata", type=Path, default=None,
                        help="Path to app support / data directory containing mods/ "
                             "(default: auto-detect from OS)")
    parser.add_argument("--mods-dir", type=Path, default=None,
                        help="Path to mods/ directory (default: {appdata}/mods)")
    parser.add_argument("--filetable-dat", type=Path, default=None,
                        help="Path to filetable.dat (default: repo root)")
    parser.add_argument("--launcher", type=Path, default=None,
                        help=f"Path to {LAUNCHER_SRC} (default: repo root)")
    parser.add_argument("--output-dir", type=Path, default=None,
                        help="Output directory (default: ./release-{tag})")
    parser.add_argument("--work-dir", type=Path, default=None,
                        help="Working directory for downloads (default: temp)")
    parser.add_argument("--keep-work", action="store_true",
                        help="Keep working directory after packaging")
    parser.add_argument("--skip-download", action="store_true",
                        help="Skip download, reuse existing work-dir")
    args = parser.parse_args()

    # Resolve paths
    appdata = args.appdata or find_default_appdata()
    mods_dir = args.mods_dir or appdata / "mods"
    filetable_dat = args.filetable_dat or REPO_ROOT / "filetable.dat"
    launcher = args.launcher or REPO_ROOT / LAUNCHER_SRC
    output_dir = args.output_dir or Path(f"release-{args.tag}")

    # Validate
    if not mods_dir.is_dir():
        print(f"Error: mods directory not found: {mods_dir}")
        sys.exit(1)

    if not filetable_dat.is_file():
        print(f"Warning: filetable.dat not found at {filetable_dat}, will skip")
        filetable_dat = None

    if not launcher.is_file():
        print(f"Warning: {LAUNCHER_SRC} not found at {launcher}, will skip")
        launcher = None

    if shutil.which("gh") is None:
        print("Error: 'gh' CLI not found. Install from https://cli.github.com/")
        sys.exit(1)

    output_dir.mkdir(parents=True, exist_ok=True)

    if args.work_dir:
        work_dir = args.work_dir
        work_dir.mkdir(parents=True, exist_ok=True)
        cleanup_work = False
    else:
        work_dir = Path(tempfile.mkdtemp(prefix="pd-foj-release-"))
        cleanup_work = not args.keep_work

    print("Configuration:")
    print(f"  Run ID:        {args.run}")
    print(f"  Tag:           {args.tag}")
    print(f"  Mods dir:      {mods_dir}")
    print(f"  Filetable:     {filetable_dat}")
    print(f"  Launcher:      {launcher}")
    print(f"  Output dir:    {output_dir}")
    print(f"  Work dir:      {work_dir}")

    try:
        # 1. Download artifacts
        if args.skip_download:
            print("\nReusing existing work-dir contents...")
            downloaded = {}
            for name in list(ARTIFACTS.keys()) + [FLATPAK_ARTIFACT]:
                d = work_dir / name
                if d.is_dir():
                    downloaded[name] = d
        else:
            downloaded = download_artifacts(args.run, work_dir)

        if not downloaded:
            print("No artifacts to process!")
            sys.exit(1)

        print(f"\n{len(downloaded)} artifact(s) ready")

        # 2. Assemble release layout for each artifact
        print("\nAssembling release directories...")
        for name, artifact_dir in downloaded.items():
            if name == FLATPAK_ARTIFACT:
                continue
            print(f"  {name}:")
            is_nswitch = "nswitch" in name
            assemble_release_dir(
                artifact_dir, mods_dir, REPO_ROOT,
                filetable_dat, launcher, is_nswitch
            )

        # 3. Package zips
        print("\nCreating release zips...")
        release_files = []
        for name, artifact_dir in downloaded.items():
            if name == FLATPAK_ARTIFACT:
                path = package_flatpak(args.tag, artifact_dir, output_dir)
            elif name in ARTIFACTS:
                path = package_zip(args.tag, name, artifact_dir, output_dir)
            else:
                continue
            if path:
                release_files.append(path)

        # 4. Checksums
        print("\nGenerating checksums...")
        checksums_path = output_dir / f"pd-{args.tag}-checksums.sha256"
        with open(checksums_path, "w") as f:
            for rf in sorted(release_files):
                h = sha256_file(rf)
                f.write(f"{h}  {rf.name}\n")
        release_files.append(checksums_path)

        # Summary
        print(f"\nRelease files in {output_dir}/:")
        for rf in sorted(release_files):
            size_mb = rf.stat().st_size / (1024 * 1024)
            print(f"  {rf.name}  ({size_mb:.1f} MB)")
        print(f"\nDone! {len(release_files)} file(s) ready for upload.")

    finally:
        if cleanup_work and work_dir.exists():
            print(f"\nCleaning up: {work_dir}")
            shutil.rmtree(work_dir)


if __name__ == "__main__":
    main()
