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

POS_TOL = 120.0      # world units, 90th percentile distance from a client sample to the host's
                     # path (see POS_LAG_TOL); the old same-tick metric needed 200 for lag alone
HEALTH_TOL = 0.1     # bondhealth, 90th percentile
POS_LAG_TOL = 90     # ticks, how far along the host's path a client sample may lag or lead
TICK_PAIR_TOL = 8    # ticks, how far apart two samples may be and still pair (both sides
                     # log on the second boundary of the shared net tick)
DRIFT_MAX = 180      # ticks, worst tick-clock drift a process may report
MOVE_MIN = 40.0      # world units, how far a driven pawn must travel in xz during gameplay
CUT_PAIR_TOL = 40    # ticks, a cutscene edge may land this far apart on the two sides (one frame
                     # of wire lag against once-a-second sampling)
ANIM_LOOP_MISS = 0.05  # fraction of paired chr-anim samples whose looping flag may disagree with the host's
ANIM_FROZEN_MAX = 0.02 # fraction of paired samples where the client's copy sat 3+ s on one frame while the host's moved
ANIM_SNAP_MAX = 0.05   # re-seeds per judged frame the client's anim correction may need (more is a snap storm)
ANIM_ERR_MAX = 3.0     # frames, mean |error| the client's anim correction may see
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


def seg_dist(p, a, b):
    """distance from point p to segment ab (3-vectors)"""
    ab = [b[k] - a[k] for k in range(3)]
    ap = [p[k] - a[k] for k in range(3)]
    l2 = sum(v * v for v in ab)
    if l2 <= 1e-9:
        return math.dist(p, a)
    t = max(0.0, min(1.0, sum(ap[k] * ab[k] for k in range(3)) / l2))
    q = [a[k] + t * ab[k] for k in range(3)]
    return math.dist(p, q)


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

    # 4a. every machine dresses each seat in that seat's own player's carousel
    # pick (teamagentindex, the seat line's agent=): the pick rides the wire
    # (proto 90) and follows the local pawn to its seat. before, a client's
    # pawn wore its seat's default and its pick sat on config slot 0 - the
    # host's seat on that client.
    def agents_of(p):
        last = {}
        for tick, _, f in p.diag.get("seat", []):
            if "agent" in f:
                last[int(f.get("i", -1))] = f.get("agent")
        return [last.get(i) for i in range(max(last.keys()) + 1)] if last else []
    if host:
        hag = agents_of(host)
        for c in clients:
            cag = agents_of(c)
            if not hag and not cag:
                check(results, f"{c.name}: seat picks match host", False, "no seat line carries agent=", skip=True)
            else:
                check(results, f"{c.name}: seat picks match host", cag == hag,
                      f"client {cag} host {hag}")
            # and the client's own seat wears the client's own pick (its
            # netcl local=1 agent=, read off its profile at connect)
            mine = [f.get("agent") for _, _, f in c.diag.get("netcl", []) if f.get("local") == "1" and "agent" in f]
            ls = c.local_seat()
            if not mine or ls is None:
                check(results, f"{c.name}: own seat wears own pick", False, "no local netcl agent= or no local seat", skip=True)
            else:
                want = mine[-1]
                gotc = cag[ls] if ls < len(cag) else None
                goth = hag[ls] if ls < len(hag) else None
                check(results, f"{c.name}: own seat wears own pick", gotc == want and goth == want,
                      f"pick {want}; seat {ls} wears {gotc} here, {goth} on the host")

    # 4d. back to the lobby (EXPECT_LOOP): every machine goes mission -> hub
    # -> mission - the endscreen hands everyone back to the hub, the ready-up
    # starts the mission again.
    if meta.get("expect_loop"):
        for p in procs.values():
            seq = []
            for _, _, f in p.diag.get("team", []):
                st = f.get("stage")
                if st and (not seq or seq[-1] != st):
                    seq.append(st)
            hub = "0x26"
            want = expect_stage
            ok = False
            for i, st in enumerate(seq):
                if st == want and hub in seq[i + 1:]:
                    j = seq.index(hub, i + 1)
                    if want in seq[j + 1:]:
                        ok = True
                        break
            check(results, f"{p.name}: mission, lobby, mission", ok, f"stages {' -> '.join(seq)}")

    # 4c. --dedicated: the host's pawn is a ghost on every machine - hidden
    # (no render, no collision) and never hurt - for the whole of gameplay.
    # the host's seat is 0 (it is wire client 0).
    if host and any(f.get("ghost") == "1" for _, _, f in host.diag.get("team", [])):
        for p in procs.values():
            # live gameplay only: the endscreen freezes the stage (the ghost
            # tick does not run while nothing advances)
            play = {t for t, _, f in p.diag.get("team", []) if f.get("tickmode") == "1" and f.get("cutscene") == "0"
                    and f.get("endscreen") in (None, "0")
                    and (expect_stage is None or f.get("stage") == expect_stage)}
            sb = p.seats_by_tick()
            samples = [sb[t][0] for t in sorted(play) if t in sb and 0 in sb[t]]
            if not samples:
                check(results, f"{p.name}: host seat is a ghost", False, "no gameplay samples of seat 0", skip=True)
                continue
            shown = sum(1 for f in samples[2:] if f.get("hidden") != "1")  # the first two seconds may race the intro's unhide
            hurt = sum(1 for f in samples if f.get("dead") == "1" or float(f.get("health", "1")) < 0.999)
            check(results, f"{p.name}: host seat is a ghost", shown == 0 and hurt == 0,
                  f"{len(samples)} gameplay seconds: {shown} not hidden, {hurt} hurt or dead")

    # 4b. the operative is wherever the BOND role (3) sits, on every machine:
    # team lines' bond= is that seat's players[] index (seats are dense, so
    # the seat number). the operative used to be seat 0 by rule; it can be any.
    for p in procs.values():
        proles = roles_of(p)
        if "3" not in proles:
            check(results, f"{p.name}: operative seat", False, f"no seat has the operative role in {proles}")
            continue
        want = proles.index("3")
        bonds = {f.get("bond") for _, _, f in p.diag.get("team", [])
                 if expect_stage is None or f.get("stage") == expect_stage}
        check(results, f"{p.name}: operative seat", bonds == {str(want)},
              f"bond= {sorted(bonds)} in team lines, operative role on seat {want}")

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
                    # the operative is the one the intro shows; any seat can be it
                    if f.get("role") == "3" or "hidden" not in f:
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
    # host-left-alone: cl1 leaves mid-mission, a dedicated host must go back to
    # the hub on its own, and the late client must get in (not "rejected: team
    # mission in progress") and reach a mission.
    if meta.get("expect_rejoin"):
        def log_of(name):
            for fn in ("pd.log", "out.txt"):
                try:
                    return open(os.path.join(rundir, name, fn), errors="replace").read()
                except OSError:
                    pass
            return ""
        hostlog = log_of("host")
        late = sorted((n for n in procs if n.startswith("cl")), key=lambda n: int(n[2:] or 0))[-1]
        latelog = log_of(late)
        check(results, "host: back to the lobby when everyone left", "everyone left the mission" in hostlog
              and "back to the lobby" in hostlog, "saw the empty-mission return" if "everyone left the mission" in hostlog else "never noticed it was alone")
        rej = hostlog.count("rejected: team mission in progress")
        check(results, "host: nobody refused as late", rej == 0, f"{rej} refusal(s)")
        stages = []
        for _, _, f in procs[late].diag.get("team", []):
            st = f.get("stage")
            if st and (not stages or stages[-1] != st):
                stages.append(st)
        check(results, f"{late}: joined after cl1 left", "CLSTATE" not in latelog and "Couldn't join" not in latelog
              and "disconnected from server" not in latelog and bool(stages), f"stages {' -> '.join(stages) or '(no diag)'}")
        check(results, f"{late}: reached a mission", any(s != "0x26" for s in stages), f"stages {' -> '.join(stages) or '(none)'}")

    # during a walk run (an instafail - a critical chr dead - shows here).
    for p in procs.values():
        ends = [f for _, _, f in p.diag.get("team", []) if f.get("endscreen") not in (None, "0")
                and (expect_stage is None or f.get("stage") == expect_stage)]
        if meta.get("expect_loop"):
            # the loop ends the mission on purpose; it must show an endscreen
            check(results, f"{p.name}: endscreen shown", bool(ends), f"{len(ends)} endscreen second(s)")
            continue
        check(results, f"{p.name}: mission didn't end early", not ends,
              f"{len(ends)} second(s) of endscreen" if ends else "no endscreen")

    # 5a3. nobody died at spawn: a seat that reads dead in the first five
    # seconds of gameplay fell through the world or spawned in the wrong place
    # (seen: the counter-op dropping from y=-624 to the kill plane on defection).
    for p in procs.values():
        play = sorted(t for t, _, f in p.diag.get("team", []) if f.get("tickmode") == "1" and f.get("cutscene") == "0"
                      and (expect_stage is None or f.get("stage") == expect_stage))
        if not play:
            check(results, f"{p.name}: nobody died at spawn", False, "no gameplay", skip=True)
            continue
        early = set(play[:5])
        sb = p.seats_by_tick()
        died = sorted({i for t in early for i, f in sb.get(t, {}).items() if f.get("dead") not in (None, "0")})
        check(results, f"{p.name}: nobody died at spawn", not died,
              ("seat(s) " + ", ".join(str(i) for i in died) + " dead in the first 5 s") if died else "all seats alive")

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
            # gameplay only: in the intro the seats are hidden and parked at
            # their un-grounded spawn pos on the client (a constant 50-70
            # units of y), which is not a sync question
            cplay = {t for t, _, f in c.diag.get("team", []) if f.get("tickmode") == "1" and f.get("cutscene") == "0"
                     and (expect_stage is None or f.get("stage") == expect_stage)}
            dists, dhp, pairs = {}, {}, 0
            for ct, seats in cs.items():
                if ct not in cplay:
                    continue
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
                    a = parse_pos(f.get("pos"))
                    if not a:
                        continue
                    # a running pawn covers ~260 units/s and the two sides
                    # sample it once a second with interp lag between them,
                    # so "same tick, same spot" is the wrong question. ask
                    # instead how far the client's sample is from the host's
                    # PATH: the nearest host sample within POS_LAG_TOL ticks.
                    # a lagging-but-faithful path scores near 0; a real
                    # divergence (a stuck pawn, a wrong snap) stays large.
                    # the host samples once a second and a pawn covers ~260
                    # units in that second, so measure against the segments
                    # between consecutive host samples, not the samples.
                    dmin = None
                    prev = None
                    for ht in hticks:
                        if ht < ct - POS_LAG_TOL:
                            continue
                        if ht > ct + POS_LAG_TOL:
                            break
                        hb = parse_pos(hs[ht].get(i, {}).get("pos"))
                        if not hb:
                            continue
                        d = seg_dist(a, prev, hb) if prev else math.dist(a, hb)
                        if dmin is None or d < dmin:
                            dmin = d
                        prev = hb
                    if dmin is not None:
                        dists.setdefault(i, []).append(dmin)
                    ha, hb = fnum(f.get("health")), fnum(hf.get("health"))
                    if ha is not None and hb is not None:
                        dhp.setdefault(i, []).append(abs(ha - hb))
            if not pairs:
                check(results, f"{c.name}: positions agree with host", False, "no paired samples")
                continue
            worst = max((pct(v, 0.9) for v in dists.values()), default=0.0)
            detail = ", ".join(f"seat{i} p90 {pct(v,0.9):.0f} max {max(v):.0f}" for i, v in sorted(dists.items()))
            check(results, f"{c.name}: positions agree with host", worst <= POS_TOL, f"{pairs} paired seconds, path distance; {detail}")
            worsth = max((pct(v, 0.9) for v in dhp.values()), default=0.0)
            check(results, f"{c.name}: health agrees with host", worsth <= HEALTH_TOL,
                  ", ".join(f"seat{i} p90 {pct(v,0.9):.2f}" for i, v in sorted(dhp.items())))

    # 7b. replicated chr anims (the bug where the guards forget how to walk).
    # pair host and client canim samples by tick and syncid while both play
    # the same anim: the client must loop where the host loops, and must not
    # park a cycle on its last frame. the client's own correction stats
    # (canimfix) say whether it had to fight to stay in step.
    if host:
        hanim = {}
        for t, _, f in host.diag.get("canim", []):
            hanim.setdefault(t, {})[f.get("sid")] = f
        hat = sorted(hanim)
        for c in clients:
            pairs = miss = frozen = 0
            # frozen: a client copy whose frame hasn't moved for three straight
            # seconds while the host's copy of the same anim kept moving. the
            # pre-92 client sat mid-anim (held at ACT_STAND, it only advanced
            # while on its own screen) and never looped or restarted a cycle.
            stuck = {}
            for t, _, f in c.diag.get("canim", []):
                best = None
                for ht in hat:
                    if abs(ht - t) <= TICK_PAIR_TOL and (best is None or abs(ht - t) < abs(best - t)):
                        best = ht
                sid = f.get("sid")
                if best is None:
                    continue
                hf = hanim[best].get(sid)
                if not hf or hf.get("anim") != f.get("anim"):
                    stuck.pop(sid, None)
                    continue
                pairs += 1
                if hf.get("loop") != f.get("loop"):
                    miss += 1
                key = (f.get("anim"), f.get("frame"))
                run = stuck.get(sid)
                if run and run["key"] == key:
                    run["secs"] += 1
                    run["hframes"].add(hf.get("frame"))
                else:
                    run = stuck[sid] = {"key": key, "secs": 1, "hframes": {hf.get("frame")}}
                if run["secs"] >= 3 and len(run["hframes"]) > 1:
                    frozen += 1
            sk = pairs == 0
            check(results, f"{c.name}: chr anims loop like the host's", not sk and miss <= ANIM_LOOP_MISS * pairs,
                  f"{miss} of {pairs} paired chr-anim samples disagree on looping", skip=sk)
            check(results, f"{c.name}: chr anim cycles not frozen", not sk and frozen <= ANIM_FROZEN_MAX * pairs,
                  f"{frozen} of {pairs} paired samples frozen 3+ s on one frame while the host's copy moves", skip=sk)
            fx = c.diag.get("canimfix", [])
            samples = sum(int(fnum(f.get("samples"), 0)) for _, _, f in fx)
            snaps = sum(int(fnum(f.get("snaps"), 0)) for _, _, f in fx)
            restarts = sum(int(fnum(f.get("restarts"), 0)) for _, _, f in fx)
            errw = sum(fnum(f.get("meanerr"), 0) * fnum(f.get("samples"), 0) for _, _, f in fx)
            on = any(f.get("on") == "1" for _, _, f in fx)
            sk = not on or samples == 0
            meanerr = errw / samples if samples else 0.0
            check(results, f"{c.name}: chr anim correction steady",
                  not sk and snaps <= ANIM_SNAP_MAX * samples and meanerr <= ANIM_ERR_MAX,
                  f"{samples} judged frames, {snaps} drift re-seeds, {restarts} host restarts followed, mean error {meanerr:.2f} frames" if not sk
                  else ("correction off" if fx and not on else "no canimfix samples"), skip=sk)

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
