#!/usr/bin/env python3
"""tools/runtime_stress.py — runtime-adversary hill-climb for ICPC 2026 Prob A.

The solver must not TLE / crash / WA under extreme parameter ratios. This tool
hill-climbs each axis toward the setting that MAXIMIZES solver runtime (the
stress metric), and flags any failure (verdict != ok, or per-run timeout).

Four axes (per process-failure audit):
  1. S vs task-duration ratio : S from ~0 (chunking free) up to huge
                                 (setup cost dominates -> many task slices).
  2. Transfer vs compute ratio: latency/bandwidth/bpt from compute-bound to
                                 transfer-saturated (UP/DOWN FIFO pressure).
  3. Arrival burstiness       : all-at-t0 burst  ->  fully spread dribble.
  4. w_tp vs w_c extremes      : (w_tp=1,w_c=0) pure-throughput vs
                                 (w_tp=0,w_c=1) pure-latency.

Hill-climb: from a middle case, at each step try +delta and -delta along the
axis; keep the move that yields the HIGHER solver runtime (greedy ascent).
Stop after `steps` steps or on the first hard failure (which is exactly what
we are hunting for).

Usage:
  python3 tools/runtime_stress.py --solver build/frozen_v5 [--steps 8] [--seed 1]
"""

import argparse
import math
import random
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from metamorphic import Case, gen_table, run_case  # reuse harness plumbing


def base_case(rng, R=1500, K=8, layers=10):
    table = gen_table(rng, 12, kind="sublinear")
    arrivals = []
    for i in range(R):
        lin = rng.randint(1, 10)
        lout = rng.randint(1, 400)
        t = 0.0  # caller overrides burstiness
        arrivals.append((float(t), lin, lout))
    return Case(K=K, S=1.0, latency=1.0, bw=1.0, bpt=1, layers=layers,
                SLO1=500.0, SLO2=200.0, tp_UB=100.0, tp_base=2.0,
                dist_base=1.0, w_tp=0.5, w_c=0.5,
                table=table, arrivals=arrivals)


# ---------------------------------------------------------------------------
# Axis perturbations. Each returns a NEW Case with one parameter nudged.
# `sign` is +1 / -1 (direction along the axis).
# ---------------------------------------------------------------------------

def perturb_s(case, sign, scale):
    c = Case(**{**vars(case)})
    # geometric step on S
    if sign > 0:
        c.S = min(c.S * 3.0, 1e6)
    else:
        c.S = max(c.S / 3.0, 0.0)
    return c


def perturb_transfer(case, sign, scale):
    c = Case(**{**vars(case)})
    if sign > 0:
        # toward transfer-saturated: bigger latency, smaller bw, bigger bpt
        c.latency = min(c.latency * 3.0, 1e5)
        c.bw = max(c.bw / 3.0, 1e-4)
        c.bpt = min(int(c.bpt * 2 + 1), 100000)
    else:
        c.latency = max(c.latency / 3.0, 1e-3)
        c.bw = min(c.bw * 3.0, 1e4)
        c.bpt = max(int(c.bpt / 2), 1)
    return c


def perturb_burst(case, sign, scale):
    c = Case(**{**vars(case)})
    # increase spread between arrivals (sign>0 = more dribble)
    step = 2.0 * (3.0 ** scale)
    new = []
    for i, (t, lin, lout) in enumerate(case.arrivals):
        if sign > 0:
            new.append((float(i) * step, lin, lout))
        else:
            new.append((0.0, lin, lout))  # all burst
    c.arrivals = new
    return c


def perturb_wtp(case, sign, scale):
    c = Case(**{**vars(case)})
    # sweep w_tp from 0..1 ; sign>0 pushes toward w_tp=1 (throughput)
    c.w_tp = max(0.0, min(1.0, c.w_tp + sign * 0.25))
    c.w_c = 1.0 - c.w_tp
    return c


AXES = {
    "S_ratio": perturb_s,
    "transfer_ratio": perturb_transfer,
    "burstiness": perturb_burst,
    "wtp_wc": perturb_wtp,
}


def climb(axis_name, solver, rng, steps, timeout, slow_budget):
    fn = AXES[axis_name]
    case = base_case(rng)
    v, s, rt = run_case(case, solver, timeout=timeout)
    rt = rt if rt is not None else 0.0
    worst = {"axis": axis_name, "verdict": v, "runtime": rt,
             "params": summarize(case)}
    failures = []
    scale = 0
    for step in range(steps):
        scale += 1
        c_up = fn(case, +1, scale)
        c_dn = fn(case, -1, scale)
        v_up, s_up, rt_up = run_case(c_up, solver, timeout=timeout)
        v_dn, s_dn, rt_dn = run_case(c_dn, solver, timeout=timeout)
        rt_up = rt_up if rt_up is not None else 0.0
        rt_dn = rt_dn if rt_dn is not None else 0.0
        # pick the move with higher runtime (greedy ascent on stress)
        if rt_up >= rt_dn:
            case, v, s, rt = c_up, v_up, s_up, rt_up
        else:
            case, v, s_dn, rt = c_dn, v_dn, s_dn, rt_dn
        rt = rt if rt is not None else 0.0
        if rt > worst["runtime"]:
            worst["runtime"] = rt
            worst["verdict"] = v
            worst["params"] = summarize(case)
        # Real break: verdict is not "ok" (tle/re/violation/stuck/None).
        if v not in ("ok",):
            failures.append({"axis": axis_name, "step": step, "verdict": v,
                             "runtime": rt, "params": summarize(case)})
            print(f"    BREAK on {axis_name} step {step}: verdict={v} "
                  f"runtime={rt:.2f} params={worst['params']}")
            break  # found the breaking point; nothing worse to climb toward
        elif rt > slow_budget:
            print(f"    SLOW on {axis_name} step {step}: runtime={rt:.2f}s "
                  f"(> {slow_budget}s budget) params={worst['params']}")
    return worst, failures


def summarize(case):
    return (f"K={case.K} S={case.S:.3g} lat={case.latency:.3g} bw={case.bw:.3g} "
            f"bpt={case.bpt} wtp/wc={case.w_tp:.2f}/{case.w_c:.2f} "
            f"R={len(case.arrivals)} t0={case.arrivals[0][0]:.2f}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--solver", required=True)
    ap.add_argument("--steps", type=int, default=8)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--timeout", type=float, default=30.0)
    ap.add_argument("--slow", type=float, default=10.0,
                    help="runtime budget; above this (with verdict ok) is a SLOW warning")
    ap.add_argument("--only", default=None)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    axes = [args.only] if args.only else list(AXES.keys())
    print(f"runtime_stress: solver={args.solver} steps={args.steps} "
          f"seed={args.seed} timeout={args.timeout}s slow_budget={args.slow}s")
    any_fail = False
    for axis in axes:
        print(f"\n=== hill-climb axis: {axis} ===")
        worst, failures = climb(axis, args.solver, rng, args.steps, args.timeout, args.slow)
        any_fail = any_fail or bool(failures)
        print(f"    worst (max runtime): verdict={worst['verdict']} "
              f"runtime={worst['runtime']:.2f}s  {worst['params']}")
        if not failures:
            print(f"    OK: no failure/timeout along {axis}")

    print(f"\n==== runtime_stress summary: "
          f"{'FAILURES FOUND' if any_fail else 'all axes survived'} ====")
    sys.exit(1 if any_fail else 0)


if __name__ == "__main__":
    main()
