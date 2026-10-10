# AIO 6.0 layout report

source: pd-aio-6.0 | packs: mod_allinone, mod_dark_noon, mod_gex, mod_goldfinger_64, mod_kakariko
files hashed: 6560 | distinct rel paths: 2176 | identical in all 5 packs: 990
shared-identical by class: {'tex': 752, 'G': 68, 'C': 166, 'P': 1, 'seq': 3}
in all packs but DIFFERING somewhere: 1 ['textures/0b69.bin']

## mod_aio6_characters
moved the previous mod_aio6_characters aside to pd-fojo/basedir/data/mods/_to_delete/mod_aio6_characters.20261010-221203 (delete it yourself; this mount cannot)
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

## mod_aio6_pdplus (from mod_allinone, stage blocks from mod_aio_stages)
moved the previous mod_aio6_pdplus aside to pd-fojo/basedir/data/mods/_to_delete/mod_aio6_pdplus.20261010-221204 (delete it yourself; this mount cannot)
carried: {'P': 6, 'U': 20, 'bgdata': 50, 'tex': 764} | non-vanilla names among them: none
L* banks NOT carried (they overwrite live vanilla text slots): ['LmpmenuE', 'LmpmenuJ', 'LmpmenuP', 'LoptionsE', 'LoptionsJ', 'LoptionsP']
  arenaname pdplus_stat: "Offices?" -> "Offices" (1)
  arenaname pdplus_lam: "Egyptian/Villa?" -> "Apex" (1)
  dropped block STAGE_TEST_ARCH (1): allocation-only placeholder; replaced by pdplus_suburb in overrides
  appended overrides from tools/aio6/overrides/mod_aio6_pdplus.txt
parked config: un-slugged 63 file references, renamed 17 stage names (aio -> pdplus prefix)
levels shipped: 19 | named by a bgfile in a stage block: 19 | NOT claimed by any block: none

## mod_aio6_gex (from mod_gex, stage blocks from mod_gex_stages)
moved the previous mod_aio6_gex aside to pd-fojo/basedir/data/mods/_to_delete/mod_aio6_gex.20261010-221206 (delete it yourself; this mount cannot)
carried: {'P': 118, 'U': 35, 'bgdata': 105, 'tex': 1486} | non-vanilla names among them: none
  arenaname STAGE_28: "Snow Cavern?" -> "Arctic Mine" (1)
  arenaname gex_lam: "Stack?" -> "Grand Library" (1)
  arenaname STAGE_TEST_LEN: "Village Well?" -> "Courtyard" (1)
  arenaname STAGE_TEST_MP7: "Snowy Grove?" -> "Frost Valley" (1)
  arenaname gex_sev: "Holodeck?" -> "Void" (1)
  appended overrides from tools/aio6/overrides/mod_aio6_gex.txt
parked config: un-slugged 130 file references, renamed 23 stage names (gex -> gex prefix)
levels shipped: 35 | named by a bgfile in a stage block: 32 | NOT claimed by any block: ['bg_cryp', 'bg_mp17', 'bg_mp6']

## mod_aio6_kakariko (from mod_kakariko, stage blocks from mod_kakariko_stages)
moved the previous mod_aio6_kakariko aside to pd-fojo/basedir/data/mods/_to_delete/mod_aio6_kakariko.20261010-221209 (delete it yourself; this mount cannot)
carried: {'P': 7, 'U': 4, 'bgdata': 12, 'tex': 987} | non-vanilla names among them: none
parked config: un-slugged 16 file references, renamed 3 stage names (kakariko -> kakariko prefix)
levels shipped: 4 | named by a bgfile in a stage block: 4 | NOT claimed by any block: none

## mod_aio6_darknoon (from mod_dark_noon, stage blocks from mod_darknoon_stages)
moved the previous mod_aio6_darknoon aside to pd-fojo/basedir/data/mods/_to_delete/mod_aio6_darknoon.20261010-221210 (delete it yourself; this mount cannot)
carried: {'P': 5, 'U': 4, 'bgdata': 12, 'tex': 887} | non-vanilla names among them: none
  appended overrides from tools/aio6/overrides/mod_aio6_darknoon.txt
parked config: un-slugged 4 file references, renamed 0 stage names (darknoon -> darknoon prefix)
levels shipped: 4 | named by a bgfile in a stage block: 4 | NOT claimed by any block: none

## mod_aio6_goldfinger (from mod_goldfinger_64, stage blocks from mod_goldfinger_stages)
moved the previous mod_aio6_goldfinger aside to pd-fojo/basedir/data/mods/_to_delete/mod_aio6_goldfinger.20261010-221212 (delete it yourself; this mount cannot)
carried: {'P': 5, 'U': 4, 'bgdata': 12, 'tex': 844} | non-vanilla names among them: none
  stage goldfinger_junkyard -> goldfinger_steelmill (1): carries bg_ref = AIO Steel Mill (EXTRA21)
  stage goldfinger_steelmill -> goldfinger_mall (1): carries bg_crad = AIO Mall (EXTRA22)
  stage goldfinger_mall -> goldfinger_tunnels (1): carries bg_arec = AIO Tunnels (EXTRA23)
  stage goldfinger_tunnels -> goldfinger_junkyard (1): carries bg_mp11 = AIO Junkyard (EXTRA20)
parked config: un-slugged 16 file references, renamed 4 stage names (goldfinger -> goldfinger prefix)
levels shipped: 4 | named by a bgfile in a stage block: 4 | NOT claimed by any block: none

## tools
### mod_aio6_characters
slugmod rc=0
mod_aio6_characters: slug 'aio6_characters'
  915 entries: 196 renamed and re-named, 719 renamed with the name kept, 0 left alone
  longest resulting basename: 38 bytes, 'Cheadcarroll_sinister_aio6_charactersZ'
  rewrote 211 file references in modconfig.txt
  moved 915 files and rewrote pd-fojo/basedir/data/mods/mod_aio6_characters/mod_aio6_characters_filetable.json
  now rebuild: mkfiletable mod_aio6_characters --workspace pd-fojo/basedir/data/mods/mod_aio6_characters --output pd-fojo/basedir/data/mods/mod_aio6_characters

model-scoped textures: 557 of 719 texture entries now live under their one model; 210 ids shared by several models stay flat
mkfiletable rc=0
wrote pd-fojo/basedir/data/mods/mod_aio6_characters/filetable.dat: v4, 915 files, 0 romSources, 915 self-sourced, 719 textures, 90438 bytes

### mod_aio6_pdplus
slugmod rc=0
mod_aio6_pdplus: slug 'aio6_pdplus'
  840 entries: 76 renamed and re-named, 764 renamed with the name kept, 0 left alone
  longest resulting basename: 29 bytes, 'Pa51radarconsole_aio6_pdplusZ'
  rewrote 67 file references in modconfig.txt
  moved 840 files and rewrote pd-fojo/basedir/data/mods/mod_aio6_pdplus/mod_aio6_pdplus_filetable.json
  now rebuild: mkfiletable mod_aio6_pdplus --workspace pd-fojo/basedir/data/mods/mod_aio6_pdplus --output pd-fojo/basedir/data/mods/mod_aio6_pdplus

mkfiletable rc=0
wrote pd-fojo/basedir/data/mods/mod_aio6_pdplus/filetable.dat: v4, 840 files, 0 romSources, 840 self-sourced, 764 textures, 55496 bytes

### mod_aio6_gex
slugmod rc=0
mod_aio6_gex: slug 'aio6_gex'
  1744 entries: 258 renamed and re-named, 1486 renamed with the name kept, 0 left alone
  longest resulting basename: 32 bytes, 'Pa51_horiz_door_secret_aio6_gexZ'
  rewrote 134 file references in modconfig.txt
  moved 1744 files and rewrote pd-fojo/basedir/data/mods/mod_aio6_gex/mod_aio6_gex_filetable.json
  now rebuild: mkfiletable mod_aio6_gex --workspace pd-fojo/basedir/data/mods/mod_aio6_gex --output pd-fojo/basedir/data/mods/mod_aio6_gex

mkfiletable rc=0
wrote pd-fojo/basedir/data/mods/mod_aio6_gex/filetable.dat: v4, 1744 files, 0 romSources, 1744 self-sourced, 1486 textures, 113000 bytes

### mod_aio6_kakariko
slugmod rc=0
mod_aio6_kakariko: slug 'aio6_kakariko'
  1010 entries: 23 renamed and re-named, 987 renamed with the name kept, 0 left alone
  longest resulting basename: 30 bytes, 'Pchrdyrocketmis_aio6_kakarikoZ'
  rewrote 16 file references in modconfig.txt
  moved 1010 files and rewrote pd-fojo/basedir/data/mods/mod_aio6_kakariko/mod_aio6_kakariko_filetable.json
  now rebuild: mkfiletable mod_aio6_kakariko --workspace pd-fojo/basedir/data/mods/mod_aio6_kakariko --output pd-fojo/basedir/data/mods/mod_aio6_kakariko

mkfiletable rc=0
wrote pd-fojo/basedir/data/mods/mod_aio6_kakariko/filetable.dat: v4, 1010 files, 0 romSources, 1010 self-sourced, 987 textures, 66480 bytes

### mod_aio6_darknoon
slugmod rc=0
mod_aio6_darknoon: slug 'aio6_darknoon'
  908 entries: 21 renamed and re-named, 887 renamed with the name kept, 0 left alone
  longest resulting basename: 30 bytes, 'Pchrdyrocketmis_aio6_darknoonZ'
  rewrote 16 file references in modconfig.txt
  moved 908 files and rewrote pd-fojo/basedir/data/mods/mod_aio6_darknoon/mod_aio6_darknoon_filetable.json
  now rebuild: mkfiletable mod_aio6_darknoon --workspace pd-fojo/basedir/data/mods/mod_aio6_darknoon --output pd-fojo/basedir/data/mods/mod_aio6_darknoon

mkfiletable rc=0
wrote pd-fojo/basedir/data/mods/mod_aio6_darknoon/filetable.dat: v4, 908 files, 0 romSources, 908 self-sourced, 887 textures, 59763 bytes

### mod_aio6_goldfinger
slugmod rc=0
mod_aio6_goldfinger: slug 'aio6_goldfinger'
  865 entries: 21 renamed and re-named, 844 renamed with the name kept, 0 left alone
  longest resulting basename: 34 bytes, 'Pmulti_ammo_crate_aio6_goldfingerZ'
  rewrote 16 file references in modconfig.txt
  moved 865 files and rewrote pd-fojo/basedir/data/mods/mod_aio6_goldfinger/mod_aio6_goldfinger_filetable.json
  now rebuild: mkfiletable mod_aio6_goldfinger --workspace pd-fojo/basedir/data/mods/mod_aio6_goldfinger --output pd-fojo/basedir/data/mods/mod_aio6_goldfinger

mkfiletable rc=0
wrote pd-fojo/basedir/data/mods/mod_aio6_goldfinger/filetable.dat: v4, 865 files, 0 romSources, 865 self-sourced, 844 textures, 58797 bytes

