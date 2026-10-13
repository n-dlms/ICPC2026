#!/usr/bin/env python3
"""tools/metamorphic.py — metamorphic invariants for ICPC 2026 Problem A.

Metamorphic testing catches state-machine bugs that fixed correctness tests
miss: a transformation of the INPUT should produce a predictable transformation
of the OUTPUT (without re-implementing the solver).

Four invariants (per process-failure audit):
  1. Time-shift invariance : shifting all arrivals by +Δ shifts all completions
                             by +Δ -> score identical (verdict ok).
  2. Cloud symmetry         : clouds are identical -> same input run twice must
                             yield identical verdict+score (run-to-run
                             determinism is the observable proxy for label
                             symmetry; any label-dependent / nondeterministic
                             cloud handling surfaces as score drift).
  3. Request permutation    : shuffling request IDs (same (t,lin,lout) multiset)
                             must keep the schedule FEASIBLE (verdict ok); score
                             drift beyond tolerance flags an id-dependent policy.
  4. Batch splitting        : decode work clustered into one burst vs spread
                             across time must both stay FEASIBLE (no batching
                             constraint violation: mixed clouds, dup ids,
                             finished request).

Usage:
  python3 tools/metamorphic.py --solver build/frozen_v5 [--cases N] [--seed S]
"""

import argparse
import math
import os
import random
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

REL_TOL = 1e-6
ABS_TOL = 1e-2


@dataclass
class Case:
    K: int = 8
    S: float = 1.0
    latency: float = 1.0
    bw: float = 1.0
    bpt: int = 1
    layers: int = 10
    SLO1: float = 500.0
    SLO2: float = 200.0
    tp_UB: float = 100.0
    tp_base: float = 2.0
    dist_base: float = 1.0
    w_tp: float = 0.3
    w_c: float = 0.7
    table: list = field(default_factory=list)   # rows: [batch, pp, pproc, ppost, dp, dproc, dpst]
    arrivals: list = field(default_factory=list)  # (t, lin, lout)

    def tokens(self):
        out = []
        out += [str(self.K), f"{self.S}", f"{self.latency}", f"{self.bw}",
                str(self.bpt), str(self.layers)]
        out += [f"{self.SLO1}", f"{self.SLO2}", f"{self.tp_UB}", f"{self.tp_base}",
                f"{self.dist_base}", f"{self.w_tp}", f"{self.w_c}"]
        out.append(str(len(self.table)))
        for r in self.table:
            out += [f"{v}" for v in r]
        out.append(str(len(self.arrivals)))
        for (t, lin, lout) in self.arrivals:
            out += [f"{t}", str(lin), str(lout)]
        return out

    def write(self, path):
        with open(path, "w") as f:
            f.write(" ".join(self.tokens()) + "\n")


def run_case(case, solver, timeout=30.0):
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as tf:
        case.write(tf.name)
        path = tf.name
    try:
        proc = subprocess.run(
            [sys.executable, str(HERE / "interactor.py"), "--case", path,
             "--solver", solver, "--timeout", str(timeout)],
            capture_output=True, text=True, timeout=timeout + 30)
        out = proc.stdout + proc.stderr
    finally:
        os.unlink(path)
    verdict = None
    score = None
    runtime = None
    for line in out.splitlines():
        if line.startswith("VERDICT"):
            seg = line.split("|")
            verdict = seg[0].replace("VERDICT", "").strip()
            for s in seg[1:]:
                s = s.strip()
                if s.startswith("score"):
                    try:
                        score = float(s.split()[1])
                    except Exception:
                        pass
                elif s.startswith("runtime"):
                    try:
                        runtime = float(s.split()[1].rstrip("s"))
                    except Exception:
                        pass
            break
    return verdict, score, runtime


# ---------------------------------------------------------------------------
# Base case generation
# ---------------------------------------------------------------------------

def gen_table(rng, n, kind="sublinear", gaps=False):
    rows = []
    for b in range(1, n + 1):
        if kind == "sublinear":
            dp = 6.0 + 2.0 * math.log2(b)
            dproc = 4.0 + 1.0 * math.log2(b)
        elif kind == "linear":
            dp = 6.0 + 0.5 * b
            dproc = 4.0 + 0.5 * b
        elif kind == "superlinear":
            dp = 1.0 + 0.12 * b * b
            dproc = 1.0 + 0.10 * b * b
        else:  # flat
            dp = 8.0
            dproc = 5.0
        pp = 2.0
        pproc = 8.0
        ppost = 1.5
        dpst = 2.0
        rows.append([b, pp, pproc, ppost, dp, dproc, dpst])
    if gaps:
        for i in range(1, n - 1):
            for col in (1, 3, 4, 6):  # sprinkle -1 (anchors row 1 & n stay valid)
                if rng.random() < 0.4:
                    rows[i][col] = -1.0
    return rows


def gen_base(rng, scenario):
    n = 12
    if scenario == "transfer_heavy":
        K, S, lat, bw, bpt, layers = 4, 1.0, 500.0, 0.02, 500, 10
        w_tp, w_c = 0.5, 0.5
    elif scenario == "compute_heavy":
        K, S, lat, bw, bpt, layers = 8, 1.0, 0.1, 100.0, 1, 10
        w_tp, w_c = 0.5, 0.5
    elif scenario == "big_S":
        K, S, lat, bw, bpt, layers = 4, 200.0, 1.0, 1.0, 1, 10
        w_tp, w_c = 0.3, 0.7
    elif scenario == "zero_S":
        K, S, lat, bw, bpt, layers = 4, 0.0, 1.0, 1.0, 1, 10
        w_tp, w_c = 0.3, 0.7
    elif scenario == "tp_dominant":
        K, S, lat, bw, bpt, layers = 8, 1.0, 1.0, 1.0, 1, 10
        w_tp, w_c = 1.0, 0.0
    elif scenario == "c_dominant":
        K, S, lat, bw, bpt, layers = 8, 1.0, 1.0, 1.0, 1, 10
        w_tp, w_c = 0.0, 1.0
    else:  # balanced
        K, S, lat, bw, bpt, layers = 8, 1.0, 1.0, 1.0, 1, 10
        w_tp, w_c = 0.3, 0.7
    table = gen_table(rng, n, kind="sublinear",
                      gaps=(scenario == "gaps"))
    R = 300
    arrivals = []
    pattern = rng.choice(["burst", "dribble", "mixed"])
    for i in range(R):
        lin = rng.randint(1, 10)
        lout = rng.randint(1, 300)
        if pattern == "burst":
            t = 0.0
        elif pattern == "dribble":
            t = i * 5.0
        else:
            t = (0.0 if i < R // 3 else
                 500.0 if i < 2 * (R // 3) else 1200.0)
        arrivals.append((float(t), lin, lout))
    return Case(K=K, S=S, latency=lat, bw=bw, bpt=bpt, layers=layers,
                w_tp=w_tp, w_c=w_c, table=table, arrivals=arrivals)


# ---------------------------------------------------------------------------
# Transformations
# ---------------------------------------------------------------------------

def time_shift(case, delta):
    c = Case(**{**vars(case)})
    c.arrivals = [(t + delta, lin, lout) for (t, lin, lout) in case.arrivals]
    return c


def permute_requests(case, rng):
    c = Case(**{**vars(case)})
    arr = list(case.arrivals)
    rng.shuffle(arr)
    c.arrivals = arr
    return c


def cluster_decode(case, spread):
    """Return a variant whose arrivals are spread by `spread` (controls how
    much decode work is concurrent). Used for batch-splitting feasibility."""
    c = Case(**{**vars(case)})
    arr = []
    for i, (t, lin, lout) in enumerate(case.arrivals):
        arr.append((float(t) + i * spread, lin, lout))
    c.arrivals = arr
    return c


# ---------------------------------------------------------------------------
# Invariant checks
# ---------------------------------------------------------------------------

def chk(cond, msg, fails, warns):
    if cond:
        print(f"    PASS: {msg}")
    else:
        print(f"    FAIL: {msg}")
        fails.append(msg)


def invariant_timeshift(case, solver, rng, fails, warns):
    delta = 1000.0
    v0, s0, _ = run_case(case, solver)
    c1 = time_shift(case, delta)
    v1, s1, _ = run_case(c1, solver)
    chk(v0 == "ok" and v1 == "ok",
        f"time-shift: both feasible (v0={v0}, v1={v1})", fails, warns)
    if v0 == "ok" and v1 == "ok" and s0 is not None and s1 is not None:
        same = (abs(s0 - s1) <= ABS_TOL + REL_TOL * max(abs(s0), abs(s1)))
        chk(same, f"time-shift: score preserved ({s0:.4f} -> {s1:.4f})", fails, warns)
        if not same:
            warns.append(f"time-shift score drift {s0:.4f}->{s1:.4f}")


def invariant_cloud_symmetry(case, solver, rng, fails, warns):
    v0, s0, _ = run_case(case, solver)
    v1, s1, _ = run_case(case, solver)
    same = (v0 == v1) and (s0 is not None and s1 is not None and
                           abs(s0 - s1) <= ABS_TOL + REL_TOL * max(abs(s0), abs(s1)))
    chk(same, f"cloud-symmetry: deterministic run (v0={v0},s0={s0}; v1={v1},s1={s1})",
        fails, warns)
    if not same:
        warns.append(f"cloud-symmetry drift {s0} vs {s1}")


def invariant_permutation(case, solver, rng, fails, warns):
    v0, s0, _ = run_case(case, solver)
    c1 = permute_requests(case, rng)
    v1, s1, _ = run_case(c1, solver)
    # Hard invariant: feasibility preserved.
    chk(v1 == "ok", f"permutation: feasible after shuffle (v0={v0}, v1={v1})", fails, warns)
    if v0 == "ok" and v1 == "ok" and s0 is not None and s1 is not None:
        drift = abs(s0 - s1)
        if drift > ABS_TOL + REL_TOL * max(abs(s0), abs(s1)):
            warns.append(f"permutation score drift {s0:.4f}->{s1:.4f} "
                         f"(id-dependent policy?)")
            print(f"    WARN: permutation score drift {s0:.4f}->{s1:.4f}")


def invariant_batch_split(case, solver, rng, fails, warns):
    # Clustered (all concurrent decode) vs spread: both must stay feasible.
    v0, s0, _ = run_case(case, solver)
    c1 = cluster_decode(case, spread=20.0)
    v1, s1, _ = run_case(c1, solver)
    chk(v0 == "ok", f"batch-split: clustered feasible (v0={v0})", fails, warns)
    chk(v1 == "ok", f"batch-split: spread feasible (v1={v1})", fails, warns)


INVARIANTS = {
    "time_shift": invariant_timeshift,
    "cloud_symmetry": invariant_cloud_symmetry,
    "permutation": invariant_permutation,
    "batch_split": invariant_batch_split,
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--solver", required=True)
    ap.add_argument("--cases", type=int, default=6)
    ap.add_argument("--seed", type=int, default=12345)
    ap.add_argument("--only", default=None, help="comma-separated invariant names")
    args = ap.parse_args()

    rng = random.Random(args.seed)
    scenarios = ["balanced", "transfer_heavy", "compute_heavy", "big_S",
                 "zero_S", "tp_dominant", "c_dominant", "gaps"]
    only = set(args.only.split(",")) if args.only else None

    total_fail = 0
    total_warn = 0
    print(f"metamorphic test: solver={args.solver} cases={args.cases} seed={args.seed}")
    for ci in range(args.cases):
        scenario = scenarios[ci % len(scenarios)]
        case = gen_base(rng, scenario)
        print(f"\n=== base case {ci} (scenario={scenario}, R={len(case.arrivals)}, "
              f"K={case.K}, S={case.S}, w_tp/w_c={case.w_tp}/{case.w_c}) ===")
        for name, fn in INVARIANTS.items():
            if only and name not in only:
                continue
            print(f"  -- {name}")
            fails, warns = [], []
            fn(case, args.solver, rng, fails, warns)
            total_fail += len(fails)
            total_warn += len(warns)

    print(f"\n==== metamorphic summary: {total_fail} FAIL, {total_warn} WARN ====")
    sys.exit(1 if total_fail else 0)


if __name__ == "__main__":
    main()
