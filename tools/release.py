#!/usr/bin/env python3
"""
Release packager for Perfect Dark: Friends of Joanna.

Supports two input modes:
1) Local binaries (manual --*-bin args)
2) CI artifact download from a GitHub Actions run (--run)

Packaging model is basedir-first (portable layout):
- Copy prepared basedir into a staging release root
- Copy target binary into release root
- Merge optional runtime files (frameworks/DLLs/etc)
- Zip one archive per target + SHA256 checksums

Supported targets only:
- windows-x86_64
- linux-x86_64
- macos-x86_64
- macos-arm64

Unsupported targets are intentionally excluded:
- Nintendo Switch
- 32-bit Windows/Linux
"""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import textwrap
from pathlib import Path
from typing import Dict, Iterable, Optional, Set, Tuple

# Locate repo root relative to this script (tools/release.py -> repo root)
SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent

DEFAULT_REPO = "cylonicboom/pd-friends-of-joanna"
DEFAULT_CONFIG_PATH = SCRIPT_DIR / "release.config.json"

# Extra files from repo root to include at release top level if present.
RELEASE_EXTRAS = ["CHANGELOG.md"]

# Files that must never appear in release zips.
BLACKLISTED_NAMES = {"eeprom.bin", "mpsetups.bin", "pd.ini"}
BLACKLISTED_EXTENSIONS = {".z64", ".pak"}
REGION_BINARY_TOKENS = {"jpn", "pal"}
REGION_BINARY_EXTENSIONS = {".z64", ".n64", ".v64", ".bin", ".exe"}

# Target metadata.
TARGETS = {
    "windows-x86_64": {
        "bin_arg": "win64_bin",
        "extra_arg": "win64_extra",
        "ci_artifact": "pd-x86_64-windows",
        "ci_primary_binary": "pd.x86_64.exe",
    },
    "linux-x86_64": {
        "bin_arg": "linux64_bin",
        "extra_arg": "linux64_extra",
        "ci_artifact": "pd-x86_64-linux",
        "ci_primary_binary": "pd.x86_64",
    },
    "macos-x86_64": {
        "bin_arg": "macos_x64_bin",
        "extra_arg": "macos_x64_extra",
        "ci_artifact": "pd-x86_64-osx",
        "ci_primary_binary": "pd.x86_64",
    },
    "macos-arm64": {
        "bin_arg": "macos_arm64_bin",
        "extra_arg": "macos_arm64_extra",
        "ci_artifact": "pd-arm64-osx",
        "ci_primary_binary": "pd.arm64",
    },
}

CI_ARTIFACT_TO_TARGET = {
    meta["ci_artifact"]: target
    for target, meta in TARGETS.items()
}


def is_blacklisted(name: str) -> bool:
    lower_name = name.lower()

    if lower_name in BLACKLISTED_NAMES:
        return True

    if Path(name).suffix.lower() in BLACKLISTED_EXTENSIONS:
        return True

    # Exclude JPN/PAL game binaries from release bundles for now.
    has_region_token = any(token in lower_name for token in REGION_BINARY_TOKENS)
    if has_region_token:
        if lower_name.startswith("pd."):
            return True
        if Path(lower_name).suffix.lower() in REGION_BINARY_EXTENSIONS:
            return True

    return False


def run_cmd(cmd: Iterable[str], **kwargs):
    printable = " ".join(str(c) for c in cmd)
    print(f"  $ {printable}")
    return subprocess.run(list(cmd), check=True, **kwargs)


def copy_tree_clean(src: Path, dest: Path) -> None:
    """Copy src recursively into dest while filtering unsafe/unwanted files."""
    for root, dirnames, filenames in os.walk(src, followlinks=False):
        root_path = Path(root)

        # Prevent traversal into symlinked and ignored directories.
        filtered_dirs = []
        for dirname in dirnames:
            dpath = root_path / dirname
            if dpath.is_symlink():
                print(f"    SKIP (symlink): {dpath}")
                continue
            if dirname in {".git", "tools"}:
                print(f"    SKIP (excluded dir): {dpath}")
                continue
            filtered_dirs.append(dirname)
        dirnames[:] = filtered_dirs

        for filename in filenames:
            item = root_path / filename

            if item.is_symlink():
                print(f"    SKIP (symlink): {item}")
                continue

            if item.name == ".DS_Store" or ".git" in item.parts:
                continue

            if is_blacklisted(item.name):
                print(f"    SKIP (blacklisted): {item}")
                continue

            rel = item.relative_to(src)
            target = dest / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, target)


def merge_dir(extra_dir: Path, release_root: Path, skip_names: Optional[Set[str]] = None) -> None:
    """Merge contents of extra_dir into release_root, skipping selected top-level names."""
    if not extra_dir.is_dir():
        print(f"  ERROR: extra dir not found: {extra_dir}")
        sys.exit(1)

    skip_names = skip_names or set()

    for item in extra_dir.iterdir():
        if item.name in skip_names:
            continue

        if item.is_symlink():
            print(f"    SKIP (symlink): {item}")
            continue

        if is_blacklisted(item.name):
            print(f"    SKIP (blacklisted): {item}")
            continue

        dest = release_root / item.name

        if item.is_dir():
            if dest.exists():
                shutil.rmtree(dest)
            dest.mkdir(parents=True, exist_ok=True)
            copy_tree_clean(item, dest)
        else:
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, dest)


def verify_no_blacklist(root: Path) -> None:
    violations = []
    for item in root.rglob("*"):
        if item.is_file() and is_blacklisted(item.name):
            violations.append(item.relative_to(root))

    if violations:
        print("  ERROR: blacklisted files found in release layout:")
        for v in violations:
            print(f"    - {v}")
        sys.exit(1)


def package_target(
    tag: str,
    platform_name: str,
    basedir: Path,
    binary_path: Path,
    output_dir: Path,
    extras_dir: Path,
    merge_dirs: Iterable[Tuple[Path, Set[str]]],
) -> Path:
    """Build one target layout, zip it, and return resulting zip path."""
    if not basedir.is_dir():
        print(f"ERROR: basedir not found: {basedir}")
        sys.exit(1)

    if not binary_path.is_file():
        print(f"ERROR: binary not found for {platform_name}: {binary_path}")
        sys.exit(1)

    release_name = f"pd-friends-of-joanna-{tag}-{platform_name}"
    print(f"\nPackaging {release_name}...")

    with tempfile.TemporaryDirectory(prefix=f"pd-foj-{platform_name}-") as td:
        temp_root = Path(td)
        staging = temp_root / release_name
        staging.mkdir(parents=True, exist_ok=True)

        # Start from portable basedir.
        copy_tree_clean(basedir, staging)

        # Ensure platform binary is present in release root.
        shutil.copy2(binary_path, staging / binary_path.name)

        # Merge optional runtime dirs.
        for merge_dir_path, skip_names in merge_dirs:
            print(f"  Merging runtime files: {merge_dir_path}")
            merge_dir(merge_dir_path, staging, skip_names=skip_names)

        for extra in RELEASE_EXTRAS:
            src = extras_dir / extra
            if src.is_file():
                shutil.copy2(src, staging / src.name)

        verify_no_blacklist(staging)

        archive_base = output_dir / release_name
        shutil.make_archive(str(archive_base), "zip", root_dir=str(temp_root), base_dir=release_name)

    return output_dir / f"{release_name}.zip"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


def maybe_resolve(path: Optional[Path]) -> Optional[Path]:
    if path is None:
        return None
    return path.expanduser().resolve()


def path_value(value) -> Optional[Path]:
    if value is None:
        return None
    if isinstance(value, Path):
        return maybe_resolve(value)
    return maybe_resolve(Path(value))


def load_release_config(path: Path) -> dict:
    if not path.exists():
        return {}

    try:
        with path.open("r", encoding="utf-8") as f:
            data = json.load(f)
    except json.JSONDecodeError as e:
        print(f"ERROR: invalid JSON in config file {path}: {e}")
        sys.exit(1)

    if not isinstance(data, dict):
        print(f"ERROR: config file must contain a JSON object: {path}")
        sys.exit(1)

    return data


def discover_ci_artifacts(run_id: str, repo: str) -> Dict[str, dict]:
    """Return artifact metadata keyed by artifact name from a run."""
    try:
        result = subprocess.run(
            ["gh", "api", f"/repos/{repo}/actions/runs/{run_id}/artifacts", "--paginate"],
            capture_output=True,
            text=True,
            check=True,
        )
    except subprocess.CalledProcessError as e:
        stderr = (e.stderr or "").strip()
        stdout = (e.stdout or "").strip()
        print("ERROR: failed to query GitHub Actions artifacts via gh CLI.")
        print(f"  Repo: {repo}")
        print(f"  Run:  {run_id}")
        if stderr:
            print(f"  gh stderr: {stderr}")
        elif stdout:
            print(f"  gh output: {stdout}")
        print("Hint: run 'gh auth status' and ensure this run/repo is accessible.")
        sys.exit(1)

    data = json.loads(result.stdout)
    artifacts = data.get("artifacts", [])
    return {a["name"]: a for a in artifacts}


def download_ci_artifacts(
    run_id: str,
    repo: str,
    work_dir: Path,
) -> Dict[str, Path]:
    """Download supported artifacts; returns target->artifact_dir mapping."""
    print(f"\nDiscovering artifacts for run {run_id} in {repo}...")
    artifact_map = discover_ci_artifacts(run_id, repo)

    selected = {}
    for artifact_name, target in CI_ARTIFACT_TO_TARGET.items():
        if artifact_name in artifact_map:
            selected[target] = artifact_name

    if not selected:
        print("ERROR: no supported artifacts found in this run.")
        print("Expected one or more of:")
        for name in sorted(CI_ARTIFACT_TO_TARGET.keys()):
            print(f"  - {name}")
        sys.exit(1)

    print("  Supported artifacts found:")
    for target, artifact_name in sorted(selected.items()):
        print(f"    - {target}: {artifact_name}")

    target_dirs: Dict[str, Path] = {}
    for target, artifact_name in sorted(selected.items()):
        out_dir = work_dir / artifact_name
        out_dir.mkdir(parents=True, exist_ok=True)

        print(f"  Downloading {artifact_name}...")
        run_cmd([
            "gh",
            "run",
            "download",
            str(run_id),
            "-R",
            repo,
            "-n",
            artifact_name,
            "-D",
            str(out_dir),
        ])

        target_dirs[target] = out_dir

    return target_dirs


def resolve_ci_binary(artifact_dir: Path, target: str) -> Path:
    expected = TARGETS[target]["ci_primary_binary"]
    candidate = artifact_dir / expected

    if candidate.is_file():
        return candidate

    print(f"ERROR: expected CI binary not found for {target}: {candidate}")
    sys.exit(1)


def package_all_artifacts(tag: str, output_dir: Path, artifact_paths: Iterable[Path]) -> Path:
    """Create one max-compression tar.xz containing all generated release artifacts."""
    tar_name = f"pd-friends-of-joanna-{tag}.tar.xz"
    tar_path = output_dir / tar_name

    # Only include files in output_dir, referenced by basename.
    members = sorted([p.name for p in artifact_paths if p.is_file()])

    if not members:
        print("ERROR: no artifact files available to bundle into tar.xz")
        sys.exit(1)

    if tar_path.exists():
        tar_path.unlink()

    env = os.environ.copy()
    env["XZ_OPT"] = "-9e -T0"

    cmd = ["tar", "-cJf", str(tar_path), "-C", str(output_dir)]
    cmd.extend(members)
    run_cmd(cmd, env=env)

    return tar_path


def main() -> None:
    help_epilog = textwrap.dedent(
        """
        Modes:
             0) Config defaults
                 - Script loads defaults from --config (default: tools/release.config.json).
                 - CLI args override config values.

          1) Local binaries only
             - Provide one or more --*-bin paths.
             - Optional --*-extra paths are merged into the release root.

          2) CI run only
             - Provide --run <id> (and optional --repo).
             - Script downloads supported artifacts and auto-detects binaries.
             - Artifact runtime files are merged, but artifact data/ is skipped.

          3) Hybrid CI + local override
             - Provide --run plus any --*-bin overrides.
             - Local binary path wins for that target.

        Supported targets:
          windows-x86_64, linux-x86_64, macos-x86_64, macos-arm64

        Examples:
                    Run with config defaults only:
                        python3 tools/release.py

          Local packaging (single target):
            python3 tools/release.py \
              --tag v0.3.1 \
              --basedir ../pd-fojo-v.3.1-basedir \
              --linux64-bin ./build/pd.x86_64

          CI packaging (all supported artifacts found in run):
            python3 tools/release.py \
              --tag v0.3.1 \
              --run 30497971805 \
              --basedir ../pd-fojo-v.3.1-basedir

          CI packaging with local override for one target:
            python3 tools/release.py \
              --tag v0.3.1 \
              --run 30497971805 \
              --basedir ../pd-fojo-v.3.1-basedir \
              --macos-arm64-bin ./build/pd.arm64
        """
    ).strip()

    parser = argparse.ArgumentParser(
        description="Package Friends of Joanna releases from basedir + local binaries and/or CI artifacts",
        epilog=help_epilog,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    parser.add_argument(
        "--config",
        type=Path,
        default=DEFAULT_CONFIG_PATH,
        help="Path to JSON config file (default: tools/release.config.json)",
    )
    parser.add_argument("--tag", default=None, help="Release tag for filenames (e.g. v0.3.1)")
    parser.add_argument(
        "--basedir",
        type=Path,
        default=None,
        help="Portable basedir root (default: ../pd-fojo-v.3.1-basedir)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=None,
        help="Output directory for release zips (default: ./release-{tag})",
    )

    parser.add_argument("--run", default=None, help="GitHub Actions run ID to download build artifacts from")
    parser.add_argument("--repo", default=None, help=f"GitHub repo owner/name (default: {DEFAULT_REPO})")
    parser.add_argument("--work-dir", type=Path, default=None, help="Artifact download dir (default: temp)")
    parser.add_argument("--keep-work", action="store_true", help="Keep temporary work dir after completion")

    parser.add_argument("--win64-bin", type=Path, default=None, help="Path to 64-bit Windows binary")
    parser.add_argument("--linux64-bin", type=Path, default=None, help="Path to 64-bit Linux binary")
    parser.add_argument("--macos-x64-bin", type=Path, default=None, help="Path to macOS x86_64 binary")
    parser.add_argument("--macos-arm64-bin", type=Path, default=None, help="Path to macOS arm64 binary")

    parser.add_argument("--win64-extra", type=Path, default=None, help="Extra runtime dir to merge for windows-x86_64")
    parser.add_argument("--linux64-extra", type=Path, default=None, help="Extra runtime dir to merge for linux-x86_64")
    parser.add_argument(
        "--macos-x64-extra",
        type=Path,
        default=None,
        help="Extra runtime dir to merge for macos-x86_64 (e.g. SDL2.framework)",
    )
    parser.add_argument(
        "--macos-arm64-extra",
        type=Path,
        default=None,
        help="Extra runtime dir to merge for macos-arm64 (e.g. SDL2.framework)",
    )

    args = parser.parse_args()

    config_path = path_value(args.config)
    assert config_path is not None
    config = load_release_config(config_path)

    tag = args.tag or config.get("tag")
    if not tag:
        print("ERROR: missing tag. Provide --tag or set 'tag' in release config.")
        print(f"Config path: {config_path}")
        sys.exit(1)

    basedir = path_value(args.basedir or config.get("basedir") or (REPO_ROOT.parent / "pd-fojo-v.3.1-basedir"))
    output_dir_raw = args.output_dir or config.get("output_dir")
    output_dir = path_value(output_dir_raw) if output_dir_raw else Path(f"release-{tag}").resolve()
    run_id = args.run or config.get("run")
    repo = args.repo or config.get("repo") or DEFAULT_REPO
    keep_work = bool(config.get("keep_work", False)) or args.keep_work

    output_dir.mkdir(parents=True, exist_ok=True)

    target_bins = {
        "windows-x86_64": path_value(args.win64_bin or config.get("win64_bin")),
        "linux-x86_64": path_value(args.linux64_bin or config.get("linux64_bin")),
        "macos-x86_64": path_value(args.macos_x64_bin or config.get("macos_x64_bin")),
        "macos-arm64": path_value(args.macos_arm64_bin or config.get("macos_arm64_bin")),
    }

    target_merge_dirs: Dict[str, list] = {
        "windows-x86_64": [],
        "linux-x86_64": [],
        "macos-x86_64": [],
        "macos-arm64": [],
    }

    manual_extras = {
        "windows-x86_64": path_value(args.win64_extra or config.get("win64_extra")),
        "linux-x86_64": path_value(args.linux64_extra or config.get("linux64_extra")),
        "macos-x86_64": path_value(args.macos_x64_extra or config.get("macos_x64_extra")),
        "macos-arm64": path_value(args.macos_arm64_extra or config.get("macos_arm64_extra")),
    }

    for target, extra_dir in manual_extras.items():
        if extra_dir:
            target_merge_dirs[target].append((extra_dir, set()))

    cleanup_work = False
    work_dir: Optional[Path] = None

    if run_id:
        if shutil.which("gh") is None:
            print("ERROR: gh CLI is required for --run mode. Install from https://cli.github.com/")
            sys.exit(1)

        work_dir_input = args.work_dir or config.get("work_dir")

        if work_dir_input:
            work_dir = path_value(work_dir_input)
            assert work_dir is not None
            work_dir.mkdir(parents=True, exist_ok=True)
        else:
            work_dir = Path(tempfile.mkdtemp(prefix="pd-foj-release-ci-"))
            cleanup_work = not keep_work

        ci_artifacts = download_ci_artifacts(run_id, repo, work_dir)

        for target, artifact_dir in ci_artifacts.items():
            if target_bins[target] is None:
                target_bins[target] = resolve_ci_binary(artifact_dir, target)
            target_merge_dirs[target].append((artifact_dir, {"data"}))

    selected_targets = [(name, bpath) for name, bpath in target_bins.items() if bpath is not None]

    if not selected_targets:
        print("ERROR: no targets selected.")
        print("Provide one or more --*-bin args, or pass --run for CI artifacts.")
        sys.exit(1)

    print("Configuration:")
    print(f"  Config:         {config_path}")
    print(f"  Tag:            {tag}")
    print(f"  Basedir:        {basedir}")
    print(f"  Output dir:     {output_dir}")
    if run_id:
        print(f"  CI run:         {run_id}")
        print(f"  CI repo:        {repo}")
        print(f"  CI work dir:    {work_dir}")

    print("  Targets:")
    for target, bin_path in selected_targets:
        print(f"    - {target}: {bin_path}")
        for merge_dir_path, skip_names in target_merge_dirs[target]:
            if skip_names:
                print(f"      merge: {merge_dir_path} (skip: {sorted(skip_names)})")
            else:
                print(f"      merge: {merge_dir_path}")

    release_files = []

    try:
        for platform_name, binary_path in selected_targets:
            assert binary_path is not None
            zip_path = package_target(
                tag=tag,
                platform_name=platform_name,
                basedir=basedir,
                binary_path=binary_path,
                output_dir=output_dir,
                extras_dir=REPO_ROOT,
                merge_dirs=target_merge_dirs[platform_name],
            )
            release_files.append(zip_path)

        checksums_path = output_dir / f"pd-friends-of-joanna-{tag}-checksums.sha256"
        with checksums_path.open("w", encoding="utf-8") as f:
            for rf in sorted(release_files):
                f.write(f"{sha256_file(rf)}  {rf.name}\n")
        release_files.append(checksums_path)

        artifacts_tar_path = package_all_artifacts(tag, output_dir, release_files)
        release_files.append(artifacts_tar_path)

        print(f"\nRelease files in {output_dir}/:")
        for rf in sorted(release_files):
            size_mb = rf.stat().st_size / (1024 * 1024)
            print(f"  {rf.name} ({size_mb:.1f} MB)")

        print(f"\nDone! {len(release_files)} file(s) ready for upload.")

    finally:
        if cleanup_work and work_dir and work_dir.exists():
            print(f"\nCleaning up: {work_dir}")
            shutil.rmtree(work_dir)


if __name__ == "__main__":
    main()
