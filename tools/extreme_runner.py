#!/usr/bin/env python3
"""Extreme torture-gate for the ICPC 2026 solver.

Runs the 5 extreme cases and enforces the EXACT expected
results (not just "no crash"). Each case has a precise behavioral/numeric
acceptance criterion documented inline.

Usage:
    python3 tools/extreme_runner.py --solver build/submission
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CASE_DIR = os.path.join(ROOT, "cases", "adv")
TRACE = "/tmp/extreme_trace.txt"


def read_case(path):
    with open(path) as f:
        toks = f.read().split()
    i = 0
    K = int(toks[i]); i += 1
    S = float(toks[i]); i += 1
    lat = float(toks[i]); i += 1
    bw = float(toks[i]); i += 1
    bpt = int(toks[i]); i += 1
    layers = int(toks[i]); i += 1
    slo = [float(toks[i + j]) for j in range(7)]; i += 7
    N = int(toks[i]); i += 1
    table = [[float(toks[i + r * 7 + c]) for c in range(7)] for r in range(N)]; i += 7 * N
    R = int(toks[i]); i += 1
    arrivals = [(float(toks[i + 3 * r]), int(toks[i + 3 * r + 1]), int(toks[i + 3 * r + 2])) for r in range(R)]
    return dict(K=K, S=S, lat=lat, bw=bw, bpt=bpt, layers=layers, slo=slo,
                table=table, arrivals=arrivals)


def run_case(solver, case_path):
    case = read_case(case_path)
    cmd = [sys.executable, os.path.join(HERE, "interactor.py"),
           "--case", case_path, "--solver", solver,
           "--trace-file", TRACE, "--timeout", "60"]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    out = proc.stdout + proc.stderr
    verdict = None
    score = None
    rt = None
    frames = None
    for line in out.splitlines():
        m = re.search(r"VERDICT\s+(\S+)", line)
        if m:
            verdict = m.group(1)
        m = re.search(r"score\s+([0-9.]+)", line)
        if m:
            score = float(m.group(1))
        m = re.search(r"runtime\s+([0-9.]+)s", line)
        if m:
            rt = float(m.group(1))
        m = re.search(r"frames\s+(\d+)", line)
        if m:
            frames = int(m.group(1))
    return case, verdict, score, rt, frames, out


def parse_trace():
    """Return (events, decisions) from TRACE."""
    events = []   # dicts from EVENT lines
    decisions = []  # (t, srv, spec_tokens)
    if not os.path.exists(TRACE):
        return events, decisions
    with open(TRACE) as f:
        for line in f:
            line = line.strip()
            if line.startswith("EVENT t="):
                # EVENT t=.. KEY rest
                m = re.match(r"EVENT t=([0-9.eE+-]+)\s+(\S+)\s+(.*)", line)
                if not m:
                    continue
                t = float(m.group(1))
                key = m.group(2)
                rest = m.group(3).split()
                events.append(dict(t=t, kind=key, rest=rest))
            elif line.startswith("DECISION t="):
                m = re.match(r"DECISION t=([0-9.eE+-]+)\s+ASSIGN\s+(\S+)\s+(.*)", line)
                if not m:
                    continue
                t = float(m.group(1))
                srv = m.group(2)
                spec = m.group(3).split()
                decisions.append((t, srv, spec))
    return events, decisions


def check_massacre(case, verdict, score, rt, frames):
    msgs = []
    ok = True
    if verdict != "ok":
        ok = False
        msgs.append(f"verdict={verdict} (expected ok)")
    if rt is None or rt >= 3.0:
        ok = False
        msgs.append(f"runtime={rt}s (expected < 3.0s)")
    else:
        msgs.append(f"runtime={rt:.2f}s (< 3.0s OK)")
    return ok, msgs


def check_blackhole(case, verdict, score, rt, frames, events, decisions):
    msgs = []
    ok = True
    if verdict != "ok":
        ok = False
        msgs.append(f"verdict={verdict} (expected ok)")
    # FIFO UP-queue depth = (#UPs enqueued by t) - (#UPs completed by t).
    # Enqueue happens when the triggering task completes (TDN E P PRE / D PRE);
    # each P PRE enqueues 1 UP, each D PRE enqueues 1 UP per distinct cloud.
    id_cloud = {}
    for t, srv, spec in decisions:
        if len(spec) >= 4 and spec[0] == "P" and spec[1] == "PRE":
            cloud = int(spec[2]); rid = int(spec[3])
            id_cloud[rid] = cloud
    # Build timeline: (time, delta). Enqueues from TDN events of local tasks.
    timeline = []
    for ev in events:
        if ev["kind"] != "TDN":
            continue
        rest = ev["rest"]
        # rest = [srv, "P"/"D", "PRE"/"POST", ...]
        if len(rest) < 3:
            continue
        if rest[0] != "E":
            continue
        step = rest[1] + " " + rest[2]
        if step == "P PRE":
            timeline.append((ev["t"], 1))  # 1 UP
        elif step == "D PRE":
            m = int(rest[4])
            ids = [int(x) for x in rest[5:5 + m]]
            clouds = set(id_cloud.get(rid, -1) for rid in ids)
            timeline.append((ev["t"], len(clouds)))
    for ev in events:
        if ev["kind"] == "XDN" and ev["rest"][0] == "UP":
            timeline.append((ev["t"], -1))
    timeline.sort(key=lambda x: x[0])
    cur = 0
    mx = 0
    for _, d in timeline:
        cur += d
        mx = max(mx, cur)
    if mx > 2:
        ok = False
        msgs.append(f"max in-flight UP (FIFO depth) = {mx} (expected <= 2)")
    else:
        msgs.append(f"max in-flight UP (FIFO depth) = {mx} (<= 2 OK)")
    msgs.append(f"total UP transfers = {sum(1 for ev in events if ev['kind']=='XDN' and ev['rest'][0]=='UP')}")
    return ok, msgs


def check_straggler(case, verdict, score, rt, frames, events, decisions):
    msgs = []
    ok = True
    if verdict != "ok":
        ok = False
        msgs.append(f"verdict={verdict} (expected ok)")
    # arrivals: id 400 is the straggler (Lout=512).
    STRAG = 400
    arr_times = [ev["t"] for ev in events if ev["kind"] == "ARR"]
    fin_times = {int(ev["rest"][0]): ev["t"] for ev in events if ev["kind"] == "FIN"}
    min_arr = min(arr_times) if arr_times else 0.0
    max_fin = max(fin_times.values()) if fin_times else 0.0
    makespan = max_fin - min_arr
    long_fin = fin_times.get(STRAG)
    # Build id -> cloud from P PRE decisions.
    id_cloud = {}
    for t, srv, spec in decisions:
        if len(spec) >= 4 and spec[0] == "P" and spec[1] == "PRE":
            cloud = int(spec[2])
            rid = int(spec[3])
            id_cloud[rid] = cloud
    long_cloud = id_cloud.get(STRAG)
    # Count OTHER ids sharing the long request's decode cloud (D PROC groups).
    other_on_cloud = 0
    if long_cloud is not None:
        for t, srv, spec in decisions:
            if len(spec) >= 4 and spec[0] == "D" and spec[1] == "PROC":
                cloud = int(spec[2])
                if cloud == long_cloud:
                    for tok in spec[4:]:
                        rid = int(tok)
                        if rid != STRAG:
                            other_on_cloud += 1
    msgs.append(f"makespan={makespan:.1f}s long_fin={long_fin:.1f}s "
                f"long_cloud=C{long_cloud} other_decode_ids_on_cloud={other_on_cloud}")
    # The long request must be the makespan determinant (not blocked).
    if long_fin is None:
        ok = False
        msgs.append("straggler FIN not found")
    elif (max_fin - long_fin) > 1.0:
        ok = False
        msgs.append("straggler is NOT the last finisher (blocked by shorts)")
    else:
        msgs.append("straggler is the makespan determinant (OK)")
    # Isolation: prefer the straggler to be nearly alone on its cloud.
    if other_on_cloud > 20:
        ok = False
        msgs.append(f"straggler shares decode cloud with {other_on_cloud} others (not isolated)")
    else:
        msgs.append(f"straggler decode-cloud occupancy OK ({other_on_cloud} others)")
    return ok, msgs


def check_void(case, verdict, score, rt, frames):
    msgs = []
    ok = True
    if verdict != "ok":
        ok = False
        msgs.append(f"verdict={verdict} (expected ok)")
    if score is None or score < 900.0:
        ok = False
        msgs.append(f"score={score} (expected >= 900 / max)")
    else:
        msgs.append(f"score={score:.3f} (>= 900 OK)")
    return ok, msgs


def check_falsehope(case, verdict, score, rt, frames, events, decisions):
    msgs = []
    ok = True
    if verdict != "ok":
        ok = False
        msgs.append(f"verdict={verdict} (expected ok)")
    # id -> cloud
    id_cloud = {}
    for t, srv, spec in decisions:
        if len(spec) >= 4 and spec[0] == "P" and spec[1] == "PRE":
            cloud = int(spec[2])
            rid = int(spec[3])
            id_cloud[rid] = cloud
    # Detect cross-cloud D PRE combine.
    cross = 0
    for t, srv, spec in decisions:
        if len(spec) >= 4 and spec[0] == "D" and spec[1] == "PRE":
            ids = [int(x) for x in spec[4:]]
            clouds = set(id_cloud.get(rid, -1) for rid in ids)
            if len(clouds) > 1:
                cross += 1
    msgs.append(f"cross-cloud D PRE groups = {cross}")
    if cross == 0:
        ok = False
        msgs.append("NO cross-cloud D PRE combine -> solver is in WAITING "
                    "(holds D PRE). Expected THROUGHPUT regime (dist_base=0 cliff).")
    else:
        msgs.append("cross-cloud D PRE combine present -> THROUGHPUT regime (OK)")
    msgs.append(f"score={score} (dist_base=0 + min_TDR>SLO1 -> structural 0; "
                f"behavioral flip is the real check)")
    return ok, msgs


EXTREMES = {
    "massacre": "massacre",
    "blackhole": "blackhole",
    "straggler": "straggler",
    "void": "void",
    "falsehope": "falsehope",
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--solver", required=True)
    ap.add_argument("--only", default=None)
    args = ap.parse_args()
    names = [args.only] if args.only else ["massacre", "blackhole", "straggler", "void", "falsehope"]
    total = 0
    passed = 0
    for name in names:
        case_path = os.path.join(CASE_DIR, f"adv_{name}.txt")
        if not os.path.exists(case_path):
            print(f"[SKIP] {name}: case not found ({case_path})")
            continue
        total += 1
        case, verdict, score, rt, frames, out = run_case(args.solver, case_path)
        events, decisions = parse_trace()
        print(f"\n=== {name} ===")
        print(f"verdict={verdict} score={score} runtime={rt} frames={frames}")
        fn = EXTREMES[name][0]
        # call with correct signature
        if name == "massacre":
            ok, msgs = check_massacre(case, verdict, score, rt, frames)
        elif name == "blackhole":
            ok, msgs = check_blackhole(case, verdict, score, rt, frames, events, decisions)
        elif name == "straggler":
            ok, msgs = check_straggler(case, verdict, score, rt, frames, events, decisions)
        elif name == "void":
            ok, msgs = check_void(case, verdict, score, rt, frames)
        elif name == "falsehope":
            ok, msgs = check_falsehope(case, verdict, score, rt, frames, events, decisions)
        else:
            ok, msgs = False, ["unknown"]
        for m in msgs:
            print(f"  - {m}")
        status = "PASS" if ok else "FAIL"
        print(f"  >>> {status}")
        if ok:
            passed += 1
    print(f"\nEXTREME GATE: {passed}/{total} passed")
    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    main()
