#!/usr/bin/env python3
"""netverdict.py <rundir>: read a nettest run and say what it proved.

A run directory holds one folder per process (host, cl1, cl2, ...), each with
the process log (out.txt) and the netplay diag CSV (diag.csv, from --netdiag).
Every check below is a PASS / FAIL / SKIP with one line of detail, written to
verdict.json and a zero-byte PASS or FAIL beside it.  The checks lean on the
`team` / `seat` / `netcl` diag lines (netteam.c) that both ends emit once a
second, so a verdict never needs the game to be watched.

Agreement checks pair host and client samples by net tick (the client adopts
the host's tick on connect), so "the client saw the coop pawn where the host
had it" is a distance between two numbers, not an opinion.
"""
import json, math, os, re, sys, time

POS_TOL = 200.0      # world units, 90th percentile host-vs-client per seat. a running pawn
                     # covers ~260/s, so this is ~0.75 s of interp lag + pairing slack
HEALTH_TOL = 0.1     # bondhealth, 90th percentile
TICK_PAIR_TOL = 8    # ticks, how far apart two samples may be and still pair (both sides
                     # log on the second boundary of the shared net tick)
DRIFT_MAX = 180      # ticks, worst tick-clock drift a process may report
MOVE_MIN = 40.0      # world units, how far a driven pawn must travel in xz during gameplay
CUT_PAIR_TOL = 40    # ticks, a cutscene edge may land this far apart on the two sides (one frame
                     # of wire lag against once-a-second sampling)
WARN_ALLOW = (       # net warnings that do not fail a run
    "tick clock drift",
    "SVC_PROP_MOVE: obj body for syncid 1 unapplied",   # the hub's first move before ids exist
    "prop with syncid 47 does not exist",                # hub door before ids exist (netmsg guard pending)
)


def parse_diag(path):
    """-> dict event -> list of (tick, realtime, fields dict)"""
    out = {}
    try:
        f = open(path, errors="replace")
    except OSError:
        return out
    with f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.rstrip("\n").split(",", 3)
            if len(parts) < 3:
                continue
            try:
                tick = int(parts[0]); rt = float(parts[1])
            except ValueError:
                continue
            ev = parts[2]
            fields = {}
            if len(parts) > 3:
                for m in re.finditer(r'(\w+)=(\([^)]*\)|\S+)', parts[3]):
                    fields[m.group(1)] = m.group(2)
            out.setdefault(ev, []).append((tick, rt, fields))
    return out


def fnum(v, default=None):
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def parse_pos(v):
    m = re.match(r'\(([-\d.]+),([-\d.]+),([-\d.]+)\)', v or "")
    return tuple(float(x) for x in m.groups()) if m else None


def pct(values, p):
    if not values:
        return None
    s = sorted(values)
    return s[min(len(s) - 1, int(math.ceil(p * len(s))) - 1)]


class Proc:
    def __init__(self, rundir, name):
        self.name = name
        self.dir = os.path.join(rundir, name)
        self.diag = parse_diag(os.path.join(self.dir, "diag.csv"))
        try:
            self.log = open(os.path.join(self.dir, "out.txt"), errors="replace").read()
        except OSError:
            self.log = ""
        self.ishost = name == "host"

    def seats_by_tick(self):
        """tick -> {seatindex: fields}"""
        out = {}
        for tick, _, f in self.diag.get("seat", []):
            try:
                out.setdefault(tick, {})[int(f.get("i", -1))] = f
            except ValueError:
                pass
        return out

    def local_seat(self):
        for _, _, f in reversed(self.diag.get("netcl", [])):
            if f.get("local") == "1" and f.get("boundslot") not in (None, "-1"):
                return int(f["boundslot"])
        return None


def check(results, name, ok, detail, skip=False):
    results.append({"check": name, "verdict": "SKIP" if skip else ("PASS" if ok else "FAIL"), "detail": detail})


def run(rundir):
    meta = {}
    try:
        meta = json.load(open(os.path.join(rundir, "run.json")))
    except Exception:
        pass
    names = sorted(d for d in os.listdir(rundir) if os.path.isdir(os.path.join(rundir, d)))
    procs = {n: Proc(rundir, n) for n in names if n == "host" or n.startswith("cl")}
    host = procs.get("host")
    clients = [p for n, p in sorted(procs.items()) if n != "host"]
    results = []
    expect_stage = meta.get("stagenum")
    expect_roles = meta.get("roles")   # list of ints per seat, as the host armed them

    def driver_of(p):
        return meta.get("host_driver" if p.ishost else "client_driver", "walker")

    # 1. boot
    for p in procs.values():
        ev = "server_start" if p.ishost else "client_start"
        check(results, f"{p.name}: booted", ev in p.diag, f"{ev} line {'present' if ev in p.diag else 'missing'}")

    # 2. connect: every client reached state 4 (CLSTATE_GAME) on the host's table
    if host:
        ids = set()
        for _, _, f in host.diag.get("netcl", []):
            if f.get("local") == "0" and f.get("state") == "4":
                ids.add(f.get("id"))
        check(results, "host: clients in game", len(ids) >= len(clients),
              f"{len(ids)} remote client(s) reached CLSTATE_GAME, {len(clients)} expected")
    else:
        check(results, "host: clients in game", False, "no host folder")

    # 3. stage + team flag
    for p in procs.values():
        teams = [f for _, _, f in p.diag.get("team", [])]
        instage = [f for f in teams if expect_stage is None or f.get("stage") == expect_stage]
        isteam = any(f.get("isteam") == "1" for f in instage)
        check(results, f"{p.name}: team stage loaded", bool(instage) and isteam,
              f"{len(instage)} team line(s) at stage {expect_stage or '?'}, isteam={'1' if isteam else '0'}")

    # 4. roles: the manifest is the same everywhere; each client sits in its own seat
    def roles_of(p):
        last = {}
        for tick, _, f in p.diag.get("seat", []):
            last[int(f.get("i", -1))] = f.get("role")
        return [last.get(i) for i in range(max(last.keys()) + 1)] if last else []
    if host:
        hroles = roles_of(host)
        for c in clients:
            croles = roles_of(c)
            check(results, f"{c.name}: roles match host", croles == hroles and bool(croles),
                  f"client {croles} host {hroles}")
            ls = c.local_seat()
            want = expect_roles[ls] if (expect_roles and ls is not None and ls < len(expect_roles)) else None
            got = croles[ls] if (ls is not None and ls < len(croles)) else None
            check(results, f"{c.name}: own seat role", ls is not None and ls != 0 and (want is None or str(want) == got),
                  f"local seat {ls} role {got} (armed {want})")

    # 5. gameplay reached (cutscene over)
    for p in procs.values():
        play = [f for _, _, f in p.diag.get("team", []) if f.get("tickmode") == "1" and f.get("cutscene") == "0"
                and (expect_stage is None or f.get("stage") == expect_stage)]
        check(results, f"{p.name}: gameplay reached", bool(play), f"{len(play)} second(s) of gameplay logged")

    # 5a. the intro plays in lockstep: the seconds a process spends in
    # tickmode 6 (cutscene) at the stage, host vs client, keyed by net tick.
    # the client's scripts hold at 0111/00dd on the host's SVC_CUTSCENE, so
    # the two sets should differ by at most a second or two at each edge.
    if host:
        def cut_ticks(p):
            return {t for t, _, f in p.diag.get("team", []) if f.get("tickmode") == "6"
                    and (expect_stage is None or f.get("stage") == expect_stage)}
        hcut = cut_ticks(host)
        for c in clients:
            ccut = cut_ticks(c)
            if not hcut and not ccut:
                check(results, f"{c.name}: cutscene in lockstep", False, "no cutscene seconds on either side", skip=True)
                continue
            # pair each second with the nearest on the other side (±TICK_PAIR_TOL)
            def unmatched(a, b):
                return sum(1 for t in a if not any(abs(t - u) <= CUT_PAIR_TOL for u in b))
            only_c = unmatched(ccut, hcut)
            only_h = unmatched(hcut, ccut)
            check(results, f"{c.name}: cutscene in lockstep", only_c <= 2 and only_h <= 2,
                  f"host {len(hcut)} s in cutscene, client {len(ccut)} s; {only_c} client-only, {only_h} host-only seconds")

        # 5a'. while the intro plays, the non-operative seats are hidden on
        # every machine (the setup's set_chr_chrflag(CHR_COOP/ANTI, HIDDEN)).
        # a client that raced the scene showed them standing on their pads.
        for p in procs.values():
            cut = cut_ticks(p)
            if not cut:
                check(results, f"{p.name}: seats hidden in the intro", False, "no cutscene seconds", skip=True)
                continue
            sb = p.seats_by_tick()
            shown = 0
            samples = 0
            for t in cut:
                for i, f in sb.get(t, {}).items():
                    if i == 0 or "hidden" not in f:
                        continue
                    samples += 1
                    if f.get("hidden") != "1":
                        shown += 1
            if samples == 0:
                check(results, f"{p.name}: seats hidden in the intro", False, "no seat samples carry hidden=", skip=True)
            else:
                # the first second or two before the list reaches the hide is tolerated
                check(results, f"{p.name}: seats hidden in the intro", shown <= 2,
                      f"{shown} of {samples} cutscene seat-seconds visible")

    # 5a''. the mission didn't end on its own: no endscreen on any machine
    # during a walk run (an instafail - a critical chr dead - shows here).
    for p in procs.values():
        ends = [f for _, _, f in p.diag.get("team", []) if f.get("endscreen") not in (None, "0")
                and (expect_stage is None or f.get("stage") == expect_stage)]
        check(results, f"{p.name}: mission didn't end early", not ends,
              f"{len(ends)} second(s) of endscreen" if ends else "no endscreen")

    # 5b. the local pawn moved: a frozen pawn (pad routed to a controller that
    # doesn't exist) agrees with the host perfectly and proves nothing.
    for p in procs.values():
        if driver_of(p) != "walker":
            continue
        ls = 0 if p.ishost else p.local_seat()
        playticks = {t for t, _, f in p.diag.get("team", []) if f.get("tickmode") == "1" and f.get("cutscene") == "0"}
        pts = [parse_pos(f.get("pos")) for t, seats in p.seats_by_tick().items() if t in playticks
               for i, f in seats.items() if i == ls]
        pts = [q for q in pts if q][3:]   # the first seconds still carry the spawn teleport
        if len(pts) < 5:
            check(results, f"{p.name}: own pawn moved", False, f"seat {ls}: {len(pts)} gameplay sample(s)", skip=ls is None)
        else:
            x0, _, z0 = pts[0]
            far = max(math.hypot(x - x0, z - z0) for x, _, z in pts)
            check(results, f"{p.name}: own pawn moved", far >= MOVE_MIN, f"seat {ls} travelled {far:.0f} units across {len(pts)} gameplay samples")

    # 5c. the frame isn't black. frame.png is a root-window grab of each
    # process's xvfb display at 3/4 of the run (nettest, ImageMagick import).
    for p in procs.values():
        fp = os.path.join(p.dir, "frame.png")
        if not os.path.exists(fp):
            check(results, f"{p.name}: frame drawn", False, "no frame.png (no xvfb / no ImageMagick)", skip=True)
            continue
        try:
            from PIL import Image
            im = Image.open(fp).convert("L")
            px = list(im.get_flattened_data() if hasattr(im, "get_flattened_data") else im.getdata())
            mean = sum(px) / max(1, len(px))
            lit = sum(1 for v in px if v > 24) / max(1, len(px))
            check(results, f"{p.name}: frame drawn", mean > 6.0 and lit > 0.05,
                  f"mean {mean:.1f}/255, {lit*100:.0f}% of pixels lit")
        except Exception as e:  # PIL missing, bad png
            check(results, f"{p.name}: frame drawn", False, f"could not read frame.png: {e}", skip=True)

    # 6/7. agreement: host vs each client, per seat, paired by tick
    if host:
        hs = host.seats_by_tick()
        hticks = sorted(hs)
        for c in clients:
            cs = c.seats_by_tick()
            dists, dhp, pairs = {}, {}, 0
            for ct, seats in cs.items():
                # nearest host sample
                best = None
                for ht in hticks:
                    if abs(ht - ct) <= TICK_PAIR_TOL and (best is None or abs(ht - ct) < abs(best - ct)):
                        best = ht
                if best is None:
                    continue
                pairs += 1
                for i, f in seats.items():
                    hf = hs[best].get(i)
                    if not hf:
                        continue
                    a, b = parse_pos(f.get("pos")), parse_pos(hf.get("pos"))
                    if a and b:
                        dists.setdefault(i, []).append(math.dist(a, b))
                    ha, hb = fnum(f.get("health")), fnum(hf.get("health"))
                    if ha is not None and hb is not None:
                        dhp.setdefault(i, []).append(abs(ha - hb))
            if not pairs:
                check(results, f"{c.name}: positions agree with host", False, "no paired samples")
                continue
            worst = max((pct(v, 0.9) for v in dists.values()), default=0.0)
            detail = ", ".join(f"seat{i} p90 {pct(v,0.9):.0f} max {max(v):.0f}" for i, v in sorted(dists.items()))
            check(results, f"{c.name}: positions agree with host", worst <= POS_TOL, f"{pairs} paired seconds; {detail}")
            worsth = max((pct(v, 0.9) for v in dhp.values()), default=0.0)
            check(results, f"{c.name}: health agrees with host", worsth <= HEALTH_TOL,
                  ", ".join(f"seat{i} p90 {pct(v,0.9):.2f}" for i, v in sorted(dhp.items())))

    # 8. stability: crashes, stalls, guards, net warnings
    for p in procs.values():
        bad = [l for l in p.log.splitlines() if re.search(r"FATAL|watchdog: main loop stalled|anim walk of|proptick_guard|propssort_guard|gfx_run_dl: >", l)]
        check(results, f"{p.name}: no crash or stall", not bad, bad[0][:140] if bad else "clean")
        warns = {}
        for l in p.log.splitlines():
            m = re.search(r"WARNING: NET: (.*)", l)
            if m and not any(a in m.group(1) for a in WARN_ALLOW):
                k = re.sub(r"\d+", "N", m.group(1))[:90]
                warns[k] = warns.get(k, 0) + 1
        check(results, f"{p.name}: no net warnings", not warns,
              "; ".join(f"{v}x {k}" for k, v in sorted(warns.items(), key=lambda kv: -kv[1])[:4]) or "clean")

    # 9. the prop-sync auditor
    for p in procs.values():
        audits = p.diag.get("audit", [])
        fails = [f for _, _, f in audits if f.get("result") not in (None, "PASS")]
        check(results, f"{p.name}: prop audit", not fails, f"{len(audits)} cycle(s), {len(fails)} not PASS", skip=not audits)

    # 10. tick clock drift
    for p in procs.values():
        drifts = [abs(int(fnum(f.get("drift"), 0))) for _, _, f in p.diag.get("tickdrift", [])]
        check(results, f"{p.name}: tick drift bounded", max(drifts, default=0) <= DRIFT_MAX,
              f"worst {max(drifts, default=0)} ticks over {len(drifts)} report(s)")

    verdict = "FAIL" if any(r["verdict"] == "FAIL" for r in results) else "PASS"
    out = {
        "verdict": verdict,
        "scenario": meta.get("scenario"),
        "started": meta.get("started"),
        "finished": meta.get("finished", int(time.time())),
        "secs": meta.get("secs"),
        "sha": meta.get("sha"),
        "procs": list(procs.keys()),
        "checks": results,
        "pass": sum(r["verdict"] == "PASS" for r in results),
        "fail": sum(r["verdict"] == "FAIL" for r in results),
        "skip": sum(r["verdict"] == "SKIP" for r in results),
    }
    json.dump(out, open(os.path.join(rundir, "verdict.json"), "w"), indent=1)
    for v in ("PASS", "FAIL"):
        try:
            os.remove(os.path.join(rundir, v))
        except OSError:
            pass
    open(os.path.join(rundir, verdict), "w").close()
    width = max(len(r["check"]) for r in results)
    for r in results:
        print(f"{r['verdict']:4} {r['check']:<{width}}  {r['detail']}")
    print(verdict)
    return 0 if verdict == "PASS" else 1


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__); sys.exit(2)
    sys.exit(run(sys.argv[1]))
