#!/usr/bin/env python3
"""Differential test for port/src/rompatch.c.

Builds reference BPS and IPS patches here, in Python, from the format specs,
runs them through the C decoder, and requires the result to be byte-identical
to the target the patch was built from.  Covers the cases that are easy to get
wrong: IPS RLE runs, BPS SourceCopy with forward *and* backward relative
offsets, and BPS TargetCopy reading bytes it is still writing.  Also checks
that malformed and mismatched patches are refused rather than misread.

    cc -O1 -D_LANGUAGE_C -Iport/include -Iinclude \
        -o /tmp/rompatch_selftest tools/rompatch_selftest.c port/src/rompatch.c -lz
    python3 tools/rompatch_test.py /tmp/rompatch_selftest

(from pd-fojo/.  -D_LANGUAGE_C is what turns on the typedefs in
include/PR/ultratypes.h outside a real port build.)

xdelta/VCDIFF is NOT covered: it needs either xdelta3 itself or a VCDIFF
encoder, and neither is available offline.  Run the same comparison against
xdelta3 output before trusting that path.
"""
import os, random, struct, subprocess, sys, tempfile, zlib

HARNESS = sys.argv[1] if len(sys.argv) > 1 else "/tmp/rompatch_selftest"


def bps_varint(n):
    out = bytearray()
    while True:
        x = n & 0x7f
        n >>= 7
        if n == 0:
            out.append(0x80 | x)
            break
        out.append(x)
        n -= 1
    return bytes(out)


def bps_signed(d):
    return bps_varint((abs(d) << 1) | (1 if d < 0 else 0))


def bps_seal(p, src, tgt):
    p += struct.pack('<III', zlib.crc32(src) & 0xffffffff,
                     zlib.crc32(tgt) & 0xffffffff, 0)
    p[-4:] = struct.pack('<I', zlib.crc32(bytes(p[:-4])) & 0xffffffff)
    return bytes(p)


def make_bps(src, tgt, meta=b''):
    """SourceRead for the common prefix, TargetRead for the rest."""
    p = bytearray(b'BPS1')
    p += bps_varint(len(src)) + bps_varint(len(tgt)) + bps_varint(len(meta)) + meta
    common = 0
    while common < min(len(src), len(tgt)) and src[common] == tgt[common]:
        common += 1
    if common:
        p += bps_varint(((common - 1) << 2) | 0)
    rest = len(tgt) - common
    if rest:
        p += bps_varint(((rest - 1) << 2) | 1) + tgt[common:]
    return bps_seal(p, src, tgt)


def make_bps_sourcecopy(src, order, blk):
    """Source blocks emitted out of order, so offsets run both ways."""
    tgt = b''.join(src[i * blk:(i + 1) * blk] for i in order)
    p = bytearray(b'BPS1')
    p += bps_varint(len(src)) + bps_varint(len(tgt)) + bps_varint(0)
    srcrel = 0
    for i in order:
        srcoff = i * blk
        p += bps_varint(((blk - 1) << 2) | 2) + bps_signed(srcoff - srcrel)
        srcrel = srcoff + blk
    return bps_seal(p, src, tgt), tgt


def make_bps_targetcopy(src, keep, pair, runlen):
    """TargetCopy over bytes still being written — BPS's run-length idiom."""
    tgt = src[:keep] + pair * (runlen // len(pair))
    p = bytearray(b'BPS1')
    p += bps_varint(len(src)) + bps_varint(len(tgt)) + bps_varint(0)
    p += bps_varint(((keep - 1) << 2) | 0)
    p += bps_varint(((len(pair) - 1) << 2) | 1) + pair
    n = len(tgt) - keep - len(pair)
    p += bps_varint(((n - 1) << 2) | 3) + bps_signed(keep)
    return bps_seal(p, src, tgt), tgt


def make_ips(src, tgt):
    assert len(src) == len(tgt), "IPS cannot change the file length"
    p = bytearray(b'PATCH')
    i = 0
    while i < len(tgt):
        if src[i] == tgt[i]:
            i += 1
            continue
        j = i
        while j < len(tgt) and src[j] != tgt[j] and j - i < 0xffff:
            j += 1
        chunk = tgt[i:j]
        p += bytes([(i >> 16) & 0xff, (i >> 8) & 0xff, i & 0xff])
        if len(set(chunk)) == 1 and len(chunk) > 3:
            p += b'\x00\x00' + bytes([(len(chunk) >> 8) & 0xff, len(chunk) & 0xff]) + bytes([chunk[0]])
        else:
            p += bytes([(len(chunk) >> 8) & 0xff, len(chunk) & 0xff]) + chunk
        i = j
    return bytes(p + b'EOF')


def apply(rom, patch, tmp):
    rp, pp, op = (os.path.join(tmp, n) for n in ('rom.bin', 'p.bin', 'out.bin'))
    open(rp, 'wb').write(rom)
    open(pp, 'wb').write(patch)
    r = subprocess.run([HARNESS, rp, pp, op], capture_output=True, text=True)
    if r.returncode != 0:
        return None, r.stderr.strip().replace('FAIL: ', '')
    return open(op, 'rb').read(), None


def main():
    if not os.path.exists(HARNESS):
        sys.exit(f"no harness at {HARNESS} — see the docstring for how to build it")
    random.seed(20260906)
    src = bytes(random.randrange(256) for _ in range(4096))
    tgt = bytearray(src)
    for _ in range(40):
        o = random.randrange(0, 4000)
        n = random.randrange(1, 60)
        tgt[o:o + n] = bytes(random.randrange(256) for _ in range(n))
    tgt[1000:1120] = b'\x5a' * 120
    tgt = bytes(tgt)
    order = list(range(len(src) // 64))
    random.shuffle(order)
    sc_patch, sc_tgt = make_bps_sourcecopy(src, order, 64)
    tc_patch, tc_tgt = make_bps_targetcopy(src, 100, b'\xab\xcd', 400)

    cases = [
        ("IPS, literal runs and RLE", make_ips(src, tgt), tgt),
        ("BPS, SourceRead + TargetRead", make_bps(src, tgt), tgt),
        ("BPS, SourceCopy with offsets both ways", sc_patch, sc_tgt),
        ("BPS, TargetCopy over its own output", tc_patch, tc_tgt),
        ("BPS, metadata block", make_bps(src, tgt, b'{"note":"x"}'), tgt),
    ]
    good = make_bps(src, src[:2000] + b'z' * 96)
    other = bytes(random.randrange(256) for _ in range(4096))
    refusals = [
        ("corrupted target checksum", good[:-8] + b'\x00' * 4 + good[-4:], src),
        ("wrong base ROM", good, other),
        ("truncated mid-action", good[:len(good) // 2], src),
        ("no magic at all", b'NOTAPATCH' + bytes(200), src),
    ]

    fails = 0
    with tempfile.TemporaryDirectory() as tmp:
        for name, patch, want in cases:
            got, err = apply(src, patch, tmp)
            if got is None:
                print(f"FAIL  {name}: decoder refused a valid patch — {err}")
                fails += 1
            elif got != want:
                d = next((i for i in range(min(len(got), len(want))) if got[i] != want[i]),
                         min(len(got), len(want)))
                print(f"FAIL  {name}: got {len(got)}B want {len(want)}B, first difference at {d}")
                fails += 1
            else:
                print(f"ok    {name} ({len(want)} bytes byte-identical)")
        for name, patch, rom in refusals:
            got, err = apply(rom, patch, tmp)
            if got is None:
                print(f"ok    refuses {name}: {err}")
            else:
                print(f"FAIL  accepted {name}, should have refused")
                fails += 1
    print("\nxdelta/VCDIFF is not covered here — see the module docstring.")
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
