#!/usr/bin/env python3
"""Byte-parity test for pdft_write.c against the filetables already shipped.

Decodes a real filetable.dat the way port/src/romdata.c reads it, hands the
decoded contents to the C encoder, and requires the result to be byte-identical
to the file it started from. Every shipped mod fragment is a golden file, so
this is parity with whatever produced them - today `pdt build-mod-filetable` -
without needing that tool present or runnable.

    cc -O2 -Wall -o /tmp/pdft_reencode pdft_reencode.c pdft_write.c
    python3 pdft_test.py /tmp/pdft_reencode ../../../pd-fojo-basedir/data/mods/mod_*/filetable.dat

With no paths it looks for the mods under pd-fojo-basedir itself.
"""
import glob
import os
import struct
import subprocess
import sys
import tempfile


class Trunc(Exception):
    pass


class R:
    def __init__(self, data):
        self.d = data
        self.p = 0

    def take(self, n):
        if self.p + n > len(self.d):
            raise Trunc(f"want {n} at {self.p}, {len(self.d) - self.p} left")
        out = self.d[self.p:self.p + n]
        self.p += n
        return out

    def u8(self):
        return self.take(1)[0]

    def u16(self):
        return struct.unpack('>H', self.take(2))[0]

    def u32(self):
        return struct.unpack('>I', self.take(4))[0]

    def s8(self):
        """A length-prefixed, NUL-terminated string with a u8 length."""
        n = self.u8()
        return self.take(n)[:-1].decode('utf-8', 'surrogateescape') if n else ''

    def s16(self):
        n = self.u16()
        return self.take(n)[:-1].decode('utf-8', 'surrogateescape') if n else ''


def decode(data):
    """Read a PDFT the way romdataParseFileTable does."""
    r = R(data)
    if r.take(4) != b'PDFT':
        raise ValueError('not a PDFT')
    version = r.u32()
    num_files = r.u32()
    num_sources = r.u32() if version >= 2 else 0

    sources = []
    for _ in range(num_sources):
        sid = r.s8()
        fn = r.s8()
        expected = r.u32()
        flags = r.u8()
        fallback = r.u8()
        r.take(2)
        sources.append(dict(id=sid, filename=fn, expected=expected,
                            required=bool(flags & 1), strict=bool(flags & 2),
                            fallback=fallback))

    files = []
    for _ in range(num_files):
        fid = r.u32()
        flags = r.u32()
        offset = r.u32()
        size = r.u32()
        name = r.s16()
        path = r.s16()
        alt = None
        if flags & 4:
            if version < 2:
                raise ValueError('alt tail in a v1 table: the reader would desync here')
            romidx = r.u8()
            alt = dict(rom=romidx, offset=r.u32(), size=r.u32(), compression=r.u8())
        files.append(dict(id=fid, flags=flags, offset=offset, size=size,
                          name=name, path=path, alt=alt))

    texmap = []
    if version >= 3:
        count = r.u32()
        for _ in range(count):
            texmap.append((r.u16(), r.u16()))

    trailing = len(data) - r.p
    return dict(version=version, sources=sources, files=files, texmap=texmap,
                trailing=trailing)


def hx(s):
    return s.encode('utf-8', 'surrogateescape').hex() if s else '-'


def to_spec(t):
    out = []
    for s in t['sources']:
        out.append("S %s %s %u %u %u %u" % (hx(s['id']), hx(s['filename']), s['expected'],
                                            int(s['required']), int(s['strict']), s['fallback']))
    for f in t['files']:
        a = f['alt']
        out.append("F %u %u %u %u %s %s %d %u %u %u %u" % (
            f['id'], 1 if f['flags'] & 1 else 0, f['offset'], f['size'],
            hx(f['name']), hx(f['path']),
            a['rom'] if a else -1, a['offset'] if a else 0,
            a['size'] if a else 0, a['compression'] if a else 0,
            1 if f['flags'] & 8 else 0))
    for local, slot in t['texmap']:
        out.append("T %u %u" % (local, slot))
    return "\n".join(out) + "\n"


def check(path, harness, tmp):
    original = open(path, 'rb').read()
    try:
        t = decode(original)
    except (Trunc, ValueError) as e:
        return f"could not decode: {e}"

    notes = []
    if t['trailing']:
        notes.append(f"{t['trailing']} trailing bytes ignored")

    spec = os.path.join(tmp, 'spec.txt')
    out = os.path.join(tmp, 'out.dat')
    open(spec, 'w').write(to_spec(t))
    r = subprocess.run([harness, spec, out], capture_output=True, text=True)
    if r.returncode != 0:
        return "encoder refused it: " + (r.stderr.strip() or "no reason given")

    got = open(out, 'rb').read()
    if got == original:
        return None if not notes else "OK, but " + "; ".join(notes)

    if len(got) != len(original):
        return f"length {len(got)} vs {len(original)}"
    at = next(i for i in range(len(got)) if got[i] != original[i])
    return (f"differs at byte {at}: wrote {got[at]:#04x}, file has {original[at]:#04x}"
            + (" (" + "; ".join(notes) + ")" if notes else ""))


def main():
    argv = sys.argv[1:]
    harness = argv.pop(0) if argv and not argv[0].endswith('.dat') else '/tmp/pdft_reencode'
    paths = argv
    if not paths:
        here = os.path.dirname(os.path.abspath(__file__))
        root = os.path.abspath(os.path.join(here, '..', '..', '..'))
        paths = sorted(glob.glob(os.path.join(root, 'pd-fojo-basedir', 'data', 'mods', '*', 'filetable.dat')))
    if not paths:
        sys.exit("no filetable.dat found; pass some paths")
    if not os.path.exists(harness):
        sys.exit(f"no harness at {harness}; see the docstring for the build line")

    fails = 0
    with tempfile.TemporaryDirectory() as tmp:
        for p in paths:
            label = os.path.relpath(p)
            problem = check(p, harness, tmp)
            if problem is None:
                size = os.path.getsize(p)
                print(f"ok    {label} ({size} bytes byte-identical)")
            elif problem.startswith("OK,"):
                print(f"ok    {label} — {problem[4:]}")
            else:
                print(f"FAIL  {label}: {problem}")
                fails += 1
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
