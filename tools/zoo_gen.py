#!/usr/bin/env python3
"""tools/zoo_gen.py — generate zoo cells for the new protocol (deterministic).

Cell catalog per TESTING_FORTRESS_DESIGN.md §2.1-2.3. Every case is seeded by
its name: rerunning regenerates byte-identical cases.
Case file format (interactor.py):
    K S latency bandwidth bytes_per_token num_layers
    SLO1 SLO2 tp_UB tp_base dist_base w_tp w_c
    N
    batch_size pp pproc ppost dp dproc dpst   (N rows; -1 = missing)
    R
    t Lin Lout  x R
    EMPTY t     (optional, any number: emit an e=0 frame at that time)

Usage: python3 tools/zoo_gen.py [--dir cases] [--cases correctness,perf,adv,all]
"""

import argparse
import math
import random
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent


def jit(rng, x):
    return x * (1.0 + 0.2 * (rng.random() - 0.5))


def gen_table(rng, n, kind, scale=1.0):
    """Rows batch 1..n (ascending; callers may shuffle). Returns [[...], ...].
    dproc(kind): sublinear -> batching helps; linear -> neutral; superlinear ->
    catastrophic; flat -> no scaling. Every column keeps row 1 and row n."""
    rows = []
    for b in range(1, n + 1):
        if kind == "one":
            vals = [3.0, 10.0, 2.0, 1.0, 4.0, 1.0]
        elif kind == "flat":
            vals = [jit(rng, 2.0), jit(rng, 8.0) * scale, jit(rng, 1.5),
                    jit(rng, 1.5), jit(rng, 5.0), jit(rng, 2.0)]
        elif kind == "linear":
            vals = [jit(rng, 2.0), jit(rng, 8.0) * scale, jit(rng, 1.5),
                    jit(rng, 1.5), 2.0 + 1.0 * b, jit(rng, 2.0)]
        elif kind == "superlinear":
            vals = [jit(rng, 2.0), jit(rng, 8.0) * scale, jit(rng, 1.5),
                    jit(rng, 1.5), 1.0 + 0.12 * b * b, jit(rng, 2.0)]
        elif kind == "pre_linear":
            # prefill columns scale with batch (=Lin): statement-correct shape
            # the old local judge could not see (it priced prefill at batch=1)
            vals = [0.5 + 0.008 * b, 1.0 + 0.004 * b, 0.3 + 0.006 * b,
                    jit(rng, 1.5), 2.0 + 1.0 * b, jit(rng, 2.0)]
        elif kind == "pre_steep":
            # steep superlinear prefill: punishes prefill starvation /
            # decode-first policies on Lin-heterogeneous fleets (CF #17/#19)
            vals = [0.5 + 0.01 * b, 1.0 + 0.004 * b, 0.3 + 0.008 * b,
                    jit(rng, 1.5), 0.5 + 0.006 * b, jit(rng, 2.0)]
        else:  # sublinear (default): decode grows ~log2
            vals = [jit(rng, 2.0), jit(rng, 8.0) * scale, jit(rng, 1.5),
                    jit(rng, 1.5), 6.0 + 2.0 * math.log2(b), jit(rng, 2.0)]
        rows.append([b] + vals)
    if kind == "gaps":
        # -1 mid-table cells; anchors (row 1, row n) stay valid for every column
        for i in range(1, n - 1):
            for col in range(1, 7):
                if rng.random() < 0.5:
                    rows[i][col] = -1.0
    return rows


def arrivals(rng, pattern, r, lin_min, lin_max, lout_min, lout_max, dt=10.0):
    out = []
    for i in range(r):
        lin = rng.randint(lin_min, lin_max)
        lout = rng.randint(lout_min, lout_max)
        if pattern == "immediate":
            t = 0.0
        elif pattern == "dribble":
            t = i * dt
        elif pattern == "delayed":
            t = 50.0 + i * dt
        elif pattern == "mixed":
            bursts = [(0.0, r // 3), (500.0, r // 3), (1200.0, r - 2 * (r // 3))]
            t = 0.0
            acc = 0
            for bt, cnt in bursts:
                for _ in range(cnt):
                    if acc == i:
                        t = bt
                        break
                    acc += 1
                if acc > i:
                    break
        else:
            t = 0.0
        out.append((t, lin, lout))
    return out


def emit(path, name, K, S, latency, bw, bpt, layers, table, arrs, empties,
         slo1, slo2, tp_ub, tp_base, dist_base, wtp, wc):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w") as f:
        f.write(f"{K} {S} {latency} {bw} {bpt} {layers}\n")
        f.write(f"{slo1} {slo2} {tp_ub} {tp_base} {dist_base} {wtp} {wc}\n")
        f.write(f"{len(table)}\n")
        for row in table:
            f.write(" ".join("%.6f" % v if isinstance(v, float) else str(v) for v in row) + "\n")
        f.write(f"{len(arrs)}\n")
        for t, lin, lout in arrs:
            f.write(f"{t} {lin} {lout}\n")
        for t in empties:
            f.write(f"EMPTY {t}\n")


def default_score():
    return dict(slo1=500.0, slo2=100.0, tp_ub=1000.0, tp_base=0.0,
                dist_base=0.1, wtp=0.5, wc=0.5)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default=str(ROOT / "cases"))
    ap.add_argument("--cases", default="all",
                    choices=["all", "correctness", "perf", "adv"])
    args = ap.parse_args()
    base = Path(args.dir)

    spec = []  # (relpath, cell dict)

    def cell(name, group, K, S, latency, bw, bpt, layers, n_rows, kind,
             pattern, r, lin, lout, scale=1.0, score=None, empties=(),
             dt=10.0, unordered=False):
        spec.append((f"{group}/{name}.txt",
                     dict(name=name, K=K, S=S, latency=latency, bw=bw, bpt=bpt,
                          layers=layers, n=n_rows, kind=kind, pattern=pattern,
                          r=r, lin=(lin, lin) if isinstance(lin, int) else lin,
                          lout=(lout, lout) if isinstance(lout, int) else lout,
                          scale=scale, score=score,
                          empties=list(empties), dt=dt, unordered=unordered)))

    # ---- correctness zoo (18 cells) ----
    cell("single_k1", "zoo", 1, 1.0, 1.0, 1.0, 1, 1, 4, "one", "immediate", 1, 4, 1)
    cell("single_k8", "zoo", 8, 1.0, 1.0, 1.0, 1, 1, 4, "one", "immediate", 1, 4, 1)
    cell("burst_100", "zoo", 8, 1.0, 1.0, 1.0, 1, 10, 8, "sublinear", "immediate", 100, 4, 4)
    cell("dribble", "zoo", 8, 1.0, 1.0, 1.0, 1, 10, 8, "sublinear", "dribble", 100, 4, 4)
    cell("zero_S", "zoo", 2, 1.0, 0.001, 100.0, 1, 64, 16, "sublinear", "immediate", 10, 8, 4)
    cell("huge_S", "zoo", 2, 10.0, 0.001, 100.0, 1, 64, 16, "sublinear", "immediate", 10, 8, 4)
    cell("one_layer", "zoo", 2, 1.0, 1.0, 1.0, 1, 1, 8, "sublinear", "immediate", 5, 4, 4)
    cell("many_layers", "zoo", 2, 1.0, 1.0, 1.0, 1, 64, 16, "sublinear", "immediate", 5, 8, 4)
    cell("table_1row", "zoo", 2, 1.0, 1.0, 1.0, 1, 5, 2, "one", "immediate", 5, 4, 4)
    cell("table_gaps", "zoo", 2, 1.0, 1.0, 1.0, 1, 5, 8, "gaps", "immediate", 5, 4, 4)
    cell("prefill_linear_mix", "zoo", 8, 0.5, 1.0, 5.0, 1, 10, 16, "pre_linear", "immediate", 400, 1, 4096, 1, 8)
    cell("prefill_steep_mix", "zoo", 8, 0.5, 1.0, 5.0, 1, 10, 16, "pre_steep", "immediate", 400, 1, 4096, 1, 8)
    cell("prefill_steep_lout1", "zoo", 8, 0.5, 1.0, 5.0, 1, 10, 16, "pre_steep", "immediate", 600, 1, 4096, 1, 1)
    cell("table_unordered", "zoo", 2, 1.0, 1.0, 1.0, 1, 5, 8, "sublinear", "immediate", 5, 4, 4,
         unordered=True)
    cell("empty_frame", "zoo", 2, 1.0, 1.0, 1.0, 1, 5, 8, "sublinear", "delayed", 1, 4, 4,
         empties=(0.0, 25.0))
    cell("precision", "zoo", 2, 1.0, 0.001, 100.0, 1, 1, 4, "one", "immediate", 1, 4, 1)
    cell("transfer_bottleneck", "zoo", 2, 1.0, 50.0, 0.001, 1000000, 5, 8, "flat",
         "immediate", 10, 4, 4)
    cell("compute_bottleneck", "zoo", 2, 1.0, 0.001, 100.0, 1, 5, 8, "linear",
         "immediate", 10, 4, 4, scale=50.0)
    cell("prefill_only", "zoo", 2, 1.0, 1.0, 1.0, 1, 5, 32, "sublinear", "immediate", 5, 32, 1)
    cell("decode_only", "zoo", 2, 1.0, 1.0, 1.0, 1, 5, 8, "sublinear", "immediate", 5, 1, 16)
    cell("mixed", "zoo", 8, 1.0, 1.0, 1.0, 1, 10, 8, "sublinear", "mixed", 50, 4, 4)

    # ---- performance zoo (6 cells) ----
    cell("scale_n_10", "perf", 8, 1.0, 1.0, 1.0, 1, 10, 8, "sublinear", "immediate", 10, 4, 4)
    cell("scale_n_100", "perf", 8, 1.0, 1.0, 1.0, 1, 10, 8, "sublinear", "immediate", 100, 4, 4)
    cell("scale_n_1000", "perf", 8, 1.0, 1.0, 1.0, 1, 10, 8, "sublinear", "immediate", 1000, 4, 4)
    cell("scale_k_1", "perf", 1, 1.0, 1.0, 1.0, 1, 10, 8, "sublinear", "immediate", 100, 4, 4)
    cell("scale_k_8", "perf", 8, 1.0, 1.0, 1.0, 1, 10, 8, "sublinear", "immediate", 100, 4, 4)
    cell("scale_k_16", "perf", 8, 1.0, 1.0, 1.0, 1, 10, 8, "sublinear", "immediate", 100, 4, 4)

    # ---- adversarial zoo (8 cells) ----
    s = default_score()
    cell("cloud_imbalance", "adv", 2, 1.0, 10.0, 1.0, 1000, 8, 8, "linear",
         "mixed", 60, 8, 4, scale=20.0)
    s2 = dict(default_score(), slo1=500.0, slo2=100.0)
    cell("batching_trap", "adv", 4, 10.0, 1.0, 1.0, 1, 8, 16, "linear", "immediate", 40, 4, 8)
    cell("chunking_trap", "adv", 2, 10.0, 1.0, 1.0, 1, 64, 8, "flat", "immediate", 10, 4, 4)
    cell("transfer_burst", "adv", 1, 1.0, 50.0, 0.001, 1000000, 5, 8, "flat",
         "immediate", 10, 4, 4)
    cell("tdr_sensitive", "adv", 4, 1.0, 1.0, 1.0, 1, 10, 8, "sublinear", "immediate",
         30, 4, 4, score=dict(slo1=40.0, slo2=200.0, tp_ub=1000.0, tp_base=0.0,
                              dist_base=0.1, wtp=0.0, wc=1.0))
    cell("tpot_sensitive", "adv", 4, 1.0, 1.0, 1.0, 1, 10, 8, "sublinear", "immediate",
         30, 4, 4, score=dict(slo1=2000.0, slo2=8.0, tp_ub=1000.0, tp_base=0.0,
                              dist_base=0.1, wtp=1.0, wc=0.0))
    cell("dist_base_zero", "adv", 4, 1.0, 1.0, 1.0, 1, 10, 8, "sublinear", "immediate",
         30, 4, 4, score=dict(slo1=2000.0, slo2=200.0, tp_ub=1000.0, tp_base=0.0,
                              dist_base=0.0, wtp=0.5, wc=0.5))
    cell("stuck_risk", "adv", 1, 1.0, 1.0, 1.0, 1, 5, 4, "one", "delayed", 1, 4, 4,
         empties=(0.0,))

    groups = {"correctness": "zoo", "perf": "perf", "adv": "adv"}
    n = 0
    for rel, c in spec:
        if args.cases != "all" and c["name"].split("_")[0] not in (
                "single", "burst", "dribble", "zero", "huge", "one", "many",
                "table", "empty", "precision", "transfer", "compute",
                "prefill", "decode", "mixed", "scale", "cloud", "batching",
                "chunking", "tdr", "tpot", "dist", "stuck"):
            continue
        if args.cases != "all":
            keep = (args.cases == "correctness" and rel.startswith("zoo/")) or \
                   (args.cases == "perf" and rel.startswith("perf/")) or \
                   (args.cases == "adv" and rel.startswith("adv/"))
            if not keep:
                continue
        rng = random.Random(c["name"])
        table = gen_table(rng, c["n"], c["kind"], c["scale"])
        if c.get("unordered"):
            rng.shuffle(table)
        arrs = arrivals(rng, c["pattern"], c["r"], *c["lin"], *c["lout"], dt=c["dt"])
        score = c["score"] or default_score()
        emit(base / rel, c["name"], c["K"], c["S"], c["latency"], c["bw"],
             c["bpt"], c["layers"], table, arrs, c["empties"], **score)
        n += 1
    print(f"generated {n} cases under {base}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
