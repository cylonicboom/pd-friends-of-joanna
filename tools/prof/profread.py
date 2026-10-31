#!/usr/bin/env python3
"""profread: read a pd --profdump trace (Fuchsia Trace Format, .fxt).

    tools/prof/profread.py run.fxt                 per-zone, per-seat table
    tools/prof/profread.py run.fxt --frames        per-frame totals as CSV
    tools/prof/profread.py a.fxt --diff b.fxt      ratio table b/a (the dedupe table)
    tools/prof/profread.py run.fxt --counters      count series (props ticked, net bytes per message id, ...)
    tools/prof/profread.py run.fxt --events [NAME|all]   engine events (chr.death, player.die, tickmode, music.*, hud.label ...)
    tools/prof/profread.py run.fxt --after 600 --before 1200   frame window

/mark <text> in the chat box or the server console drops a marker; the table
header lists them with their frame numbers, so --after/--before can window
on what you saw.

The timeline view is not here: open the .fxt in https://ui.perfetto.dev
(it reads the format directly; seats are tracks). This tool is the table
side - no dependencies, reads a truncated file up to the last whole record.

Format: port/src/prof.c writes it; this parses the subset it writes plus
enough of the rest to skip what it doesn't know.
"""
import argparse
import struct
import sys
from collections import defaultdict

REC_METADATA, REC_INIT, REC_STRING, REC_THREAD, REC_EVENT, REC_KOBJ = 0, 1, 2, 3, 4, 7
EV_INSTANT, EV_COUNTER, EV_BEGIN, EV_END = 0, 1, 2, 3
MAGIC = 0x0016547846040010


class Trace:
    def __init__(self):
        self.ticks_per_sec = 1_000_000_000
        self.strings = {0: ""}
        self.threads = {}        # thread ref -> (pkoid, tkoid)
        self.thread_names = {}   # tkoid -> name
        self.process_name = ""
        # per (seat name, zone name): [count, total_ticks, max_ticks, self_ticks]
        self.cells = defaultdict(lambda: [0, 0, 0, 0])
        self.frames = []         # (frame number, ts)
        self.marks = []          # (frame number, ts, text) from /mark
        self.events = []         # (frame number, ts, seat, name, {arg: value}) engine-emitted
        self.zone_names = set()
        self.counters = defaultdict(list)   # (seat, name, id) -> [(ts, value)]
        self.records = 0
        self.truncated = False
        self.first_ts = None
        self.last_ts = None

    def seat_name(self, ref):
        if ref in self.threads:
            return self.thread_names.get(self.threads[ref][1], "t%d" % ref)
        return "inline"


def parse(path, after=None, before=None):
    t = Trace()
    data = open(path, "rb").read()
    n = len(data) // 8
    words = struct.unpack_from("<%dQ" % n, data, 0)
    if len(data) % 8:
        t.truncated = True
    i = 0
    # per-seat open-zone stacks: ref -> [(zone name, ts, child ticks)]
    stacks = defaultdict(list)
    cur_frame = None
    in_window = after is None

    while i < n:
        hdr = words[i]
        rtype = hdr & 0xf
        size = (hdr >> 4) & 0xfff
        if size == 0 or i + size > n:
            t.truncated = True
            break
        t.records += 1
        if rtype == REC_METADATA:
            pass
        elif rtype == REC_INIT:
            t.ticks_per_sec = words[i + 1]
        elif rtype == REC_STRING:
            idx = (hdr >> 16) & 0x7fff
            ln = (hdr >> 32) & 0x7fff
            raw = data[(i + 1) * 8:(i + 1) * 8 + ln]
            t.strings[idx] = raw.decode("utf-8", "replace")
        elif rtype == REC_THREAD:
            ref = (hdr >> 16) & 0xff
            t.threads[ref] = (words[i + 1], words[i + 2])
        elif rtype == REC_KOBJ:
            objtype = (hdr >> 16) & 0xff
            nameref = (hdr >> 24) & 0xffff
            koid = words[i + 1]
            name = t.strings.get(nameref, "?")
            if objtype == 1:
                t.process_name = name
            elif objtype == 2:
                t.thread_names[koid] = name
        elif rtype == REC_EVENT:
            evtype = (hdr >> 16) & 0xf
            nargs = (hdr >> 20) & 0xf
            tref = (hdr >> 24) & 0xff
            nameref = (hdr >> 48) & 0xffff
            name = t.strings.get(nameref, "?")
            ts = words[i + 1]
            if t.first_ts is None:
                t.first_ts = ts
            t.last_ts = ts
            # arguments start at word 2 (thread and strings are always refs here)
            j = i + 2
            args = []
            for _ in range(nargs):
                ah = words[j]
                atype = ah & 0xf
                asize = (ah >> 4) & 0xfff
                aname = t.strings.get((ah >> 16) & 0xffff, "?")
                if atype in (1, 2):
                    val = (ah >> 32) & 0xffffffff
                elif atype in (3, 4) and asize >= 2:
                    val = words[j + 1]
                elif atype == 6:
                    sref = (ah >> 32) & 0xffff
                    if sref & 0x8000:
                        ln = sref & 0x7fff
                        val = data[(j + 1) * 8:(j + 1) * 8 + ln].decode("utf-8", "replace")
                    else:
                        val = t.strings.get(sref, "?")
                else:
                    val = None
                args.append((aname, val))
                j += max(asize, 1)
            if evtype == EV_INSTANT and name == "mark":
                t.marks.append((cur_frame, ts, args[0][1] if args else ""))
            elif evtype == EV_INSTANT and name != "frame":
                if in_window:
                    t.events.append((cur_frame, ts, t.seat_name(tref), name, dict(args)))
            elif evtype == EV_INSTANT and name == "frame":
                cur_frame = args[0][1] if args else None
                in_window = (after is None or (cur_frame is not None and cur_frame >= after)) and \
                            (before is None or (cur_frame is not None and cur_frame < before))
                if in_window:
                    t.frames.append((cur_frame, ts))
            elif evtype == EV_BEGIN:
                stacks[tref].append([name, ts, 0])
            elif evtype == EV_END:
                st = stacks[tref]
                if st:
                    zname, t0, child = st.pop()
                    incl = ts - t0
                    if st:
                        st[-1][2] += incl
                    if in_window:
                        c = t.cells[(t.seat_name(tref), zname)]
                        c[0] += 1
                        c[1] += incl
                        c[2] = max(c[2], incl)
                        c[3] += incl - child
            elif evtype == EV_COUNTER:
                cid = words[j] if j < n else 0
                if in_window and args:
                    t.counters[(t.seat_name(tref), name, cid)].append((ts, args[0][1]))
        i += size
    return t


def fmt_ms(ticks, tps):
    return ticks * 1000.0 / tps


def table(t, frames):
    tps = t.ticks_per_sec
    nf = max(len(t.frames), 1)
    rows = sorted(t.cells.items(), key=lambda kv: -kv[1][1])
    print("# %s  frames=%d  records=%d%s" % (t.process_name, len(t.frames), t.records,
          "  TRUNCATED" if t.truncated else ""))
    if t.first_ts is not None and t.last_ts is not None:
        print("# span %.1f s" % ((t.last_ts - t.first_ts) / tps))
    for fnum, ts, text in t.marks:
        print("# mark @ frame %s, %.1f s: %s" % (fnum, (ts - t.first_ts) / tps, text))
    print("%-8s %-16s %9s %9s %9s %9s %8s" % ("seat", "zone", "self/fr", "incl/fr", "max", "total", "calls/fr"))
    print("%-8s %-16s %9s %9s %9s %9s %8s" % ("", "", "ms", "ms", "ms", "ms", ""))
    for (seat, zone), (calls, incl, mx, self_t) in rows:
        print("%-8s %-16s %9.3f %9.3f %9.3f %9.1f %8.1f" % (
            seat, zone, fmt_ms(self_t, tps) / nf, fmt_ms(incl, tps) / nf, fmt_ms(mx, tps), fmt_ms(incl, tps), calls / nf))


def diff(a, b):
    """b over a, per cell, by inclusive time per frame."""
    nfa, nfb = max(len(a.frames), 1), max(len(b.frames), 1)
    keys = sorted(set(a.cells) | set(b.cells), key=lambda k: -(b.cells[k][1] / nfb if k in b.cells else 0))
    print("%-8s %-16s %10s %10s %7s %8s %8s" % ("seat", "zone", "a ms/fr", "b ms/fr", "b/a", "a calls", "b calls"))
    for k in keys:
        ca = a.cells.get(k, [0, 0, 0, 0])
        cb = b.cells.get(k, [0, 0, 0, 0])
        ma = fmt_ms(ca[1], a.ticks_per_sec) / nfa
        mb = fmt_ms(cb[1], b.ticks_per_sec) / nfb
        ratio = ("%7.2f" % (mb / ma)) if ma > 0 else "      -"
        print("%-8s %-16s %10.3f %10.3f %7s %8.1f %8.1f" % (k[0], k[1], ma, mb, ratio, ca[0] / nfa, cb[0] / nfb))


def counters(t):
    tps = t.ticks_per_sec
    print("%-8s %-16s %6s %9s %12s %12s" % ("seat", "counter", "id", "samples", "sum", "mean"))
    for (seat, name, cid), vals in sorted(t.counters.items(), key=lambda kv: (kv[0][1], kv[0][2], kv[0][0])):
        vs = [v for _, v in vals if v is not None]
        if not vs:
            continue
        print("%-8s %-16s %6d %9d %12d %12.1f" % (seat, name, cid, len(vs), sum(vs), sum(vs) / len(vs)))


def events(t, which=None):
    """engine-emitted instant events, one per line, oldest first"""
    tps = t.ticks_per_sec
    f0 = t.first_ts or 0
    if not which:
        from collections import Counter
        c = Counter(e[3] for e in t.events)
        print("%-16s %8s" % ("event", "n"))
        for k, n in sorted(c.items()):
            print("%-16s %8d" % (k, n))
        print("(--events NAME for the lines; NAME=all for everything)")
        return
    print("%8s %9s %-8s %-16s %10s %12s %9s %9s" % ("frame", "t(s)", "seat", "event", "id", "value", "nettick", "lvframe60"))
    for fnum, ts, seat, name, a in t.events:
        if which != "all" and name != which:
            continue
        value = a.get("value", "")
        if name == "music.queue" and isinstance(value, int):
            # eventtype<<24 | tracktype<<16 | tracknum: 1 play, 2 stop, 3 fade
            kind = {1: "play", 2: "stop", 3: "fade"}.get(value >> 24, str(value >> 24))
            value = "%s t%d #%d" % (kind, (value >> 16) & 0xff, value & 0xffff)
        print("%8s %9.3f %-8s %-16s %10s %12s %9s %9s" % (
            fnum if fnum is not None else "-", (ts - f0) / tps, seat, name,
            a.get("id", ""), value, a.get("nettick", ""), a.get("lvframe60", "")))


def frames_csv(t):
    """one line per frame: frame number, wall ms since the first frame, then the frame-track zones' inclusive ms"""
    # re-walk is simpler than keeping per-frame cells in parse(); do a cheap second pass keyed by frame marks
    print("frame,ms")
    f0 = t.frames[0][1] if t.frames else 0
    for fnum, ts in t.frames:
        print("%d,%.3f" % (fnum, fmt_ms(ts - f0, t.ticks_per_sec)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("trace")
    ap.add_argument("--diff", metavar="OTHER", help="second trace; print OTHER/trace ratios")
    ap.add_argument("--counters", action="store_true")
    ap.add_argument("--events", nargs="?", const="", metavar="NAME", help="engine events: no NAME lists kinds and counts; NAME (or all) prints them")
    ap.add_argument("--frames", action="store_true")
    ap.add_argument("--after", type=int, help="first frame number to include")
    ap.add_argument("--before", type=int, help="frame number to stop at")
    a = ap.parse_args()
    t = parse(a.trace, a.after, a.before)
    if a.diff:
        diff(t, parse(a.diff, a.after, a.before))
    elif a.counters:
        counters(t)
    elif a.events is not None:
        events(t, a.events)
    elif a.frames:
        frames_csv(t)
    else:
        table(t, a.frames)
    return 0


if __name__ == "__main__":
    sys.exit(main())
