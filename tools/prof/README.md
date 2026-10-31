# tools/prof - the frame gets a stopwatch

`port/src/prof.c` is a zone profiler: per-zone, per-seat time and counts
inside the frame. `PROF_BEGIN(z)` / `PROF_END()` bracket a zone,
`PROF_COUNT(z, n)` adds to a zone's count. Zones are a fixed enum in
`port/include/prof.h`; a per-seat zone files under
`g_Vars.currentplayernum`, the rest under the `frame` column.

## Turning it on

| | |
|---|---|
| `--prof` | live table only (Profiler panel, netdiag `prof` line) |
| `--profdump run.fxt` | live table + every edge streamed to `run.fxt` |
| `Prof.Enabled=1`, `Prof.DumpPath=` | the same from pd.ini |
| `--prof-net` / `Prof.OnNet=1` | a dump per net session, opened when you host or join and closed at disconnect: `<Prof.DumpDir>/<YYYYmmdd-HHMMSS>-<host|client|dedicated>.fxt`, dir defaults to `$S/prof` (the save dir) |
| fojOS Profiler panel, "profile zones" | toggle live |

CLI flags and the panel checkbox never write back to pd.ini; only the
`Prof.*` keys you put there persist.

Off, every hook is one load and one branch. On, an edge is one monotonic
clock read and a few stores; the dump adds a 16-byte store. The dump's own
cost is a zone (`prof.dump`), so it is on the table it produces.

## Reading it

The file is Fuchsia Trace Format (.fxt). Two readers:

- **timeline**: open the file in <https://ui.perfetto.dev>. Seats are
  tracks, zones are slices, counts are counter tracks. Nothing to install.
- **tables**: `tools/prof/profread.py run.fxt` (no dependencies).
  `--diff other.fxt` prints per-zone ratios - one seat vs three, listen
  host vs dedicated, before vs after. `--counters` for the count series,
  `--after N --before M` for a frame window.

`--netdiag` also carries a `prof` line once a second per seat column
(`zone=self_us/incl_us/calls/count`), so nettest runs get it for free.

## Fault tolerance

Records are fixed-size and self-delimiting; the buffer is flushed once a
second and from the crash handler. A truncated file reads up to the last
whole record in both readers (measured: Perfetto imports a file cut
mid-record; profread reports `TRUNCATED` and the frames it has).

## Adding a zone

Add it to `enum profzone` and the name table in `prof.c` (name, per-seat
flag), then bracket the call. Counts: `PROF_COUNT` anywhere inside. Keep
the enum append-only; the dump carries the names so old files still read.
