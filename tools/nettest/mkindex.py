#!/usr/bin/env python3
"""Write index.js: everything dashboard.html knows about the runs.

Same trick as buildq's mkindex: a page opened over file:// cannot list a
directory or fetch() a sibling, but it can load a sibling <script>, so the
index is JavaScript and the page re-injects it to notice new runs.
"""
import json, os, sys, time

RUNS = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "runs"))
TAIL_LINES, TAIL_BYTES = 120, 40_000


def tail(path):
    try:
        size = os.path.getsize(path)
        with open(path, "rb") as f:
            f.seek(max(0, size - TAIL_BYTES))
            text = f.read().decode("utf-8", "replace")
        if size > TAIL_BYTES:
            text = text.split("\n", 1)[-1]
        lines = text.splitlines()
        return "\n".join(lines[-TAIL_LINES:])
    except OSError:
        return ""


def one(d):
    p = os.path.join(RUNS, d)
    meta, verdict = {}, {}
    for name, dst in (("run.json", meta), ("verdict.json", verdict)):
        try:
            dst.update(json.load(open(os.path.join(p, name))))
        except Exception:
            pass
    procs = []
    for name in sorted(os.listdir(p)):
        pd = os.path.join(p, name)
        if not os.path.isdir(pd):
            continue
        exitcode = None
        try:
            exitcode = open(os.path.join(pd, "EXIT")).read().split()[-1]
        except Exception:
            pass
        procs.append({
            "name": name,
            "exit": exitcode,
            "log": f"runs/{d}/{name}/out.txt",
            "diag": f"runs/{d}/{name}/diag.csv",
            # the lines a reader wants first: net notes, warnings, stalls
            "tail": "\n".join(l for l in tail(os.path.join(pd, "out.txt")).splitlines()
                              if ("NET:" in l or "WARNING" in l or "FATAL" in l or "watchdog" in l))[-6000:],
        })
    state = "pass" if os.path.exists(f"{p}/PASS") else "fail" if os.path.exists(f"{p}/FAIL") \
        else "running" if not meta.get("finished") else "fail"
    return {
        "dir": d,
        "verdict": state,
        "scenario": meta.get("scenario", d),
        "sha": meta.get("sha"),
        "started": meta.get("started"),
        "finished": meta.get("finished"),
        "secs": meta.get("secs"),
        "clients": meta.get("clients"),
        "roles": meta.get("roles"),
        "stagenum": meta.get("stagenum"),
        "xvfb": meta.get("xvfb"),
        "counts": {k: verdict.get(k) for k in ("pass", "fail", "skip")},
        "checks": verdict.get("checks", []),
        "procs": procs,
    }


def main():
    runs = []
    if os.path.isdir(RUNS):
        for d in sorted(os.listdir(RUNS), reverse=True):
            if os.path.isdir(os.path.join(RUNS, d)) and not d.startswith("."):
                runs.append(one(d))
    out = {"written": int(time.time()), "runs": runs[:60]}
    with open(os.path.join(os.path.dirname(RUNS), "index.js"), "w") as f:
        f.write("window.NETTEST = " + json.dumps(out) + ";\n")
    print(f"{len(runs)} run(s) -> index.js")


if __name__ == "__main__":
    main()
