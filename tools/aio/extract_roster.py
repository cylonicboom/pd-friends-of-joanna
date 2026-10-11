#!/usr/bin/env python3
"""Pull the AIO head/body roster out of jonaeru's port-mods/all-in-one tree.

Reads, by regex, nothing clever:
  src/game/modeldata/robot.c    g_HeadsAndBodies[]   (the #ifndef PLATFORM_N64 arms)
  src/include/constants.h       HEAD_* / BODY_* -> g_HeadsAndBodies index
  port/src/romdata.c            expansionFiles[]     FILE_* -> on-disk name
  src/game/mplayer/mplayer.c    g_MpHeads[] / g_MpBodies[] / g_MpFemaleHeads[]

Emits one JSON with every g_HeadsAndBodies row >= 0x98 (the AIO additions)
plus the modified vanilla rows, each tagged with its MP slot (if any) and
the lang id its MP body name points at.
"""
import json, re, sys, os

root = sys.argv[1]
out = sys.argv[2]

def read(p):
    return open(os.path.join(root, p), encoding='utf-8', errors='replace').read()

# --- FILE_* -> name
files = {}
for m in re.finditer(r'\{\s*(FILE_\w+)\s*,\s*"([^"]+)"\s*\}', read('port/src/romdata.c')):
    files[m.group(1)] = m.group(2)
# vanilla FILE_ names too (files.h)
try:
    fh = read('src/include/files.h')
except FileNotFoundError:
    fh = ''
# vanilla names come from the vanilla file list; we only need them for the
# four modified rows, resolved later against the AIO files on disk.

# --- HEAD_/BODY_ -> index
consts = {}
for m in re.finditer(r'#define\s+((?:HEAD|BODY)_\w+)\s+(0x[0-9a-fA-F]+|\d+)', read('src/include/constants.h')):
    consts[m.group(1)] = int(m.group(2), 0)
byidx = {}
for k, v in consts.items():
    byidx.setdefault(v, []).append(k)

# --- g_HeadsAndBodies: walk the file, track #ifndef PLATFORM_N64 arms
src = read('src/game/modeldata/robot.c')
start = src.index('struct headorbody g_HeadsAndBodies[]')
body = src[start:]
body = body[:body.index('\n};')]
rows = []
depth = []  # stack of (cond, active)
active = True
rowre = re.compile(r'\{\s*/\*0x([0-9a-f]+)\*/\s*([^}]*)\}\s*,?\s*(?://\s*(.*))?')
for line in body.splitlines():
    s = line.strip()
    if s.startswith('#ifndef PLATFORM_N64'):
        depth.append('aio'); continue
    if s.startswith('#ifdef PLATFORM_N64'):
        depth.append('notaio'); continue
    if s.startswith('#ifdef') or s.startswith('#if '):
        # a VERSION test: the ntsc-final port build is what we lay out, so a
        # `VERSION == VERSION_JPN_FINAL` arm is dead and anything else is live
        depth.append('other-else' if ('VERSION_JPN_FINAL' in s and '==' in s) else 'other'); continue
    if s.startswith('#else'):
        if depth: depth[-1] = {'aio': 'notaio', 'notaio': 'aio', 'other': 'other-else'}.get(depth[-1], 'other-else')
        continue
    if s.startswith('#endif'):
        if depth: depth.pop()
        continue
    m = rowre.search(s)
    if not m: continue
    arm = depth[-1] if depth else 'common'
    act = not any(d in ('notaio', 'other-else') for d in depth)
    cols = [c.strip() for c in m.group(2).split(',')]
    if len(cols) < 10: continue
    rows.append({
        'idx_comment': int(m.group(1), 16),
        'seq': len(rows),
        'arm': arm, 'active': act,
        'ismale': int(cols[0]), 'hasownhead': int(cols[1]), 'canvaryheight': int(cols[2]),
        'type': cols[3], 'height': int(cols[4]), 'file': cols[5],
        'scale': cols[6], 'animscale': cols[7], 'hand': cols[9],
        'comment': (m.group(3) or '').strip(),
    })
# the real index: rows in 'notaio'/'other-else' arms do not count for the AIO build.
# Count only rows that are live in the AIO build: common, aio, other(if N64-independent... treat VERSION ifs as live for ntsc-final: first arm)
live = []
seen = {}
prev = -1
for r in rows:
    if not r['active']:
        continue
    idx = r['idx_comment']
    if idx == 0 and prev == 0xff:
        idx = 0x100   # the /*0x0000*/ typo on Baron Samedi (Death)
    if idx in seen:
        raise SystemExit('duplicate live index %x: %s vs %s' % (idx, seen[idx]['file'], r['file']))
    r['index'] = idx
    r['consts'] = byidx.get(idx, [])
    seen[idx] = r
    live.append(r)
    prev = idx

# --- g_MpHeads / g_MpBodies
mp = read('src/game/mplayer/mplayer.c')
def table(name):
    st = mp.index(name)
    t = mp[st:]
    return t[:t.index('\n};')]
mpheads = []
for m in re.finditer(r'\{\s*/\*0x([0-9a-f]+)\*/\s*(HEAD_\w+)\s*,\s*(\w+)\s*\}\s*,\s*(?://\s*(.*))?', table('struct mphead g_MpHeads[]')):
    mpheads.append({'slot': int(m.group(1), 16), 'head': m.group(2), 'feature': m.group(3), 'comment': (m.group(4) or '').strip()})
mpbodies = []
for m in re.finditer(r'/\*0x([0-9a-f]+)\*/\s*\{\s*(BODY_\w+)\s*,\s*(L_\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*\}\s*,\s*(?://\s*(.*))?', table('struct mpbody g_MpBodies[]')):
    mpbodies.append({'slot': int(m.group(1), 16), 'body': m.group(2), 'langid': m.group(3), 'head': m.group(4), 'feature': m.group(5), 'comment': (m.group(6) or '').strip()})
fem = re.findall(r'(HEAD_\w+)', table('u32 g_MpFemaleHeads[]'))

dead = [r for r in rows if not r['active']]
changed_vanilla = []
for r in live:
    if r['arm'] != 'aio' or r['index'] >= 0x98: continue
    # find the N64/vanilla arm row with the same comment index
    alt = [x for x in dead if x['idx_comment'] == r['idx_comment'] and x['arm'] in ('notaio',)]
    if alt:
        a = alt[0]
        diff = {k: (a[k], r[k]) for k in ('ismale','hasownhead','canvaryheight','type','height','file','scale','animscale','hand') if a[k] != r[k]}
        changed_vanilla.append({'index': r['index'], 'consts': r['consts'], 'diff': diff, 'comment': r['comment']})
json.dump({'changed_vanilla_rows': changed_vanilla, 'rows': live, 'files': files, 'mpheads': mpheads, 'mpbodies': mpbodies, 'femaleheads': fem,
           'consts': consts}, open(out, 'w'), indent=1)
print('rows live', len(live), 'files', len(files), 'mpheads', len(mpheads), 'mpbodies', len(mpbodies))
print('rows >= 0x98:', sum(1 for r in live if r['index'] >= 0x98), 'max idx', live[-1]['index'], hex(live[-1]['index']))
bad = [r for r in live if r['index'] != r['idx_comment'] and r['idx_comment'] != 0]
print('vanilla rows AIO changed:', len(changed_vanilla)); print('index/comment mismatches:', [(hex(r['index']), hex(r['idx_comment']), r['file']) for r in bad][:10])
