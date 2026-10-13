#!/usr/bin/env python3
"""tools/score_trace.py — PART 9.2 scorer.

Reads a PART 9 trace produced by `interactor.py --trace-file` and computes the
EXACT score (mirrors tools/interactor.py Sim.score, lines 382-397). The trace
is self-contained: it carries a CONFIG line (all scoring params) plus ARR
(with lout), TDN, and FIN events, so no case file is needed.

Usage:
  python3 tools/score_trace.py --trace /tmp/trace.txt
  python3 tools/score_trace.py --trace /tmp/trace.txt --per-request
"""
import argparse
import math
import sys


def parse_tdn_spec(line):
    """Return (srv, step_kind, ids_or_rid) from a TDN trace line.

    TDN format:  EVENT t=<t> TDN <srv> <spec...> dur=<dur>
    The full token list after "TDN " is [srv, cat, phase, ...]:
      P POST spec = E P POST <remote> <rid>        -> rid  = st[4]
      D POST spec = E D POST -1 <m> <ids...>       -> ids = st[5:]
    (srv "E" shifts indices by one vs the raw interactor spec.)
    """
    rest = line.split("TDN ", 1)[1]
    specstr, _ = rest.rsplit(" dur=", 1)
    st = specstr.split()
    srv = st[0]
    cat = st[1]    # "P" or "D"
    phase = st[2]  # "PRE" or "POST"
    if cat == "P" and phase == "POST":
        return ("P_POST", int(st[4]))
    if cat == "D" and phase == "POST":
        ids = [int(x) for x in st[5:]]
        return ("D_POST", ids)
    return (None, None)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trace", required=True)
    ap.add_argument("--per-request", action="store_true")
    args = ap.parse_args()

    cfg = {}
    arr = {}        # rid -> arrival time
    lout = {}       # rid -> lout (decode tokens)
    ppost = {}      # rid -> P POST TDN completion time (TDR endpoint)
    first_t = {}    # rid -> first D POST TDN time
    last_t = {}     # rid -> last D POST TDN time
    fin_t = {}      # rid -> FIN time

    with open(args.trace) as f:
        for line in f:
            line = line.rstrip("\n")
            if not line:
                continue
            if line.startswith("CONFIG"):
                toks = line.split()
                cfg["K"] = int(toks[1]); cfg["S"] = float(toks[2])
                cfg["latency"] = float(toks[3]); cfg["bw"] = float(toks[4])
                cfg["bpt"] = int(toks[5]); cfg["layers"] = int(toks[6])
                cfg["SLO1"] = float(toks[7]); cfg["SLO2"] = float(toks[8])
                cfg["tp_UB"] = float(toks[9]); cfg["tp_base"] = float(toks[10])
                cfg["dist_base"] = float(toks[11]); cfg["w_tp"] = float(toks[12])
                cfg["w_c"] = float(toks[13]); cfg["N"] = int(toks[14])
                continue
            if not line.startswith("EVENT"):
                continue
            parts = line.split()
            t = float(parts[1].split("=")[1])
            kind = parts[2]
            if kind == "ARR":
                rid = int(parts[3]); lin = int(parts[4]); lo = int(parts[5])
                arr[rid] = t; lout[rid] = lo
            elif kind == "TDN":
                sk, val = parse_tdn_spec(line)
                if sk == "P_POST":
                    ppost[val] = t
                elif sk == "D_POST":
                    for rid in val:
                        if rid not in first_t or t < first_t[rid]:
                            first_t[rid] = t
                        if rid not in last_t or t > last_t[rid]:
                            last_t[rid] = t
            elif kind == "FIN":
                rid = int(parts[3])
                fin_t[rid] = t

    if not cfg:
        sys.stderr.write("error: no CONFIG line in trace\n")
        return 1

    R = len(arr)
    if R == 0:
        sys.stderr.write("error: no ARR events in trace\n")
        return 1

    # --- TDR (per request: P POST completion - arrival) ---
    sum_tdr = 0.0
    for rid in arr:
        sum_tdr += (ppost.get(rid, 0.0) - arr[rid])
    mean_tdr = sum_tdr / R

    # --- TPOT (pooled mean gap = total decode span / pooled gap count) ---
    # Statement §Waiting-Time Component: gaps are consecutive token pairs
    # (1 <= j < Lout[i]); a request with Lout=1 contributes no gap.
    sum_span = 0.0
    tokens = 0
    total_gaps = 0
    for rid in lout:
        tokens += lout[rid]
        total_gaps += lout[rid] - 1
        if rid in first_t:
            span = last_t.get(rid, first_t[rid]) - first_t[rid]
            sum_span += span
    mean_tpot = sum_span / total_gaps if total_gaps > 0 else 0.0

    # --- Throughput ---
    tok_out = tokens
    first_arrival = min(arr.values())
    last_finish = max(fin_t.values()) if fin_t else 0.0
    tp = tok_out / (last_finish - first_arrival) if last_finish > first_arrival else 0.0

    # --- Score (exact, mirrors Sim.score) ---
    c_tp = 0.0
    if cfg["tp_UB"] > cfg["tp_base"]:
        c_tp = max(0.0, min(1.0, (tp - cfg["tp_base"]) / (cfg["tp_UB"] - cfg["tp_base"])))
    e_tdr = max(0.0, (mean_tdr - cfg["SLO1"]) / cfg["SLO1"])
    e_tpot = max(0.0, (mean_tpot - cfg["SLO2"]) / cfg["SLO2"])
    dist = math.sqrt(e_tdr * e_tdr + e_tpot * e_tpot)
    if cfg["dist_base"] > 0.0:
        c_w = max(0.0, 1.0 - dist / cfg["dist_base"])
    else:
        c_w = 1.0 if dist == 0.0 else 0.0
    final = 1000.0 * (cfg["w_tp"] * c_tp + cfg["w_c"] * c_w)

    print("=== PART 9.2 score (from trace) ===")
    print(f"requests            : {R}")
    print(f"mean_tdr            : {mean_tdr:.6f}   (SLO1={cfg['SLO1']})")
    print(f"mean_tpot (pooled)  : {mean_tpot:.6f}   (SLO2={cfg['SLO2']})")
    print(f"throughput tp       : {tp:.6f}   (base={cfg['tp_base']} UB={cfg['tp_UB']})")
    print(f"c_tp  (output_rate) : {c_tp:.6f}")
    print(f"dist                : {dist:.6f}   (dist_base={cfg['dist_base']})")
    print(f"c_w  (waiting)      : {c_w:.6f}")
    print(f"Final Score         : {final:.3f}")
    print(f"  = 1000 * ({cfg['w_tp']} * {c_tp:.6f} + {cfg['w_c']} * {c_w:.6f})")
    if args.per_request:
        print("--- per-request tdr ---")
        for rid in sorted(arr):
            print(f"  req {rid}: tdr={ppost.get(rid, 0.0) - arr[rid]:.4f} lout={lout[rid]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
