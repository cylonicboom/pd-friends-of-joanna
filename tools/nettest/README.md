# nettest

A host and N clients, booted from the CLI, driven by Lua pads, judged from
their own logs. No daemon, no server; `runs/` is scratch and gitignored, the
dashboard is one HTML file.

```sh
tools/nettest/nettest run two-seats-walk --basedir <data dir with the rom + mods>
tools/nettest/nettest run three-seats-walk --secs 150
tools/nettest/nettest verdict tools/nettest/runs/<run>     # re-judge a run
tools/nettest/nettest index                                # rewrite index.js
open tools/nettest/dashboard.html
```

`nettest run` finds the binary under `build/` (or `--build DIR`, or
`NETTEST_BUILD`). On a Linux box with `xvfb-run` every process gets its own
display at 320x240 and llvmpipe (`--no-xvfb` to use real windows; on a Mac
that is the default). The host arms a Team Mission with `--team <stageindex>
--team-roles b,c,a,n` and starts it once `--team-wait N` clients sit in the
lobby; the clients `--connect` and follow the roles manifest in
`SVC_STAGE_START`.

## What a run proves

Every process logs `team` / `seat` / `netcl` diag lines once a second
(`port/src/net/netteam.c`, into `--netdiag diag.csv`). `netverdict.py` pairs
host and client samples by net tick and writes one PASS / FAIL / SKIP per
check into `verdict.json`, plus a zero-byte `PASS` or `FAIL` beside it:

| check | passes when |
|---|---|
| booted | `server_start` / `client_start` is in the diag |
| clients in game | every client reached CLSTATE_GAME on the host's table |
| team stage loaded | a `team` line at the armed stage with `isteam=1` |
| roles match host · own seat role | the seat manifest is identical everywhere, and each client's local seat is a non-operative seat with the armed role |
| gameplay reached | `tickmode=1 cutscene=0` was logged at the stage (the intro was skipped) |
| own pawn moved | a walker-driven seat covered ≥ 40 units in xz during gameplay. a pawn whose pad routes to a controller that doesn't exist agrees with the host perfectly and proves nothing |
| positions agree | per seat, host-vs-client distance p90 ≤ 200 units over paired seconds (~0.75 s at run speed) |
| health agrees | per seat, `bondhealth` p90 within 0.1 |
| no crash or stall | no `FATAL`, watchdog stall, anim-walk or prop-walk guard in the log |
| no net warnings | no `WARNING: NET` beyond the allow-list in `netverdict.py` |
| prop audit | kai's auditor never reported a non-PASS cycle (SKIP if it never ran) |
| tick drift bounded | worst reported drift ≤ 180 ticks |

Thresholds are constants at the top of `netverdict.py`.

## Scenarios and drivers

A scenario (`scenarios/<name>.sh`) is a few variables: stage index, roles,
client count, run length, which driver each side runs. A driver
(`scripts/<name>.lua`) is a `pd.on("tick")` handler calling `pd.pad(0, …)`;
it is copied in as each process's `scripts/init.lua` with a `NETTEST` table
(`name`, `host`) prepended, so one file can act differently per role. The host
taps A after boot to skip the intro cutscene; clients get it mirrored.

## Limits, honestly

- Rendering is real: every process draws its frame (xvfb + llvmpipe on Linux).
  A headless tick path is kai's `--dedicated` / `--headless-client` tier and
  is not ported yet.
- The run ends on a wall clock, not on a mission end; "mission complete on
  every machine" is not a check yet.
- Pad drivers are blind — they do not read the world. A driver that aims at a
  chr or walks to a pad needs the `pd.*` query bindings.
