#!/bin/sh
# selftest — build a synthetic mod and check it, with no mod data on disk.
#
# WHY A FIXTURE AND NOT THE REAL ROSTER. The roster lives in pd-fojo-basedir,
# which is a separate and private repository; this one has no mods in it, so CI
# here cannot check the set that actually ships. That check belongs where the
# data is. What CI can do is prove the tools still build and still agree with
# each other, which is the failure that has actually bitten: a filetable written
# at one version and read by a binary built before it, where the reader's own
# bounds check is the only thing between you and a silent desync.
#
# So this writes a mod by hand, builds its table with mkfiletable, and reads it
# back with modsetcheck. Both binaries must be built already.
#
#   sh selftest.sh [<scratch-dir>]
set -eu

here=$(cd "$(dirname "$0")" && pwd)
mkfiletable="$here/../mkfiletable/mkfiletable"
modsetcheck="$here/modsetcheck"

for tool in "$mkfiletable" "$modsetcheck"; do
	[ -x "$tool" ] || { echo "selftest: $tool is not built"; exit 1; }
done

work=${1:-$(mktemp -d)}
mod="$work/mod_selftest"
mkdir -p "$mod/files/textures"

# Bytes for the entries to point at. Content is irrelevant; existence is not -
# mkfiletable checks a self source resolves to a real non-empty file, which is
# the whole point of declaring one.
printf 'selftest-bg' > "$mod/files/bg_selftest.seg"
printf 'selftest-td' > "$mod/files/textures/00af.bin"
printf 'selftest-md' > "$mod/files/textures/0116.bin"
printf 'selftest-pm' > "$mod/files/Pmodel_selftestZ"

# Three texture names, one per shape modTextureResolveFileDetailed() composes,
# so a change to the name rules that drops a shape fails here rather than in
# somebody's game. The alias on the bg entry forces the table to v4, which is
# what makes this a version-bump regression test as well as a build one.
cat > "$work/mod_selftest_filetable.json" <<'JSON'
{
  "modName": "mod_selftest",
  "modVersion": "1",
  "description": "synthetic fixture; see selftest.sh",
  "author": "tools/modsetcheck/selftest.sh",
  "files": [
    {
      "name": "bg_selftest.seg",
      "path": "bg_selftest.seg",
      "description": "aliased so the table is forced to version 4",
      "source": { "self": true, "alias": "bg_old.seg" }
    },
    {
      "name": "Pmodel_selftestZ",
      "path": "Pmodel_selftestZ",
      "source": { "self": true }
    },
    {
      "name": "00af.bin",
      "path": "textures/00af.bin",
      "type": "texture",
      "textureId": "0x00af",
      "source": { "self": true }
    },
    {
      "name": "selftest_00af.bin",
      "path": "textures/00af.bin",
      "type": "texture",
      "textureId": "0x00b0",
      "source": { "self": true }
    },
    {
      "name": "Pmodel_selftestZ/0116.bin",
      "path": "textures/0116.bin",
      "type": "texture",
      "textureId": "0x0116",
      "model": "Pmodel_selftestZ",
      "source": { "self": true }
    }
  ]
}
JSON

cat > "$mod/modconfig.txt" <<'CFG'
# synthetic fixture
name "selftest"
CFG

echo "selftest: building $mod"
"$mkfiletable" mod_selftest --workspace "$work" --output "$mod"

[ -s "$mod/filetable.dat" ] || { echo "selftest: no filetable.dat was written"; exit 1; }

# "PDFT" at 0, then the version at 4 - pdftWrite() writes them in that order.
magic=$(dd if="$mod/filetable.dat" bs=1 count=4 2>/dev/null)
[ "$magic" = "PDFT" ] || { echo "selftest: bad magic '$magic'"; exit 1; }

# Compared as bytes, not as a host integer: the format is big-endian, so an
# od -tu4 on x86 reads version 4 as 67108864 and "passes" nothing.
ver=$(od -An -tx1 -j4 -N4 "$mod/filetable.dat" | tr -s ' ' | sed 's/^ //;s/ $//')
echo "selftest: filetable.dat version bytes are $ver"
[ "$ver" = "00 00 00 04" ] || { echo "selftest: expected version 4, got bytes $ver"; exit 1; }

echo "selftest: checking it back"
"$modsetcheck" "$mod"

echo "selftest: ok"
