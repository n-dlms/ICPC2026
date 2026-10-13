#!/usr/bin/env python3
"""SA01 independent scorer — computes the score from FIRST PRINCIPLES per
problem.md's Scoring section (lines 403-495), deliberately using the LITERAL
pooled-gap definition (per-request sorted token times, consecutive diffs) and
token times taken from D POST TDN events, NOT the telescoping identity, and
NOT the FIN event (last-token = max token time per request).

Inputs: --case <case file>  --trace <trace file from interactor --trace-file>
Outputs: a JSON-ish dump of every intermediate quantity + final score.

This is an independent reimplementation to cross-check tools/interactor.py
Sim.score() and tools/score_trace.py, written 2026-08-23.
"""
import argparse
import math
import sys


def clamp(x, base, target):
    # problem.md line 425: clamp(x;base,target)=max(0,min(1,(x-base)/(target-base)))
    return max(0.0, min(1.0, (x - base) / (target - base)))


def parse_case(path):
    toks = []
    with open(path) as f:
        for line in f:
            line = line.split("#")[0].strip()
            if line:
                toks.extend(line.split())
    i = 0
    K = int(toks[i]); i += 1
    S = float(toks[i]); i += 1
    lat = float(toks[i]); i += 1
    bw = float(toks[i]); i += 1
    bpt = int(toks[i]); i += 1
    layers = int(toks[i]); i += 1
    SLO1 = float(toks[i]); i += 1
    SLO2 = float(toks[i]); i += 1
    tp_UB = float(toks[i]); i += 1
    tp_base = float(toks[i]); i += 1
    dist_base = float(toks[i]); i += 1
    w_tp = float(toks[i]); i += 1
    w_c = float(toks[i]); i += 1
    N = int(toks[i]); i += 1
    i += 7 * N
    R = int(toks[i]); i += 1
    arrivals = []
    for _ in range(R):
        t = float(toks[i]); lin = int(toks[i + 1]); lout = int(toks[i + 2])
        arrivals.append((t, lin, lout))
        i += 3
    return dict(K=K, S=S, SLO1=SLO1, SLO2=SLO2, tp_UB=tp_UB, tp_base=tp_base,
                dist_base=dist_base, w_tp=w_tp, w_c=w_c, arrivals=arrivals)


def parse_trace(path):
    """Returns (arr_events, ppost_events, dpost_token_events, fin_events).

    Trace EVENT lines (written by interactor --trace-file):
      EVENT t=<t> ARR <rid> <lin> <lout>
      EVENT t=<t> TDN <srv> <spec...> dur=<dur>
      EVENT t=<t> XDN ...
      EVENT t=<t> FIN <rid>
    TDN spec for local tasks carries the srv prefix ("E"): tokens after "TDN ".
    """
    arrs = {}      # rid -> (t, lin, lout)
    ppost = {}     # rid -> [t1, t2...] (P POST TDN; should be exactly 1)
    tokens = {}    # rid -> [token times]
    fins = {}      # rid -> t
    with open(path) as f:
        for line in f:
            if not line.startswith("EVENT"):
                continue
            parts = line.split()
            t = float(parts[1].split("=")[1])
            kind = parts[2]
            if kind == "ARR":
                rid = int(parts[3]); lin = int(parts[4]); lo = int(parts[5])
                arrs[rid] = (t, lin, lo)
            elif kind == "TDN":
                rest = line.split("TDN ", 1)[1]
                specstr = rest.rsplit(" dur=", 1)[0]
                st = specstr.split()
                srv = st[0]; cat = st[1]; phase = st[2]
                if cat == "P" and phase == "POST":
                    rid = int(st[4])
                    ppost.setdefault(rid, []).append(t)
                elif cat == "D" and phase == "POST":
                    ids = [int(x) for x in st[5:]]
                    for rid in ids:
                        tokens.setdefault(rid, []).append(t)
            elif kind == "FIN":
                fins[int(parts[3])] = t
    return arrs, ppost, tokens, fins


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--case", required=True)
    ap.add_argument("--trace", required=True)
    args = ap.parse_args()

    cfg = parse_case(args.case)
    arrs, ppost, tokens, fins = parse_trace(args.trace)

    rids = sorted(arrs.keys())
    R = len(rids)
    problems = []
    if R != len(cfg["arrivals"]):
        problems.append(f"trace ARR count {R} != case R {len(cfg['arrivals'])}")
    for rid in rids:
        ct, clin, clout = cfg["arrivals"][rid]
        tt, tlin, tlout = arrs[rid]
        if abs(ct - tt) > 1e-9:
            problems.append(f"rid {rid} ARR t mismatch case={ct} trace={tt}")
        if clin != tlin or clout != tlout:
            problems.append(f"rid {rid} lin/lout mismatch")
        if len(tokens.get(rid, [])) != clout:
            problems.append(
                f"rid {rid} token count {len(tokens.get(rid, []))} != Lout {clout}")
        if len(ppost.get(rid, [])) != 1:
            problems.append(f"rid {rid} P POST count {len(ppost.get(rid, []))} != 1")
        if rid not in fins:
            problems.append(f"rid {rid} has no FIN event")

    # --- tdr: mean over ALL requests of (P POST completion - arrival) ---
    sum_tdr = 0.0
    for rid in rids:
        sum_tdr += ppost[rid][0] - arrs[rid][0]
    mean_tdr = sum_tdr / R

    # --- tpot: LITERAL pooled consecutive gaps (statement lines 451-458) ---
    sum_gaps = 0.0
    n_gaps = 0
    for rid in rids:
        ts = sorted(tokens.get(rid, []))
        for a, b in zip(ts, ts[1:]):
            sum_gaps += (b - a)
            n_gaps += 1
    mean_tpot = sum_gaps / n_gaps if n_gaps > 0 else 0.0

    # --- tp: sum Lout / (latest final-token time - earliest arrival) ---
    total_tokens = 0
    last_token_overall = None
    for rid in rids:
        total_tokens += arrs[rid][2]
        ts = tokens.get(rid, [])
        if ts:
            lt = max(ts)
            last_token_overall = lt if last_token_overall is None else max(last_token_overall, lt)
    earliest_arrival = min(arrs[rid][0] for rid in rids)
    latest_fin = max(fins.values()) if fins else None
    tp = total_tokens / (last_token_overall - earliest_arrival)

    # --- components (statement lines 425-486) ---
    c_tp = clamp(tp, cfg["tp_base"], cfg["tp_UB"])
    e_tdr = max(0.0, (mean_tdr - cfg["SLO1"]) / cfg["SLO1"])
    e_tpot = max(0.0, (mean_tpot - cfg["SLO2"]) / cfg["SLO2"])
    dist = math.sqrt(e_tdr ** 2 + e_tpot ** 2)
    if cfg["dist_base"] > 0.0:
        c_w = max(0.0, 1.0 - dist / cfg["dist_base"])
    else:
        c_w = 1.0 if dist == 0.0 else 0.0
    score = 1000.0 * (cfg["w_tp"] * c_tp + cfg["w_c"] * c_w)

    print(f"R                   : {R}")
    print(f"sum Lout            : {total_tokens}")
    print(f"earliest_arrival    : {earliest_arrival:.9f}")
    print(f"last_token_overall  : {last_token_overall:.9f}")
    print(f"latest_FIN          : {latest_fin:.9f}")
    print(f"tp                  : {tp:.12f}")
    print(f"mean_tdr            : {mean_tdr:.12f}")
    print(f"mean_tpot (pooled)  : {mean_tpot:.12f}   (n_gaps={n_gaps})")
    print(f"c_tp                : {c_tp:.12f}")
    print(f"e_tdr / e_tpot      : {e_tdr:.12f} / {e_tpot:.12f}")
    print(f"dist                : {dist:.12f}")
    print(f"c_w                 : {c_w:.12f}")
    print(f"SCORE               : {score:.9f}   (display {score:.3f})")
    if problems:
        print("TRACE/CASE PROBLEMS : " + "; ".join(problems))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
