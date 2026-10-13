#!/usr/bin/env python3
"""tools/scaling_bench.py — performance scaling gate (new protocol).

Runs the solver against synthetic cases of increasing request count
(--sizes) through the interactor. GATE: every size must reach VERDICT ok
(no violation/stuck/tle/re) and wall time must stay under the per-case
budget. Reports wall times for eyeballing the growth curve.

Usage:
  python3 tools/scaling_bench.py --solver build/frozen_v1 --sizes 10,50,100,500,1000
"""

import argparse
import random
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent


def gen_case(rng, r):
    """Dribble-style scale case: K=8, dense table, lin/lout near statement max."""
    K, S, lat, bw, bpt, layers = 8, 1.0, 1.0, 1.0, 1000, 16
    N = 8
    batches = [1, 2, 4, 8, 16, 32, 64, 128]
    rows = [[b, 2.0, 12.0, 2.0, 1.5, 8.0 + 0.5 * (b.bit_length() - 1), 1.5]
            for b in batches]
    arrs = []
    for i in range(r):
        lin = rng.randint(1, 256)
        lout = rng.randint(1, 64)
        arrs.append((round(i * 2.0, 3), lin, lout))
    lines = [f"{K} {S} {lat} {bw} {bpt} {layers}",
             "500 100 1000 0 0.1 0.5 0.5", str(N)]
    for row in rows:
        lines.append(" ".join(str(v) for v in row))
    lines.append(str(len(arrs)))
    for t, lin, lout in arrs:
        lines.append(f"{t} {lin} {lout}")
    return "\n".join(lines) + "\n"


def run_one(solver, case_text, name, timeout, tmpdir):
    path = tmpdir / f"{name}.txt"
    path.write_text(case_text)
    t0 = time.monotonic()
    p = subprocess.run(
        [sys.executable, str(HERE / "interactor.py"),
         "--case", str(path), "--solver", str(solver), "--timeout", str(timeout)],
        capture_output=True, text=True)
    wall = time.monotonic() - t0
    last = ((p.stdout or "") + (p.stderr or "")).strip().splitlines()
    verdict = last[-1] if last else "(no output)"
    return name, verdict, wall


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--solver", required=True)
    ap.add_argument("--sizes", default="10,50,100,500,1000")
    ap.add_argument("--seed", type=int, default=20260814)
    ap.add_argument("--timeout", type=float, default=60.0)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    sizes = [int(s) for s in args.sizes.split(",")]
    fails = []
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        for i, r in enumerate(sizes):
            name, verdict, wall = run_one(
                args.solver, gen_case(rng, r), f"scale_{r}", args.timeout, tmp)
            ok = verdict.startswith("VERDICT ok")
            if not ok:
                fails.append((name, verdict))
            print(f"  {name:>12}: {verdict}  [{wall:6.2f}s]")
    print(f"\nscaling_bench: {len(sizes) - len(fails)}/{len(sizes)} ok")
    for name, v in fails:
        print(f"  FAIL {name}: {v}")
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())