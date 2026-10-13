#!/usr/bin/env python3
"""tools/crash_sweep.py — random-case sweep (new protocol, interactor backend).

Generates `--generate N` seeded random cases within statement constraints
(plus stress variants: bursts, delayed arrivals with EMPTY frames, precision
edges, max-scale singletons) and runs each through the interactor. FAIL on any
violation/stuck/tle/re/EOF. Deterministic per seed.

Usage:
  python3 tools/crash_sweep.py --solver build/solution --generate 3000 [-j 8]
"""

import argparse
import concurrent.futures as cf
import math
import random
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent


def gen_case(rng, i):
    """Deterministic random case; returns (case_text, name)."""
    K = rng.randint(1, 8)
    S = rng.randint(1, 10)
    latency = round(rng.uniform(0.001, 50.0), 6)
    bw = round(rng.uniform(0.001, 100.0), 6)
    bpt = rng.choice([1, 10, 1000, 100000, 1000000])
    layers = rng.randint(1, 64)

    N = rng.randint(2, 16)
    batches = sorted(rng.sample(range(1, 33), N))
    rows = []
    for b in batches:
        vals = [round(rng.uniform(0.001, 20.0), 6) for _ in range(6)]
        # dproc: mix of sublinear/linear/superlinear to shake batching logic
        kind = rng.random()
        dproc = 1.0 + b * (0.3 + 0.7 * kind) + (b * b * 0.05 if rng.random() < 0.2 else 0.0)
        vals[3] = round(dproc, 6)
        row = [b] + vals
        for c in range(1, 7):
            if rng.random() < 0.15 and c != 3:
                row[c] = -1.0  # gaps
        rows.append(row)
    # every column needs >= 1 non-missing: anchor row 0
    rows[0] = [batches[0]] + [max(0.001, round(rng.uniform(0.001, 20.0), 6)) for _ in range(6)]

    r = rng.randint(1, 60)
    if i % 20 == 0:
        r = rng.randint(100, 300)  # periodic larger stress
    max_lout = max(1, min(512, 200000 // r))
    pattern = rng.random()
    empties = []
    arrs = []
    for j in range(r):
        lin = rng.randint(1, 256)
        lout = rng.randint(1, min(32, max_lout))
        if pattern < 0.3:
            t = 0.0
        elif pattern < 0.6:
            t = round(j * rng.uniform(1.0, 30.0), 6)
        elif pattern < 0.8:
            t = round(rng.choice([0.0, 100.0, 500.0, 2000.0]), 6)
        else:
            t = round(rng.uniform(0.0, 3000.0), 6)
        arrs.append((t, lin, lout))
    arrs.sort()
    if i % 25 == 0:
        empties = [round(rng.uniform(0.0, 10.0), 3) for _ in range(rng.randint(1, 3))]

    # scoring: dead zones, binary dist_base, tp-only, c-only variants
    slo1 = round(rng.uniform(10.0, 3000.0), 3)
    slo2 = round(rng.uniform(1.0, 500.0), 3)
    tp_ub = round(rng.uniform(1.0, 5000.0), 3)
    tp_base = round(rng.uniform(0.0, tp_ub / 2), 3)
    dist_base = rng.choice([0.0, 0.0, 0.05, 0.2, 1.0, 5.0])
    wtp = rng.choice([0.0, 0.3, 0.5, 0.7, 1.0])
    wc = round(1.0 - wtp, 3)

    out = [f"{K} {S} {latency} {bw} {bpt} {layers}",
           f"{slo1} {slo2} {tp_ub} {tp_base} {dist_base} {wtp} {wc}",
           str(N)]
    for row in rows:
        out.append(" ".join(str(v) for v in row))
    out.append(str(len(arrs)))
    for t, lin, lout in arrs:
        out.append(f"{t} {lin} {lout}")
    for e in empties:
        out.append(f"EMPTY {e}")
    return "\n".join(out) + "\n", f"rand_{i:04d}"


def run_one(solver, case_text, name, timeout, tmpdir):
    path = tmpdir / f"{name}.txt"
    path.write_text(case_text)
    p = subprocess.run(
        [sys.executable, str(HERE / "interactor.py"),
         "--case", str(path), "--solver", str(solver), "--timeout", str(timeout)],
        capture_output=True, text=True)
    last = ((p.stdout or "") + (p.stderr or "")).strip().splitlines()
    verdict = last[-1] if last else "(no output)"
    return name, verdict


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--solver", required=True)
    ap.add_argument("--generate", type=int, default=1000)
    ap.add_argument("--seed", type=int, default=20260814)
    ap.add_argument("-j", "--jobs", type=int, default=8)
    ap.add_argument("--timeout", type=float, default=15.0)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        cases = [gen_case(rng, i) for i in range(args.generate)]
        fails = []
        done = 0
        with cf.ThreadPoolExecutor(max_workers=args.jobs) as ex:
            futs = [ex.submit(run_one, args.solver, t, n, args.timeout, tmp)
                    for t, n in cases]
            for fut in cf.as_completed(futs):
                name, verdict = fut.result()
                done += 1
                if not verdict.startswith("VERDICT ok"):
                    fails.append((name, verdict))
                    if len(fails) <= 5:
                        print(f"  FAIL {name}: {verdict}")
                if done % 500 == 0:
                    print(f"  {done}/{args.generate} ...")
        print(f"\ncrash_sweep: {args.generate - len(fails)}/{args.generate} ok")
        if fails:
            for name, v in fails[:20]:
                print(f"  FAIL {name}: {v}")
        return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())