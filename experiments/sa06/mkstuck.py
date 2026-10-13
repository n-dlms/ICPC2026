#!/usr/bin/env python3
"""SA06 stuck/admission probe case generator."""
import os

OUT = "/home/dlamini/ICPC2026/experiments/sa06/cases"
os.makedirs(OUT, exist_ok=True)


def case(name, arrivals, empties=(), slo1=100.0, slo2=100.0, tpub=1000.0,
         tpbase=0.0, dist_base=5.0, wtp=0.5, wc=0.5, K=2, S=1.0, lat=1.0,
         bw=1e9, bpt=1, layers=1, rows=None):
    if rows is None:
        rows = [(1, 1, 1, 1, 1, 1, 1), (8, 1, 1, 1, 1, 1, 1)]
    s = f"{K} {S} {lat} {bw} {bpt} {layers}\n"
    s += f"{slo1} {slo2} {tpub} {tpbase} {dist_base} {wtp} {wc}\n"
    s += f"{len(rows)}\n"
    for r in rows:
        s += " ".join(str(x) for x in r) + "\n"
    s += f"{len(arrivals)}\n"
    for t, lin, lout in arrivals:
        s += f"{t} {lin} {lout}\n"
    for e in empties:
        s += f"EMPTY {e}\n"
    with open(os.path.join(OUT, name), "w") as f:
        f.write(s)
    print("wrote", name)


# 4 arrivals spread out: 0, 0, 100, 200. Flat table -> batching free.
case("stuck4", [(0.0, 4, 2), (0.0, 4, 2), (100.0, 4, 2), (200.0, 4, 2)])
# Same but with local-only EMPTY wake-up frames late in the run
case("stuck4_empty", [(0.0, 4, 2), (0.0, 4, 2), (100.0, 4, 2), (200.0, 4, 2)],
     empties=(300.0, 400.0, 500.0))
# Two requests only, both at t=0 (no future arrival after frame 1)
case("stuck2", [(0.0, 4, 2), (0.0, 4, 2)])
