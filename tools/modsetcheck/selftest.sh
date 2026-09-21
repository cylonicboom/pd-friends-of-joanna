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

# --- stages ------------------------------------------------------------------
# A second mod whose modconfig declares stages every way a stage can be
# declared, against the first one, so each stage finding fires at least once.
# An unfired check is not a check. The bg_selftest.seg above is 11 bytes, which
# is what makes it a stub here.
mod2="$work/mod_selftest2"
mkdir -p "$mod2/files/bgdata"
printf 'selftest-bg2' > "$mod2/files/bgdata/bg_two.seg"
printf 'selftest-td2' > "$mod2/files/bgdata/bg_two_tilesZ"
printf 'selftest-pd2' > "$mod2/files/bgdata/bg_two_padsZ"

cat > "$work/mod_selftest2_filetable.json" <<'JSON'
{
  "modName": "mod_selftest2",
  "modVersion": "1",
  "description": "synthetic fixture; see selftest.sh",
  "author": "tools/modsetcheck/selftest.sh",
  "files": [
    { "name": "bg_two.seg",    "path": "bgdata/bg_two.seg",    "source": { "self": true } },
    { "name": "bg_two_tilesZ", "path": "bgdata/bg_two_tilesZ", "source": { "self": true } },
    { "name": "bg_two_padsZ",  "path": "bgdata/bg_two_padsZ",  "source": { "self": true } }
  ]
}
JSON

cat > "$mod/modconfig.txt" <<'CFG'
# synthetic fixture
name "selftest"

# positional, by number, on a level the base game ships
stage 0x40 {
  kind none
}

# positional on an EXTRA row with a stub for a background and nothing else named
stage "STAGE_EXTRA1" {
  bgfile "bg_selftest.seg"
  kind mp
}

# the mod's own name, twice over: once here, once in mod_selftest2
stage "shared_level" {
  bgfile "bg_selftest.seg"
  tilesfile "bg_selftest.seg"
  padsfile "bg_selftest.seg"
}
CFG

cat > "$mod2/modconfig.txt" <<'CFG'
name "selftest2"

# the same row as mod_selftest, spelled by name instead of by number
stage "STAGE_TEST_MP8" {
  kind none
}

stage "shared_level" {
  bgfile "bgdata/bg_two.seg"
  tilesfile "bgdata/bg_two_tilesZ"
  padsfile "bgdata/bg_two_padsZ"
  kind mp
  arenaname "Two"
}

# a level whose file is nowhere
stage "lost_level" {
  bgfile "bgdata/bg_nowhere.seg"
  tilesfile "bgdata/bg_two_tilesZ"
  padsfile "bgdata/bg_two_padsZ"
}

# the same level as shared_level, declared again
stage "two_again" {
  bgfile "bgdata/bg_two.seg"
  tilesfile "bgdata/bg_two_tilesZ"
  padsfile "bgdata/bg_two_padsZ"
}

# a STAGE_ constant with no row behind it
stage "STAGE_MP_RANDOM" {
  kind none
}
CFG

# What an earlier boot left in pd.ini: one name allocated onto the row that
# mod_selftest now claims outright, one name nobody declares any more, and one
# past the save field.
cat > "$work/pd.ini" <<'INI'
[Video]
Width=640

[MpStageSlots]
shared_level=5
gone_level=90
far_level=200
INI

"$mkfiletable" mod_selftest2 --workspace "$work" --output "$mod2"

echo "selftest: checking the stage fixtures"
out="$work/stages.out"
"$modsetcheck" "$mod" "$mod2" --ini "$work/pd.ini" --stage-table "$work/stages.json" > "$out" 2>&1 || true
cat "$out"

expect() {
	grep -q -- "$1" "$out" || { echo "selftest: expected a finding matching: $1"; exit 1; }
}

expect 'stages: 8 declared - 1 by number, 2 by row name, 5 by the mod'
expect 'stage 0x40 names a row by number; spell it stage "STAGE_TEST_MP8"'
expect 'stage STAGE_EXTRA1 claims a STAGE_EXTRA row outright'
expect 'stage 0x40 (STAGE_TEST_MP8) is claimed by mod_selftest (modconfig.txt:5, as 0x40) and mod_selftest2 (:4, as STAGE_TEST_MP8)'
expect 'stage "shared_level" is declared by mod_selftest (modconfig.txt:16) and mod_selftest2'
expect 'bgfile "bg_selftest.seg" is 11 bytes - a stub'
expect 'stage STAGE_EXTRA1 names a bgfile but no tilesfile, so it inherits the row'"'"'s FILE_BG_AREC_TILES'
expect 'stage STAGE_EXTRA1 is kind mp with no mpsetupfile, so the arena runs the row'"'"'s FILE_UMP_SETUPAREC'
expect 'bgfile "bgdata/bg_nowhere.seg" is in no mounted mod and is not a vanilla file'
expect 'stage shared_level (modconfig.txt:8) and stage two_again (:24) both use bgfile "bgdata/bg_two.seg"'
expect 'stage "STAGE_MP_RANDOM" is a STAGE_ constant with no stage table row'
expect "\[MpStageSlots\] 'shared_level' (declared by mod_selftest) is reserved at row 0x05, which mod_selftest claims outright at modconfig.txt:10"
expect "\[MpStageSlots\] 'gone_level' holds row 90 (STAGE_EXTRA25) and nothing in this set declares it"
expect "\[MpStageSlots\] 'far_level' holds row 200, past the 127 the save field can store"
expect 'stage "lost_level" has no \[MpStageSlots\] entry yet'
expect '\[MpStageSlots\]: 3 reservations, 2 orphaned, highest row 200 of 127'

[ -s "$work/stages.json" ] || { echo "selftest: no stage table was written"; exit 1; }
python3 - "$work/stages.json" <<'PY' 2>/dev/null || echo "selftest: (no python3, stage table not parsed)"
import json, sys
t = json.load(open(sys.argv[1]))
assert len(t["stages"]) == 8, len(t["stages"])
assert len(t["rows"]) >= 119, len(t["rows"])
byspec = {s["spec"]: s for s in t["stages"] if s["source"] == "mod_selftest"}
assert byspec["shared_level"]["stagenum"] == "0x05" and byspec["shared_level"]["stagenumFrom"] == "MpStageSlots"
assert byspec["0x40"]["row"] == "STAGE_TEST_MP8"
assert [r["name"] for r in t["reservations"]] == ["shared_level", "gone_level", "far_level"]
print("selftest: stage table parses and says what the fixtures say")
PY

echo "selftest: ok"
