#!/usr/bin/env python3
"""SA06 admission-delay case: rid0 long (Lout=12) arrives alone at t=0;
herd of 8 (Lout=3) arrives at t=50. Steep-S, mildly sublinear decode table."""
import os

OUT = "/home/dlamini/ICPC2026/experiments/sa06/cases"
os.makedirs(OUT, exist_ok=True)


def case(name, wtp=0.5, slo1=60.0, lout0=12, herd_lout=3, herd_t=50.0,
         herd_n=8, slo2=10.0, dist_base=2.0, tpub=0.3, S=5.0, K=2,
         dpre1=1.25, dpre64=3.25, herd_dt=0.0):
    wc = 1.0 - wtp
    s = f"{K} {S} 1.0 1e9 1 1\n"
    s += f"{slo1} {slo2} {tpub} 0.0 {dist_base} {wtp} {wc}\n"
    s += "2\n"
    s += f"1 1 1 1 {dpre1} {dpre1} {dpre1}\n"
    s += f"64 1 1 1 {dpre64} {dpre64} {dpre64}\n"
    arr = [(0.0, 4, lout0)]
    for i in range(herd_n):
        arr.append((herd_t + i * herd_dt, 4, herd_lout))
    s += f"{len(arr)}\n"
    for t, lin, lout in arr:
        s += f"{t} {lin} {lout}\n"
    with open(os.path.join(OUT, name), "w") as f:
        f.write(s)
    print("wrote", name)


case("adm_base")            # w_tp=.5
case("adm_tp", wtp=1.0)     # pure tp: isolates channel (a)
case("adm_wc", wtp=0.0)     # pure waiting: isolates tdr/tpot damage
case("adm_short0", lout0=3) # rid0 NOT the tail owner
