#!/usr/bin/env python3
"""layout — lay Atari-Dude's Perfect Dark All In One drop out as fojo mods.

    python3 tools/aio/layout.py --src <unzipped AIO drop> [--out basedir/data/mods]
                                [--no-tools] [--force]

Run from the pd-fojo root. Needs the drop (five mod_* pack directories, from
the link Atari-Dude / Jonaeru share - the bytes are theirs and are not in this
repository) and the built tools/mkfiletable binaries (`make -C tools/mkfiletable`).
Writes six mods, each with files/, textures/, modconfig.txt and
<mod>_filetable.json, then runs slugmod --keep-paths --apply (fojo names in
the manifest, files left where the drop put them) and mkfiletable on each,
and writes a report beside the mods. The files/, textures/ and sequences/
directories it fills are gitignored; the metadata is what is tracked.

One mod carries the cast, five carry levels:

    mod_aio_characters   the 166 C* + 68 G* + 3 sequences every pack ships
                          byte-identical, plus the textures they share; the
                          HeadsAndBodies roster is generated from jonaeru's
                          g_HeadsAndBodies / g_MpHeads / g_MpBodies (see
                          roster.json, pulled by extract_roster.py)
    mod_aio_pdplus       mod_allinone's levels (PD Plus / All Solos / Suburb / Training Day)
    mod_aio_gex          mod_gex's levels
    mod_aio_kakariko     mod_kakariko's
    mod_aio_darknoon     mod_dark_noon's
    mod_aio_goldfinger   mod_goldfinger_64's

Stage blocks come from tools/aio/stages/<mod>.txt - fojo-authored, with the
rulings in stage-renames.json and overrides/ already applied - and are
re-slugged by slugmod under the mod's name. Nothing is deleted; an existing
output mod dir refuses to be overwritten without --force.
"""
import argparse, hashlib, json, os, re, shutil, subprocess, sys, collections

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))   # the pd-fojo checkout

MODS = [
    # tag, mod name, stage-name prefix, source pack
    ('pdplus',     'mod_aio_pdplus',     'pdplus',     'mod_allinone'),
    ('gex',        'mod_aio_gex',        'gex',        'mod_gex'),
    ('kakariko',   'mod_aio_kakariko',   'kakariko',   'mod_kakariko'),
    ('darknoon',   'mod_aio_darknoon',   'darknoon',   'mod_dark_noon'),
    ('goldfinger', 'mod_aio_goldfinger', 'goldfinger', 'mod_goldfinger_64'),
]
CHARS = 'mod_aio_characters'
LANGBANK_MPMENU = 0x28
LANGBANK_OPTIONS = 0x2b

ap = argparse.ArgumentParser()
ap.add_argument('--src', required=True, help='the unzipped AIO drop: a directory holding mod_allinone, mod_gex, mod_kakariko, mod_dark_noon, mod_goldfinger_64')
ap.add_argument('--out', default=os.path.join(ROOT, 'basedir', 'data', 'mods'), help='where the six mods land')
ap.add_argument('--roster', default=os.path.join(HERE, 'roster.json'), help='g_HeadsAndBodies / g_MpHeads / g_MpBodies as extract_roster.py pulls them from the AIO source')
ap.add_argument('--vanilla', default=os.path.join(ROOT, 'tools', 'share', 'pd-ntsc-final', 'files.json'))
ap.add_argument('--tools', default=os.environ.get('AIO_TOOLS', os.path.join(ROOT, 'tools', 'mkfiletable')),
                help='dir holding the slugmod and mkfiletable binaries (make -C tools/mkfiletable)')
ap.add_argument('--pdhead', default=os.path.join(ROOT, 'tools', 'pdhead'))
ap.add_argument('--no-tools', action='store_true', help='write the layout, skip slugmod/mkfiletable')
ap.add_argument('--force', action='store_true')
args = ap.parse_args()

report = []
def say(*a):
    s = ' '.join(str(x) for x in a)
    print(s); report.append(s)

def sha1(p):
    h = hashlib.sha1()
    with open(p, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()

vanilla = set(json.load(open(args.vanilla))['files'].keys())
roster = json.load(open(args.roster))

# ---------------------------------------------------------------- hash the drop
hashes = {}  # (pack, rel) -> sha1
packs = sorted(d for d in os.listdir(args.src) if d.startswith('mod_'))
for pk in packs:
    for sub in ('files', 'textures', 'sequences'):
        base = os.path.join(args.src, pk, sub)
        for r, _, fs in os.walk(base):
            for f in fs:
                p = os.path.join(r, f)
                hashes[(pk, os.path.relpath(p, os.path.join(args.src, pk)))] = sha1(p)
byrel = collections.defaultdict(dict)
for (pk, rel), h in hashes.items():
    byrel[rel][pk] = h

def cls(rel):
    b = os.path.basename(rel)
    if rel.startswith('files/bgdata/'): return 'bgdata'
    if rel.startswith('textures/'): return 'tex'
    if rel.startswith('sequences/'): return 'seq'
    return b[0]

shared = {rel for rel, d in byrel.items() if len(d) == len(packs) and len(set(d.values())) == 1}
say('# AIO 6.0 layout report')
say('')
say('source:', args.src, '| packs:', ', '.join(packs))
say('files hashed:', len(hashes), '| distinct rel paths:', len(byrel),
    '| identical in all %d packs: %d' % (len(packs), len(shared)))
c = collections.Counter(cls(r) for r in shared)
say('shared-identical by class:', dict(c))
diffshared = sorted(rel for rel, d in byrel.items() if len(d) == len(packs) and len(set(d.values())) > 1)
say('in all packs but DIFFERING somewhere:', len(diffshared), diffshared[:10])

# ---------------------------------------------------------------- helpers
def preamble(pk):
    """Every comment line before the first directive in a source modconfig."""
    out = []
    for line in open(os.path.join(args.src, pk, 'modconfig.txt'), encoding='utf-8', errors='replace'):
        s = line.rstrip('\n')
        if s.strip() == '' or s.lstrip().startswith('#'):
            out.append(s)
        else:
            break
    return out

def outdir(mod):
    return os.path.join(args.out, mod)

def fresh(mod):
    d = outdir(mod)
    if os.path.exists(d):
        if not args.force:
            sys.exit('%s exists; pass --force to move it aside (nothing is deleted)' % d)
        import time
        aside = os.path.join(args.out, '_to_delete', '%s.%s' % (mod, time.strftime('%Y%m%d-%H%M%S')))
        os.makedirs(os.path.dirname(aside), exist_ok=True)
        os.rename(d, aside)
        say('moved the previous %s aside to %s (delete it yourself; this mount cannot)' % (mod, aside))
    for sub in ('files', 'files/bgdata', 'textures', 'sequences'):
        os.makedirs(os.path.join(d, sub), exist_ok=True)
    return d

def copy(pk, rel, mod):
    src = os.path.join(args.src, pk, rel)
    dst = os.path.join(outdir(mod), rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst)
    return rel

def texrow(rel, why):
    b = os.path.basename(rel)
    tid = int(b.split('.')[0], 16)
    # no `source`: slugmod declares {self: true} itself and skips entries that already carry one
    return {'name': b, 'path': rel, 'description': '%s, local id 0x%04x' % (why, tid),
            'type': 'texture', 'textureId': '0x%04x' % tid}

def filerow(rel, why):
    b = os.path.basename(rel)
    row = {'name': b, 'path': rel, 'description': why}
    if b in vanilla:
        row['replaces'] = b
    return row

def write_manifest(mod, desc, rows):
    m = {'modName': mod, 'modVersion': '6.0.0', 'description': desc,
         'author': 'Jonaeru / Atari-Dude and the authors credited in modconfig.txt; repackaged for the fojo modloader',
         'romSources': [], 'files': rows}
    with open(os.path.join(outdir(mod), mod + '_filetable.json'), 'w') as f:
        json.dump(m, f, indent=4)

HEADER = """modname "{mod}"
modversion "6.0.0"

# ==========================================================================
# {mod}
#
# DERIVED WORK - NOT ORIGINAL. Generated by tools/aio/layout.py from
# the Perfect Dark All in One 6.0 distribution ({src}), for the fojo
# modloader. {what}
#
# Left out of this mod: {left}
#
# Every credit and revision-history line from the source modconfig(s) is
# reproduced below unaltered. The level, model and texture work is the
# original authors'.
# ==========================================================================
"""

# ---------------------------------------------------------------- characters
say('')
say('## ' + CHARS)
d = fresh(CHARS)
rows = roster['rows']
files = roster['files']            # FILE_* -> on-disk name
byidx = {r['index']: r for r in rows}
mpheads = {h['head']: h for h in roster['mpheads']}
mpbodies = {b['body']: b for b in roster['mpbodies']}
consts = roster['consts']

# lang strings, from the drop's own banks
def extract_bank(fname):
    """A PD text bank: optionally 1172/1173-compressed (2-byte magic, 3-byte
    size, raw deflate), then a table of big-endian u32 offsets running up to
    the first string, each pointing at a NUL-terminated string."""
    import struct, zlib
    raw = open(os.path.join(args.src, 'mod_allinone', 'files', fname), 'rb').read()
    data = raw
    if len(raw) >= 5 and struct.unpack_from('>H', raw, 0)[0] in (0x1172, 0x1173):
        data = zlib.decompress(raw[5:], wbits=-15)
    first = struct.unpack_from('>I', data, 0)[0]
    offs = [struct.unpack_from('>I', data, p)[0] for p in range(0, first, 4)]
    out = []
    for o in offs:
        if o >= len(data):
            out.append(''); continue
        end = data.find(b'\x00', o)
        b = data[o:end if end >= 0 else len(data)]
        try: out.append(b.decode('utf-8'))
        except UnicodeDecodeError: out.append(b.decode('latin-1'))
    return out
bank = {'L_MPMENU': extract_bank('LmpmenuE'), 'L_OPTIONS': extract_bank('LoptionsE')}
bankj = {'L_MPMENU': extract_bank('LmpmenuJ'), 'L_OPTIONS': extract_bank('LoptionsJ')}
def langstr(lid, b=bank):
    m = re.match(r'(L_\w+?)_(\d+)$', lid)
    return b[m.group(1)][int(m.group(2))].rstrip('\n')
def langnum(lid):
    m = re.match(r'(L_\w+?)_(\d+)$', lid)
    return ({'L_MPMENU': LANGBANK_MPMENU, 'L_OPTIONS': LANGBANK_OPTIONS}[m.group(1)] << 9) | int(m.group(2))

def entname(filec):
    s = filec
    for pre, kind in (('FILE_CHEAD', 'head_'), ('FILE_C', 'body_')):
        if s.startswith(pre):
            return kind + s[len(pre):].lower()
    raise ValueError(filec)

ondisk = {os.path.basename(rel) for rel in byrel if cls(rel) in ('C', 'G')}
carried = {}      # basename -> why
held = collections.OrderedDict()   # basename -> why held back
entries = []
names_emitted = []

# AIO's additions: every g_HeadsAndBodies row from 0x98 up, plus two vanilla
# rows whose model AIO swapped for a new file (0x70 BODY_TESTCHR ->
# Ccarroll2Z "Dr. Caroll (Sinister)", 0x84 BODY_PRESIDENT_CLONE -> Cskedar2Z
# "Skedar"); those come in as new bodies under their own names rather than as
# vanilla replacements. The other vanilla rows AIO touched only re-hand a
# vanilla body; they are listed, not carried.
extra_rows = {0x70: None, 0x84: None}
gen_rows = [r for r in rows if r['index'] >= 0x98] + [byidx[i] for i in sorted(extra_rows)]
rehand = {}   # hand file -> vanilla BODY_ it is for
for cv in roster.get('changed_vanilla_rows', []):
    if cv['index'] in extra_rows: continue
    h = cv['diff'].get('hand')
    if h and files.get(h[1]):
        rehand[files[h[1]]] = (cv['consts'] or ['0x%x' % cv['index']])[0]

for r in gen_rows:
    idx = r['index']
    fname = files.get(r['file'])
    if fname is None:
        say('  SKIP 0x%04x %s: file %s is not in AIO expansion map' % (idx, r['comment'], r['file']))
        continue
    is_head = r['file'].startswith('FILE_CHEAD')
    name = extra_rows.get(idx) or entname(r['file'])
    hand = files.get(r['hand']) if r['hand'] not in ('0', '') else None
    hand_vanilla = None
    if r['hand'] not in ('0', '') and hand is None:
        # a vanilla hand file; its on-disk name is the constant lowercased
        hand_vanilla = 'G' + r['hand'][len('FILE_G'):].lower() + 'Z'
        hand = hand_vanilla
        if hand not in vanilla:
            say('  WARN 0x%04x %s: hand %s guessed as %s which is not vanilla' % (idx, name, r['hand'], hand))
    if fname not in ondisk:
        say('  SKIP 0x%04x %s: %s not in the drop' % (idx, name, fname))
        continue
    carried[fname] = 'model for %s (AIO g_HeadsAndBodies 0x%04x)' % (name, idx)
    if hand and hand in ondisk:
        carried[hand] = 'hand for %s' % name
    cst = [c for c in r['consts'] if c.startswith('HEAD_' if is_head else 'BODY_')]
    cname = cst[0] if cst else None
    lines = ['HeadsAndBodies {', '    name "%s"' % name]
    label = None
    mp = None
    if is_head:
        mp = mpheads.get(cname) if cname else None
    else:
        mp = mpbodies.get(cname) if cname else None
        if mp:
            label = langstr(mp['langid'])
            lines.append('    label "%s"   # %s in the AIO %s bank; jp: %s' % (
                label.replace('"', '\\"'), mp['langid'], 'LmpmenuE' if 'MPMENU' in mp['langid'] else 'LoptionsE',
                langstr(mp['langid'], bankj).replace('"', '\\"')))
    lines.append('    filenum "%s"' % fname)
    if hand and not is_head:
        lines.append('    handfilenum "%s"%s' % (hand, '   # vanilla hand' if hand_vanilla else ''))
    tnum = {'HEADBODYTYPE_DEFAULT': 0, 'HEADBODYTYPE_FEMALE': 1, 'HEADBODYTYPE_FEMALEGUARD': 2,
            'HEADBODYTYPE_MAIAN': 3, 'HEADBODYTYPE_CASS': 4, 'HEADBODYTYPE_MRBLONDE': 5}[r['type']]
    lines.append('    type %d' % tnum)
    lines.append('    ismale %d' % r['ismale'])
    if is_head or r['hasownhead']:
        # a body with its own head (Skedar, Dr Caroll): the previewer must not
        # bolt a head onto it (menu.c unk00_01 branch) - leaving this 0 crashes it
        lines.append('    hasownhead %d' % r['hasownhead'])
    if r['canvaryheight']:
        lines.append('    canvaryheight 1')
    lines.append('    height %d' % r['height'])
    lines.append('    scale %s' % r['scale'])
    if not is_head:
        lines.append('    animscale %s' % r['animscale'])
        if mp:
            lines.append('    bodyslotnum 1')
            if mp['head'].startswith('HEAD_'):
                hidx = consts.get(mp['head'])
                if hidx is not None and hidx >= 0x98:
                    lines.append('    bodyheadname "%s"' % entname(byidx[hidx]['file']))
                else:
                    lines.append('    bodyheadnum %d   # vanilla %s' % (hidx, mp['head']))
            elif mp['head'] == '1001':
                # AIO: a random head from its g_GexMaleHeads/g_GexFemaleHeads pool;
                # fojo has no such pool and no 1001 case (mpGetMpheadnumByMpbodynum),
                # so a random vanilla head is the nearest thing that resolves
                lines.append('    bodyheadnum 1000   # AIO says 1001 = random GEX head; no such pool here')
            else:
                lines.append('    bodyheadnum %s' % mp['head'])
            if mp['feature'] != '0':
                lines.append('    requirefeature %s' % mp['feature'])
    src_note = '    # AIO g_HeadsAndBodies 0x%04x%s' % (idx, (' "%s"' % r['comment']) if r['comment'] else '')
    if mp:
        src_note += '; AIO %s 0x%02x' % ('g_MpHeads' if is_head else 'g_MpBodies', mp['slot'])
    elif is_head:
        src_note += '; NOT in AIO g_MpHeads'
    else:
        src_note += '; NOT in AIO g_MpBodies (not selectable there)'
    if idx in extra_rows:
        src_note += '; AIO re-points vanilla row %s at this new model; carried as a new body, the vanilla row is left alone' % cname
    lines.append(src_note)
    lines.append('}')
    entries.append('\n'.join(lines))
    names_emitted.append((name, fname, label))

# what the roster did not reach
for b in sorted(ondisk):
    if b in carried:
        continue
    if b in vanilla:
        held[b] = 'vanilla name: would REPLACE the base rom file for every mod; not carried'
    elif b.startswith('Cheadfoslerfer'):
        held[b] = 'Poplin test head; never copied (her ruling, c-public-test-aio-prepare)'
    elif b in rehand:
        held[b] = 'AIO re-hands the vanilla body %s with it (a vanilla-row change); not carried' % rehand[b]
    elif b.endswith('_jpZ') or b in ('Cheaddark_usZ', 'CheadgreyZ'):
        held[b] = 'JP/US Joanna variant: fojo serves these by rom mount (mikado); not carried'
    else:
        held[b] = 'no g_HeadsAndBodies row >= 0x98 references it; not carried'

# shared textures + sequences ride with the cast
rows_c = []
for b in sorted(carried):
    rel = 'files/' + b
    pk = next(p for p in packs if (p, rel) in hashes)
    copy(pk, rel, CHARS)
    rows_c.append(filerow(rel, carried[b] + ' (AIO 6.0, identical in every pack)' if rel in shared else carried[b] + ' (from %s)' % pk))
# Textures: exactly the ids the carried models reference (pdhead info reads each
# model's texconfigs) that some pack ships. An id no pack ships is a vanilla
# texture and resolves from the base rom; an id a pack ships but the shared set
# lacks (Skedar's 0b63-0b67 live only in mod_gex) would otherwise fall to vanilla
# bytes under AIO's id, which is the wrong picture.
# Read BOTH the texconfig table and the display-list binds: the two can
# disagree (Brosnan's table says 0x55d.., its binds say 0xc33.., and the binds
# are what render), and a single-texture bind is read 16 bits wide because
# AIO-built models keep bits 12-15 of the id in w1 (see c-bind16-texid).
import importlib.util, importlib.machinery, struct
_spec = importlib.util.spec_from_loader('pdhead', importlib.machinery.SourceFileLoader('pdhead', args.pdhead))
pdhead = importlib.util.module_from_spec(_spec); _spec.loader.exec_module(pdhead)
def model_texids(path):
    kind, hdr, m = pdhead.load(path)
    md = pdhead.modeldef(m)
    ids = {t['texid'] for t in pdhead.texconfigs(m, md['numtex'])}
    for n in pdhead.gdl_nodes(m, md):
        for base in [x & 0xffffff for x in struct.unpack_from('>II', m, n['rodata'])]:
            for o in pdhead.scan_gdl(m, base):
                if m[o] != 0xc0:
                    continue
                w0, w1 = struct.unpack_from('>II', m, o)
                if (w0 & 0xff) == 1:
                    ids.add(w1 & 0xfff); ids.add((w1 >> 12) & 0xfff)
                else:
                    ids.add(w1 & 0xffff)
    return sorted(ids)
referenced = {}
disagree = 0
for b in sorted(carried):
    referenced[b] = model_texids(os.path.join(outdir(CHARS), 'files', b))
allref = {i for v in referenced.values() for i in v}
ntex = 0
vanilla_only = []
for tid in sorted(allref):
    rel = 'textures/%04x.bin' % tid
    if rel not in byrel:
        vanilla_only.append(tid); continue
    pks = byrel[rel]
    src_pk = 'mod_allinone' if 'mod_allinone' in pks else sorted(pks)[0]
    why = 'texture referenced by a carried model'
    if len(set(pks.values())) > 1:
        why += '; packs disagree on its bytes, %s taken' % src_pk
        say('  texture %s differs between packs; took %s' % (rel, src_pk))
    elif len(pks) < len(packs):
        why += '; shipped only by %s' % ','.join(sorted(pks))
    copy(src_pk, rel, CHARS); rows_c.append(texrow(rel, why)); ntex += 1
say('texture census: %d models reference %d distinct ids; %d carried (shipped by some pack), %d are vanilla ids no pack ships' % (len(referenced), len(allref), ntex, len(vanilla_only)))
unref = sum(1 for rel in byrel if cls(rel) == 'tex' and rel in shared and int(os.path.basename(rel)[:4], 16) not in allref)
say('  %d textures every pack ships identically are referenced by no carried model and were left out' % unref)
nseq = 0
for rel in sorted(byrel):
    if cls(rel) == 'seq':
        copy('mod_allinone', rel, CHARS); nseq += 1
write_manifest(CHARS, 'The AIO 6.0 cast: character and hand models every pack ships identically, their shared textures, and the three sequences. Roster generated from jonaeru/perfect_dark port-mods/all-in-one.', rows_c)

cfg = [HEADER.format(mod=CHARS, src=os.path.basename(os.path.normpath(args.src)),
    what='It carries the %d C*/G* models the generated HeadsAndBodies roster below references, the %d textures every pack ships identically, and the %d sequences.' % (len(carried), ntex, nseq),
    left='%d C*/G* files held back (listed at the end of this file); every level file (see the mod_aio_* stage mods).' % len(held))]
cfg.append('# --- begin text carried over verbatim from the source modconfigs ---')
for pk in packs:
    cfg.append('# --- %s/%s/modconfig.txt ---' % (os.path.basename(os.path.normpath(args.src)), pk))
    cfg.extend(preamble(pk))
cfg.append('# --- end carried-over text ---')
cfg.append('')
cfg.append('# ==========================================================================')
cfg.append('# HeadsAndBodies, generated from jonaeru/perfect_dark @ port-mods/all-in-one')
cfg.append('# (src/game/modeldata/robot.c g_HeadsAndBodies[], src/game/mplayer/mplayer.c')
cfg.append('# g_MpHeads[] / g_MpBodies[], port/src/romdata.c expansionFiles[]). Entry')
cfg.append('# names are AIO\'s own FILE_ identifiers; `label` is the body\'s display name')
cfg.append('# as the AIO LoptionsE/LmpmenuE banks spell it. `label` is NOT read by the')
cfg.append('# loader yet - HeadsAndBodies has only the numeric `bodyname` lang id, and')
cfg.append('# AIO\'s ids point at vanilla slots it overwrote (e.g. L_OPTIONS_211 is')
cfg.append('# "Subtitles" in vanilla, "Bond (Arctic)" in AIO), so those ids are not')
cfg.append('# emitted. Until `label` lands these bodies render langGet(0) as their name')
cfg.append('# (g_MpBodies[].name is left at 0 by the loader).')
cfg.append('# ==========================================================================')
cfg.append('')
cfg.append('\n#\n'.join(entries))
cfg.append('')
cfg.append('# ==========================================================================')
cfg.append('# Vanilla g_HeadsAndBodies rows AIO changes in C (not expressed here - a')
cfg.append('# replacement of a vanilla body is a different kind of change, hers to call):')
for cv in roster.get('changed_vanilla_rows', []):
    cfg.append('#   0x%04x %-24s %s' % (cv['index'], (cv['consts'] or ['?'])[0], '; '.join('%s %s -> %s' % (k, v[0], v[1]) for k, v in cv['diff'].items())))
cfg.append('#   0x0000-0x0003 BODY_DJBOND/CONNERY/DALTON/MOORE: hands -> Ghand_bondZ / Ghand_bondwhiteZ (nested ifdef, not diffed above)')
cfg.append('# ==========================================================================')
cfg.append('# Held back from the drop - on disk in %s but not carried here:' % os.path.basename(os.path.normpath(args.src)))
for b, why in held.items():
    cfg.append('#   %-28s %s' % (b, why))
cfg.append('# ==========================================================================')
with open(os.path.join(d, 'modconfig.txt'), 'w') as f:
    f.write('\n'.join(cfg) + '\n')
say('entries:', len(entries), '(heads %d, bodies %d)' % (sum(1 for n, _, _ in names_emitted if n.startswith('head_')), sum(1 for n, _, _ in names_emitted if n.startswith('body_'))))
say('models carried:', len(carried), '| textures:', ntex, '| sequences:', nseq)
say('held back:', len(held))
for b, why in held.items():
    say('  -', b, '—', why)

# ---------------------------------------------------------------- stage mods
for tag, mod, prefix, pk in MODS:
    say('')
    say('## ' + mod + ' (from %s, stage blocks from tools/aio/stages/%s.txt)' % (pk, mod))
    d = fresh(mod)
    rows_s = []
    carried_s = collections.Counter()
    for rel in sorted(r for r, pks in byrel.items() if pk in pks):
        k = cls(rel)
        if k in ('C', 'G', 'seq'):
            continue
        if k == 'L':
            continue
        if k == 'tex':
            copy(pk, rel, mod); rows_s.append(texrow(rel, 'stage texture')); carried_s['tex'] += 1
            continue
        why = {'bgdata': 'level geometry / tiles / pads', 'U': 'setup', 'P': 'stage prop model'}.get(k, 'file') + ' (from %s)' % pk
        copy(pk, rel, mod); rows_s.append(filerow(rel, why)); carried_s[k] += 1
    nonvan = [r['name'] for r in rows_s if 'replaces' not in r and r.get('type') != 'texture']
    say('carried:', dict(carried_s), '| non-vanilla names among them:', nonvan or 'none')
    lfiles = sorted(os.path.basename(r) for r, pks in byrel.items() if pk in pks and cls(r) == 'L')
    if lfiles:
        say('L* banks NOT carried (they overwrite live vanilla text slots):', lfiles)
    write_manifest(mod, 'Levels from %s of the AIO 6.0 distribution: stage declarations, level data (bgdata/setups/props) and the textures those stages use. The shared AIO cast lives in %s.' % (pk, CHARS), rows_s)

    # stage body: the fojo-authored declarations, rulings already applied
    fixture = os.path.join(HERE, 'stages', mod + '.txt')
    body = open(fixture, encoding='utf-8').read()
    # which shipped levels have no stage block
    segs = sorted(os.path.basename(r)[:-4] for r, pks in byrel.items() if pk in pks and r.endswith('.seg'))
    claimed = set(re.findall(r'bgfile\s+"bgdata/([^"]+)\.seg"', body))
    unclaimed = [s for s in segs if s not in claimed]
    say('levels shipped:', len(segs), '| named by a bgfile in a stage block:', len(claimed & set(segs)), '| NOT claimed by any block:', unclaimed or 'none')
    named = set(re.findall(r'"(?:bgdata/|)([A-Za-z0-9_.-]+)"', body))
    shipped = {os.path.basename(r) for r, pks in byrel.items() if pk in pks}
    stale = sorted(n for n in named if (n.endswith('Z') or n.endswith('.seg')) and n not in shipped and n not in vanilla)
    if stale:
        say('WARN: stage blocks name files this drop does not ship:', stale[:20])
    cfg = [HEADER.format(mod=mod, src=os.path.basename(os.path.normpath(args.src)),
        what='It carries %s\'s level data and textures.' % pk,
        left='the %d C* / %d G* models and 3 sequences every pack ships (see %s)%s.' % (
            sum(1 for r, pks in byrel.items() if pk in pks and cls(r) == 'C'),
            sum(1 for r, pks in byrel.items() if pk in pks and cls(r) == 'G'), CHARS,
            ('; the %d L* text banks (they overwrite live vanilla text slots - names go in labels instead)' % len(lfiles)) if lfiles else ''))]
    cfg.append('# --- begin text carried over verbatim from %s/%s/modconfig.txt ---' % (os.path.basename(os.path.normpath(args.src)), pk))
    cfg.extend(preamble(pk))
    cfg.append('# --- end carried-over text ---')
    cfg.append('')
    cfg.append('# --- stage declarations from tools/aio/stages/%s.txt (fojo-authored), re-slugged by slugmod ---' % mod)
    cfg.append(body.lstrip('\n'))
    with open(os.path.join(d, 'modconfig.txt'), 'w') as f:
        f.write('\n'.join(cfg) + '\n')

# ---------------------------------------------------------------- model-scoped textures
def scope_textures(mod):
    """After slugmod: a texture only one model references becomes that model's
    own - name '<slugged model>/<id>.bin', the candidate
    modTextureResolveFileDetailed (mod.c) tries first. The file stays at
    textures/<id>.bin, where the drop put it (her call 2026-10-10: names are
    ours, paths are upstream's); a PDFT entry carries name and path apart.
    Ids several models share stay flat ('<id>.bin'), the second candidate; scoping
    a shared texture to one model would misdescribe it. There is no texmap:
    the id in the name is the id the model uses, and it stays the mod's own."""
    mpath = os.path.join(outdir(mod), mod + '_filetable.json')
    man = json.load(open(mpath))
    # old basename -> entry, so the census (keyed by pre-slug names) can find the slugged model
    slugged = {}
    for e in man['files']:
        a = (e.get('source') or {}).get('alias')
        if a:
            slugged[a] = e['name']
    owners = collections.defaultdict(set)
    for b, ids in referenced.items():
        for i in ids:
            owners[i].add(slugged.get(b, b))
    moved = 0
    for e in man['files']:
        if e.get('type') != 'texture':
            continue
        tid = int(e['textureId'], 16)
        own = owners.get(tid, set())
        if len(own) != 1:
            continue
        model = next(iter(own))
        newname = '%s/%04x.bin' % (model, tid)
        e['name'] = newname; e['model'] = model
        e['description'] += '; scoped to %s (its only user)' % model
        moved += 1
    json.dump(man, open(mpath, 'w'), indent=4)
    say('model-scoped textures: %d of %d texture entries now carry their one model in the name; %d ids shared by several models stay flat' % (
        moved, sum(1 for e in man['files'] if e.get('type') == 'texture'), sum(1 for i, o in owners.items() if len(o) > 1)))

# ---------------------------------------------------------------- tools
allmods = [CHARS] + [m[1] for m in MODS]
if not args.no_tools:
    say('')
    say('## tools')
    for mod in allmods:
        say('### ' + mod)
        r = subprocess.run([os.path.join(args.tools, 'slugmod'), outdir(mod), '--keep-paths', '--apply'], capture_output=True, text=True)
        say('slugmod rc=%d' % r.returncode); say(r.stdout.strip()[-1500:]); say(r.stderr.strip()[-800:])
        if mod == CHARS:
            scope_textures(mod)
        r = subprocess.run([os.path.join(args.tools, 'mkfiletable'), mod, '--workspace', outdir(mod), '--output', outdir(mod), '--vanilla', args.vanilla], capture_output=True, text=True)
        say('mkfiletable rc=%d' % r.returncode); say(r.stdout.strip()[-1500:]); say(r.stderr.strip()[-800:])

with open(os.path.join(args.out, 'aio-layout-report.md'), 'w') as f:
    f.write('\n'.join(report) + '\n')
print('\nreport:', os.path.join(args.out, 'aio-layout-report.md'))
