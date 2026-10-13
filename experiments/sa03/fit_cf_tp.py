#!/usr/bin/env python3
"""SA03: fit real CF per-test tp_base / tp_UB from (tp, norm_tp) row pairs
across the 2026-08-16 and 2026-08-17 judgement dumps.

Model (linear region): norm_tp = (tp - b) / (UB - b)  =>  tp = b + n*(UB-b).
Two runs with n1 != n2 solve exactly:
    UB - b = (tp1 - tp2) / (n1 - n2)
    b      = tp1 - n1 * (UB - b)
Clamped runs (n==0 or n==1) give one-sided bounds only.
"""
import re
import sys

DUMPS = {
    "08-16": "/home/dlamini/ICPC2026/docs/CF_JUDGEMENT_2026-08-16.md",
    "08-17": "/home/dlamini/ICPC2026/docs/CF_JUDGEMENT_2026-08-17.md",
}

rows = {}  # test -> list of (tag, tp, norm_tp)
for tag, path in DUMPS.items():
    with open(path) as f:
        for line in f:
            m = re.match(r"#(\d+): OK \[[^]]*\]: points [\d.]+ tp=([\d.eE+-]+) "
                         r"mean_tdr=[\d.]+ mean_tpot=[\d.]+ dist=[\d.]+ "
                         r"norm_tp=([\d.eE+-]+)", line.strip())
            if m:
                t = int(m.group(1))
                rows.setdefault(t, []).append((tag, float(m.group(2)), float(m.group(3))))

# D-143 recorded a later (D-142-era) reading for #5/#6: norm_tp 0.301/0.292 at
# tp 1.0169/0.6316 -- use as a third consistency check where possible.
extra = {5: ("D-142", 1.0169, 0.301), 6: ("D-142", 0.6316, 0.292)}
for t, v in extra.items():
    if t in rows:
        rows[t].append(v)

print(f"{'test':>4} {'runs':>4} {'tp_base':>12} {'tp_UB':>12} {'band':>12} "
      f"{'resid':>10}  note")
for t in sorted(rows):
    pts = rows[t]
    lin = [p for p in pts if 1e-9 < p[2] < 1 - 1e-9]
    note = ""
    b = ub = band = resid = None
    if len(lin) >= 2:
        # take the two with the widest n separation
        lin.sort(key=lambda p: p[2])
        (t1, tp1, n1), (t2, tp2, n2) = lin[0], lin[-1]
        if abs(n1 - n2) > 1e-6:
            band = (tp1 - tp2) / (n1 - n2)
            b = tp1 - n1 * band
            ub = b + band
            # residual check over all points
            resid = max(abs(tp - (b + n * band)) for (_, tp, n) in pts
                        if 1e-9 < n < 1 - 1e-9)
            resid = max(resid, max(0.0, b - min(tp for _, tp, n in pts if n <= 1e-9))
                        if any(n <= 1e-9 for _, _, n in pts) else 0.0)
    if b is None:
        tps = [p[1] for p in pts]
        ns = [p[2] for p in pts]
        if all(n <= 1e-9 for n in ns):
            note = f"tp<=tp_base both (tp~{min(tps):g}..{max(tps):g}); tp_base>={max(tps):g}"
        elif all(n >= 1 - 1e-9 for n in ns):
            note = f"SATURATED both (tp~{min(tps):g}..{max(tps):g}); tp_UB<={min(tps):g}"
        else:
            note = "insufficient print precision / identical runs"
    fmt = lambda x: f"{x:.6g}" if x is not None else "-"
    print(f"{t:>4} {len(pts):>4} {fmt(b):>12} {fmt(ub):>12} {fmt(band):>12} "
          f"{fmt(resid):>10}  {note}")
