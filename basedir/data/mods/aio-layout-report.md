# AIO 6.0 layout report

source: ../pd-aio-6.0 | packs: mod_allinone, mod_dark_noon, mod_gex, mod_goldfinger_64, mod_kakariko
files hashed: 6560 | distinct rel paths: 2176 | identical in all 5 packs: 990
shared-identical by class: {'tex': 752, 'G': 68, 'C': 166, 'seq': 3, 'P': 1}
in all packs but DIFFERING somewhere: 1 ['textures/0b69.bin']

## mod_aio_characters
texture census: 196 models reference 843 distinct ids; 719 carried (shipped by some pack), 124 are vanilla ids no pack ships
  40 textures every pack ships identically are referenced by no carried model and were left out
entries: 147 (heads 79, bodies 68)
models carried: 196 | textures: 719 | sequences: 3
held back: 40
  - Cdark_af1_jpZ — JP/US Joanna variant: fojo serves these by rom mount (mikado); not carried
  - Cdark_combat_jpZ — JP/US Joanna variant: fojo serves these by rom mount (mikado); not carried
  - Cdark_frock_jpZ — JP/US Joanna variant: fojo serves these by rom mount (mikado); not carried
  - Cdark_leather_jpZ — JP/US Joanna variant: fojo serves these by rom mount (mikado); not carried
  - Cdark_negotiator_jpZ — JP/US Joanna variant: fojo serves these by rom mount (mikado); not carried
  - Cdark_ripped_jpZ — JP/US Joanna variant: fojo serves these by rom mount (mikado); not carried
  - Cdarksnow_jpZ — JP/US Joanna variant: fojo serves these by rom mount (mikado); not carried
  - Chead_vdZ — vanilla name: would REPLACE the base rom file for every mod; not carried
  - CheadbiotechZ — vanilla name: would REPLACE the base rom file for every mod; not carried
  - CheadcassandraZ — vanilla name: would REPLACE the base rom file for every mod; not carried
  - Cheaddark_combat_jpZ — JP/US Joanna variant: fojo serves these by rom mount (mikado); not carried
  - Cheaddark_frock_jpZ — JP/US Joanna variant: fojo serves these by rom mount (mikado); not carried
  - Cheaddark_snow_jpZ — JP/US Joanna variant: fojo serves these by rom mount (mikado); not carried
  - Cheaddark_usZ — JP/US Joanna variant: fojo serves these by rom mount (mikado); not carried
  - Cheaddarkaqua_jpZ — JP/US Joanna variant: fojo serves these by rom mount (mikado); not carried
  - CheaddavecZ — vanilla name: would REPLACE the base rom file for every mod; not carried
  - CheadfoslerferZ — Poplin test head; never copied (her ruling, c-public-test-aio-prepare)
  - CheadgreyZ — vanilla name: would REPLACE the base rom file for every mod; not carried
  - Cheadpeer_sZ — vanilla name: would REPLACE the base rom file for every mod; not carried
  - CstewardZ — vanilla name: would REPLACE the base rom file for every mod; not carried
  - GcommsuplinkZ — vanilla name: would REPLACE the base rom file for every mod; not carried
  - GdyrocketZ — vanilla name: would REPLACE the base rom file for every mod; not carried
  - Ghand_a51guardZ — vanilla name: would REPLACE the base rom file for every mod; not carried
  - Ghand_cifemtechZ — vanilla name: would REPLACE the base rom file for every mod; not carried
  - Ghand_cilabtechZ — AIO re-hands the vanilla body BODY_CILABTECH with it (a vanilla-row change); not carried
  - Ghand_dd_guardZ — AIO re-hands the vanilla body BODY_DD_GUARD with it (a vanilla-row change); not carried
  - Ghand_ddbioZ — vanilla name: would REPLACE the base rom file for every mod; not carried
  - Ghand_ddfodderZ — vanilla name: would REPLACE the base rom file for every mod; not carried
  - Ghand_ddsniperZ — vanilla name: would REPLACE the base rom file for every mod; not carried
  - Ghand_labtechZ — AIO re-hands the vanilla body BODY_DARKLAB with it (a vanilla-row change); not carried
  - Ghand_nsa_lackeyZ — AIO re-hands the vanilla body BODY_NSA_LACKEY with it (a vanilla-row change); not carried
  - Ghand_pres_securityZ — AIO re-hands the vanilla body BODY_PRES_SECURITY with it (a vanilla-row change); not carried
  - Ghand_presidentZ — vanilla name: would REPLACE the base rom file for every mod; not carried
  - Ghand_santa_skinnyZ — no g_HeadsAndBodies row >= 0x98 references it; not carried
  - Ghand_stewardZ — AIO re-hands the vanilla body BODY_STEWARD with it (a vanilla-row change); not carried
  - Ghand_stewardess_coatZ — vanilla name: would REPLACE the base rom file for every mod; not carried
  - Ghand_vriesZ — vanilla name: would REPLACE the base rom file for every mod; not carried
  - Gm16Z — vanilla name: would REPLACE the base rom file for every mod; not carried
  - Gmp5kZ — vanilla name: would REPLACE the base rom file for every mod; not carried
  - GskorpionZ — vanilla name: would REPLACE the base rom file for every mod; not carried

## mod_aio_pdplus (from mod_allinone, stage blocks from tools/aio/stages/mod_aio_pdplus.txt)
carried: {'P': 6, 'U': 20, 'bgdata': 50, 'tex': 764} | non-vanilla names among them: none
L* banks NOT carried (they overwrite live vanilla text slots): ['LmpmenuE', 'LmpmenuJ', 'LmpmenuP', 'LoptionsE', 'LoptionsJ', 'LoptionsP']
levels shipped: 19 | named by a bgfile in a stage block: 19 | NOT claimed by any block: none

## mod_aio_gex (from mod_gex, stage blocks from tools/aio/stages/mod_aio_gex.txt)
carried: {'P': 118, 'U': 35, 'bgdata': 105, 'tex': 1486} | non-vanilla names among them: none
levels shipped: 35 | named by a bgfile in a stage block: 32 | NOT claimed by any block: ['bg_cryp', 'bg_mp17', 'bg_mp6']

## mod_aio_kakariko (from mod_kakariko, stage blocks from tools/aio/stages/mod_aio_kakariko.txt)
carried: {'P': 7, 'U': 4, 'bgdata': 12, 'tex': 987} | non-vanilla names among them: none
levels shipped: 4 | named by a bgfile in a stage block: 4 | NOT claimed by any block: none

## mod_aio_darknoon (from mod_dark_noon, stage blocks from tools/aio/stages/mod_aio_darknoon.txt)
carried: {'P': 5, 'U': 4, 'bgdata': 12, 'tex': 887} | non-vanilla names among them: none
levels shipped: 4 | named by a bgfile in a stage block: 4 | NOT claimed by any block: none

## mod_aio_goldfinger (from mod_goldfinger_64, stage blocks from tools/aio/stages/mod_aio_goldfinger.txt)
carried: {'P': 5, 'U': 4, 'bgdata': 12, 'tex': 844} | non-vanilla names among them: none
levels shipped: 4 | named by a bgfile in a stage block: 4 | NOT claimed by any block: none

## tools
### mod_aio_characters
slugmod rc=0
mod_aio_characters: slug 'aio_characters'
  915 entries: 196 renamed and re-named, 719 renamed with the name kept, 0 left alone
  longest resulting basename: 22 bytes, 'Cheadcarroll_sinisterZ'
  --keep-paths: files stay where they are; only the manifest changes
  rewrote 211 file references in modconfig.txt
  moved 915 files and rewrote /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_characters/mod_aio_characters_filetable.json
  now rebuild: mkfiletable mod_aio_characters --workspace /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_characters --output /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_characters

model-scoped textures: 557 of 719 texture entries now carry their one model in the name; 210 ids shared by several models stay flat
mkfiletable rc=0
wrote /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_characters/filetable.dat: v4, 915 files, 0 romSources, 915 self-sourced, 719 textures, 68154 bytes

### mod_aio_pdplus
slugmod rc=0
mod_aio_pdplus: slug 'aio_pdplus'
  840 entries: 76 renamed and re-named, 764 renamed with the name kept, 0 left alone
  longest resulting basename: 17 bytes, 'Pa51radarconsoleZ'
  --keep-paths: files stay where they are; only the manifest changes
  rewrote 67 file references in modconfig.txt
  moved 840 files and rewrote /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_pdplus/mod_aio_pdplus_filetable.json
  now rebuild: mkfiletable mod_aio_pdplus --workspace /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_pdplus --output /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_pdplus

mkfiletable rc=0
wrote /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_pdplus/filetable.dat: v4, 840 files, 0 romSources, 840 self-sourced, 764 textures, 45340 bytes

### mod_aio_gex
slugmod rc=0
mod_aio_gex: slug 'aio_gex'
  1744 entries: 258 renamed and re-named, 1486 renamed with the name kept, 0 left alone
  longest resulting basename: 23 bytes, 'Pa51_horiz_door_secretZ'
  --keep-paths: files stay where they are; only the manifest changes
  rewrote 134 file references in modconfig.txt
  moved 1744 files and rewrote /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_gex/mod_aio_gex_filetable.json
  now rebuild: mkfiletable mod_aio_gex --workspace /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_gex --output /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_gex

mkfiletable rc=0
wrote /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_gex/filetable.dat: v4, 1744 files, 0 romSources, 1744 self-sourced, 1486 textures, 97046 bytes

### mod_aio_kakariko
slugmod rc=0
mod_aio_kakariko: slug 'aio_kakariko'
  1010 entries: 23 renamed and re-named, 987 renamed with the name kept, 0 left alone
  longest resulting basename: 16 bytes, 'PchrdyrocketmisZ'
  --keep-paths: files stay where they are; only the manifest changes
  rewrote 16 file references in modconfig.txt
  moved 1010 files and rewrote /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_kakariko/mod_aio_kakariko_filetable.json
  now rebuild: mkfiletable mod_aio_kakariko --workspace /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_kakariko --output /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_kakariko

mkfiletable rc=0
wrote /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_kakariko/filetable.dat: v4, 1010 files, 0 romSources, 1010 self-sourced, 987 textures, 52317 bytes

### mod_aio_darknoon
slugmod rc=0
mod_aio_darknoon: slug 'aio_darknoon'
  908 entries: 21 renamed and re-named, 887 renamed with the name kept, 0 left alone
  longest resulting basename: 16 bytes, 'PchrdyrocketmisZ'
  --keep-paths: files stay where they are; only the manifest changes
  rewrote 16 file references in modconfig.txt
  moved 908 files and rewrote /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_darknoon/mod_aio_darknoon_filetable.json
  now rebuild: mkfiletable mod_aio_darknoon --workspace /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_darknoon --output /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_darknoon

mkfiletable rc=0
wrote /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_darknoon/filetable.dat: v4, 908 files, 0 romSources, 908 self-sourced, 887 textures, 47030 bytes

### mod_aio_goldfinger
slugmod rc=0
mod_aio_goldfinger: slug 'aio_goldfinger'
  865 entries: 21 renamed and re-named, 844 renamed with the name kept, 0 left alone
  longest resulting basename: 18 bytes, 'Pmulti_ammo_crateZ'
  --keep-paths: files stay where they are; only the manifest changes
  rewrote 16 file references in modconfig.txt
  moved 865 files and rewrote /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_goldfinger/mod_aio_goldfinger_filetable.json
  now rebuild: mkfiletable mod_aio_goldfinger --workspace /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_goldfinger --output /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_goldfinger

mkfiletable rc=0
wrote /sessions/rcw-01v8r8eszrdyauwvxbemewjn/aio-test/mod_aio_goldfinger/filetable.dat: v4, 865 files, 0 romSources, 865 self-sourced, 844 textures, 44936 bytes

