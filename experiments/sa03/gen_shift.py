#!/usr/bin/env python3
"""SA03 crafted-case generators. Writes to /tmp/sa03/cases/."""
import os

OUT = "/tmp/sa03/cases"
os.makedirs(OUT, exist_ok=True)


def emit(path, K, S, lat, bw, bpt, layers, slo, table, arrivals, empties=()):
    lines = [f"{K} {S} {lat} {bw} {bpt} {layers}",
             " ".join(str(x) for x in slo),
             str(len(table))]
    for r in table:
        lines.append(" ".join(str(x) for x in r))
    evs = [(t, 1, lin, lo) for (t, lin, lo) in arrivals] + \
          [(t, 0, 0, 0) for t in empties]
    evs.sort(key=lambda e: e[0])
# case format: R lines "t lin lout"; this generator emits arrival lines only
# (the empties parameter is unused here).
    lines.append(str(len(evs)))
    for (t, _, lin, lo) in evs:
        lines.append(f"{t} {lin} {lo}")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote", path)


BS = [1, 2, 4, 8, 16, 32, 64]


def flat_table():
    # decode durations mildly sublinear; prefill constant
    return [[m, 1.0, 5.0, 1.0, 1.0, m * 0.8, 1.0] for m in BS]


# ------------------------------------------------ E1: shift invariance (1a)
def gen_shift():
    src = "/home/dlamini/ICPC2026/cases/zoo/mixed.txt"
    with open(src) as f:
        lines = f.read().splitlines()
    n_table = int(lines[2])
    hdr = lines[:3 + n_table]
    rest = lines[3 + n_table:]
    r = int(rest[0])
    rows = rest[1:1 + r]
    # mixed.txt rows are "t lin lout" with first arrival 1200
    out = list(hdr) + [str(r)]
    for row in rows:
        t, lin, lo = row.split()
        out.append(f"{float(t) - 1200.0} {lin} {lo}")
    emit(f"{OUT}/mixed_shift0.txt", 0, 0, 0, 0, 0, 0, [0], [], [])
    with open(f"{OUT}/mixed_shift0.txt", "w") as f:
        f.write("\n".join(out) + "\n")
    print("wrote", f"{OUT}/mixed_shift0.txt")


# ------------------------------------------------ E2: tail/straggler pair (1b)
def gen_tail_pair():
    slo = [1e9, 1e9, 0.5, 0.02, 5.0, 1.0, 0.0]  # tp-only scoring, linear band
    tab = flat_table()
    # A: LONG decode request arrives FIRST, short last
    # B: short arrives FIRST, LONG last. Same arrivals {0, 200}, same sumL=24.
    arrA = [(0.0, 8, 20), (200.0, 8, 4)]
    arrB = [(0.0, 8, 4), (200.0, 8, 20)]
    emit(f"{OUT}/tailA_longfirst.txt", 2, 1.0, 1.0, 100.0, 1, 8, slo, tab, arrA)
    emit(f"{OUT}/tailB_longlast.txt", 2, 1.0, 1.0, 100.0, 1, 8, slo, tab, arrB)


# ------------------------------------------------ E4: tp_base solo validation
def gen_solo_valid():
    """Isolated requests (spacing >> solo time): per-request (fin - arr)
    measured by the interactor must equal the analytic solo pipeline.
    Also validates that time before the first ARR is free (first arrival at
    t=10_000, denominator starts there)."""
    slo = [1e9, 1e9, 1e0, 0.0, 5.0, 1.0, 0.0]
    tab = flat_table()
    arrivals = [(10000.0, 8, 4), (20000.0, 64, 2), (30000.0, 1, 6)]
    emit(f"{OUT}/solo_valid.txt", 2, 1.0, 1.0, 100.0, 1, 8, slo, tab, arrivals)


# ------------------------------------------------ E5: many-layers solo (chunk policy probe)
def gen_solo_layers():
    slo = [1e9, 1e9, 1e0, 0.0, 5.0, 1.0, 0.0]
    tab = [[m, 1.0, 5.0, 1.0, 1.0, m * 0.8, 1.0] for m in BS]
    arrivals = [(0.0, 8, 4)]
    emit(f"{OUT}/solo_layers64.txt", 2, 3.0, 1.0, 100.0, 1, 64, slo, tab, arrivals)


if __name__ == "__main__":
    gen_shift()
    gen_tail_pair()
    gen_solo_valid()
    gen_solo_layers()
