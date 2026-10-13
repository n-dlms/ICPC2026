#!/usr/bin/env python3
"""SA07 standing scorecard harness (2026-08-23).

Consolidates SA01 (formula), SA03 (tp interval + solo reconstruction + resource
floors), SA04 (waiting geometry + regime census), SA05 (loss decomposition),
SA06 (admission value) into ONE normative per-case card:

  given a case file + a solver run (tools/interactor.py --trace-file), print
    - scoring params (w_tp/w_c, tp_base/tp_UB, SLO1/SLO2, dist_base)
    - achieved aggregates E (elapsed), tdr (pool mean), tpot (= mean span over
      gap count G), excesses, dist, both components, total
    - regime labels: tp side (below-base/linear/saturated) x wait side
      (met/linear/saturated-BAD/binary-cliff)
    - MARGINAL GRADIENTS dScore/dE, dScore/d(mean tdr), dScore/d(mean span)
      in pts/ms, each 0 outside the linear regions
    - WHAT-IF: score delta if E reduced X% (tp-linear only, also E->structural
      floor2), tdr reduced to the queue-free solo prefill-pipeline floor,
      span reduced Y% (also -> solo m=1 dependency-chain floor) -- all with
      clamps respected (full score recomputed)
    - structural diagnosis: binding resource floor, arrival span, tail after
      last ARR, straggler slack (last FIN - 2nd-last FIN)

Usage:
  python3 scorecard.py --all                     # zoo + adv + mimics
  python3 scorecard.py --case cases/zoo/mixed.txt [--case ...]
  python3 scorecard.py --all --csv out.csv --table table.txt
  optional: --es 1,5,10 --spans 10,25  (what-if percentages)
            --solver build/submission --jobs 8

Self-contained: no imports from other experiments (case parser, task-table
interpolation, xfer model, solo-time reconstruction inlined from SA03's
validated tp_analysis.py).
"""
import argparse
import csv
import math
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = "/home/dlamini/ICPC2026"
DEFAULT_SOLVER = f"{ROOT}/build/submission"
INTERACTOR = f"{ROOT}/tools/interactor.py"


# ----------------------------------------------------------------- case model
class Case:
    def __init__(self, path):
        toks = []
        for line in open(path):
            line = line.split("#")[0].strip()
            if line:
                toks.extend(line.split())
        i = 0
        self.K = int(float(toks[i])); i += 1
        self.S = float(toks[i]); i += 1
        self.lat = float(toks[i]); i += 1
        self.bw = float(toks[i]); i += 1
        self.bpt = int(float(toks[i])); i += 1
        self.layers = int(float(toks[i])); i += 1
        self.SLO1 = float(toks[i]); i += 1
        self.SLO2 = float(toks[i]); i += 1
        self.tp_UB = float(toks[i]); i += 1
        self.tp_base = float(toks[i]); i += 1
        self.dist_base = float(toks[i]); i += 1
        self.w_tp = float(toks[i]); i += 1
        self.w_c = float(toks[i]); i += 1
        self.N = int(float(toks[i])); i += 1
        self.rows = []
        for _ in range(self.N):
            r = [float(x) for x in toks[i:i + 7]]
            self.rows.append([int(r[0])] + r[1:])
            i += 7
        self.R = int(float(toks[i])); i += 1
        self.arrivals = []  # (t, lin, lout)
        for _ in range(self.R):
            t = float(toks[i]); lin = int(float(toks[i + 1])); lo = int(float(toks[i + 2]))
            self.arrivals.append((t, lin, lo))
            i += 3
        # interpolation tables per column (col 1..6), sorted by batch size
        self._cols = {}
        for col in range(1, 7):
            rs = sorted((r for r in self.rows if r[col] >= 0), key=lambda r: r[0])
            self._cols[col] = rs

    def dur(self, col, size):
        """Task-table duration, cols 1=ppre 2=pproc 3=ppost 4=dpre 5=dproc 6=dpost."""
        rs = self._cols[col]
        if not rs:
            return 1.0
        if size <= rs[0][0]:
            return rs[0][col]
        if size >= rs[-1][0]:
            return rs[-1][col]
        for a, b in zip(rs, rs[1:]):
            if a[0] <= size <= b[0]:
                if b[0] == a[0]:
                    return a[col]
                f = (size - a[0]) / (b[0] - a[0])
                return a[col] + f * (b[col] - a[col])
        return rs[-1][col]

    def xfer(self, length):
        return self.lat + 8.0 * length * self.bpt / (self.bw * 1e6)


def solo_prefill(c, lin):
    """Queue-free solo prefill pipeline: ARR -> P POST done (tdr floor per req).
    1-chunk prefill, no FIFO contention (SA03 solo reconstruction, validated
    to 1e-8 ms on isolated-request traces)."""
    return ((c.S + c.dur(1, lin)) + c.xfer(lin) + (c.S + c.dur(2, lin))
            + c.xfer(lin) + (c.S + c.dur(3, lin)))


def solo_round_m1(c):
    """Solo m=1 decode dependency chain D_PRE+UP+D_PROC+DOWN+D_POST."""
    return ((c.S + c.dur(4, 1)) + c.xfer(1) + (c.S + c.dur(5, 1))
            + c.xfer(1) + (c.S + c.dur(6, 1)))


def solo_time(c, lin, lout):
    return solo_prefill(c, lin) + lout * solo_round_m1(c)


def resource_floors(c):
    """Serial-time lower bounds on E per resource (SA03, optimistic batching
    for local/cloud -> still valid lower bounds)."""
    R = c.R if c.R else 1
    sumL = sum(lo for (_, _, lo) in c.arrivals)
    maxL = max((lo for (_, _, lo) in c.arrivals), default=1)
    nrounds_lb = max(maxL, math.ceil(sumL / R)) if R else 0
    ms = sorted({max(1, min(int(r[0]), R)) for r in c.rows} | {1, R})
    best_round = min((c.S + c.dur(4, m) + c.S + c.dur(6, m)) for m in ms)
    best_dproc_per_tok = min(c.dur(5, m) / m for m in ms)
    local_prefill = sum((c.S + c.dur(1, lin)) + (c.S + c.dur(3, lin))
                        for (_, lin, _) in c.arrivals)
    local_decode = nrounds_lb * best_round
    up = sum(c.xfer(lin) for (_, lin, _) in c.arrivals)
    up += nrounds_lb * c.lat + sumL * 8.0 * c.bpt / (c.bw * 1e6)
    cloud = (sum(c.dur(2, lin) for (_, lin, _) in c.arrivals)
             + sumL * best_dproc_per_tok) / c.K
    return dict(up=up, down=up, local=local_prefill + local_decode,
                cloud=cloud, nrounds_lb=nrounds_lb)


# ----------------------------------------------------------------- run + parse
def run_case(case_path, solver, trace_path, timeout):
    r = subprocess.run(
        ["python3", INTERACTOR, "--case", case_path, "--solver", solver,
         "--trace-file", trace_path, "--timeout", str(int(timeout))],
        capture_output=True, text=True, timeout=timeout + 60)
    out = r.stdout.strip().splitlines()
    verd = out[-1] if out else "NO-OUTPUT"
    if "VERDICT ok" not in verd:
        return None, verd
    return verd, None


def parse_trace(trace_path):
    """arr/lin/lout, ppost time, token times (D POST TDN completions), FIN."""
    arr, lin, lout, ppost, tok, fin = {}, {}, {}, {}, {}, {}
    for line in open(trace_path):
        if not line.startswith("EVENT"):
            continue
        p = line.split()
        t = float(p[1].split("=")[1])
        k = p[2]
        if k == "ARR":
            arr[int(p[3])] = t; lin[int(p[3])] = int(p[4]); lout[int(p[3])] = int(p[5])
        elif k == "TDN":
            rest = line.split("TDN ", 1)[1]
            specstr = rest.rsplit(" dur=", 1)[0]
            st = specstr.split()
            if st[1] == "P" and st[2] == "POST":
                ppost[int(st[4])] = t
            elif st[1] == "D" and st[2] == "POST":
                for rid in [int(x) for x in st[5:]]:
                    tok.setdefault(rid, []).append(t)
        elif k == "FIN":
            fin[int(p[3])] = t
    return arr, lin, lout, ppost, tok, fin


# ----------------------------------------------------------------- scoring
def score_from(c, E, mean_tdr, mean_tpot):
    """Full clamped score at hypothetical aggregates (saturation respected)."""
    if E is None or E <= 0:
        tp = 0.0
    else:
        tp = c.sumL / E
    if c.tp_UB > c.tp_base:
        c_tp = max(0.0, min(1.0, (tp - c.tp_base) / (c.tp_UB - c.tp_base)))
    else:
        c_tp = 0.0
    e_tdr = max(0.0, (mean_tdr - c.SLO1) / c.SLO1) if c.SLO1 > 0 else 0.0
    e_tpot = max(0.0, (mean_tpot - c.SLO2) / c.SLO2) if c.SLO2 > 0 else 0.0
    dist = math.hypot(e_tdr, e_tpot)
    if c.dist_base > 0.0:
        c_w = max(0.0, 1.0 - dist / c.dist_base)
    else:
        c_w = 1.0 if dist == 0.0 else 0.0
    return 1000.0 * (c.w_tp * c_tp + c.w_c * c_w)


def regimes(c, tp, dist):
    if tp <= c.tp_base:
        tr = "tp-below-base"
    elif tp >= c.tp_UB:
        tr = "tp-sat"
    else:
        tr = "tp-linear"
    if c.dist_base <= 0.0:
        wr = "cliff-met" if dist == 0.0 else "cliff-broken"
    elif dist == 0.0:
        wr = "wait-met"
    elif dist >= c.dist_base:
        wr = "wait-sat0"
    else:
        wr = "wait-linear"
    return tr, wr


def gradients(c, tp, E, e_tdr, e_tpot, dist):
    """pts/ms derivatives, 0 in every clamped/saturated region (SA01/SA04)."""
    v_tp = 0.0
    if c.tp_UB > c.tp_base and c.tp_base < tp < c.tp_UB:
        v_tp = 1000.0 * c.w_tp / (c.tp_UB - c.tp_base)      # pts per unit tp
    dE = -v_tp * tp / E if E > 0 else 0.0                    # pts per +1ms of E
    v_tdr = v_tpot = 0.0
    if c.w_c > 0.0 and c.dist_base > 0.0 and 0.0 < dist < c.dist_base:
        if c.SLO1 > 0.0 and e_tdr > 0.0:
            v_tdr = 1000.0 * c.w_c * e_tdr / (c.dist_base * dist * c.SLO1)
        if c.SLO2 > 0.0 and e_tpot > 0.0:
            v_tpot = 1000.0 * c.w_c * e_tpot / (c.dist_base * dist * c.SLO2)
    return dE, -v_tdr, -v_tpot   # dScore/dE, dScore/d(mean tdr), dScore/d(mean span)


# ----------------------------------------------------------------- one case
def analyze(case_path, solver, trace_path, timeout, es_pcts, span_pcts):
    name = os.path.basename(case_path).replace(".txt", "")
    grp = os.path.basename(os.path.dirname(case_path))
    c = Case(case_path)
    verd, err = run_case(case_path, solver, trace_path, timeout)
    if verd is None:
        return dict(group=grp, name=name, error=err)
    arr, lin, lout, ppost, tok, fin = parse_trace(trace_path)

    R = len(arr)
    c.sumL = sum(lout.values())
    G = sum(max(0, lout[r] - 1) for r in arr)          # gap count (tpot pool)
    sum_span = sum((max(tok[r]) - min(tok[r])) for r in arr if tok.get(r))
    mean_tdr = sum(ppost[r] - arr[r] for r in arr) / R
    mean_tpot = sum_span / G if G > 0 else 0.0
    first_arr = min(arr.values())
    last_fin = max(fin.values())
    E = last_fin - first_arr
    tp = c.sumL / E if E > 0 else 0.0
    e_tdr = max(0.0, (mean_tdr - c.SLO1) / c.SLO1) if c.SLO1 > 0 else 0.0
    e_tpot = max(0.0, (mean_tpot - c.SLO2) / c.SLO2) if c.SLO2 > 0 else 0.0
    dist = math.hypot(e_tdr, e_tpot)
    score = score_from(c, E, mean_tdr, mean_tpot)
    tr, wr = regimes(c, tp, dist)
    dE, dtdr, dspan = gradients(c, tp, E, e_tdr, e_tpot, dist)

    # ---- structural floors
    tdr_floor = sum(solo_prefill(c, lin[r]) for r in arr) / R
    round1 = solo_round_m1(c)
    span_floor_sum = sum((lout[r] - 1) * round1 for r in arr if lout[r] > 1)
    span_floor = span_floor_sum / G if G > 0 else 0.0
    rf = resource_floors(c)
    last_arr = max(arr.values())
    solo_last = max(solo_time(c, lin[r], lout[r]) for r in arr
                    if abs(arr[r] - last_arr) < 1e-9) if arr else 0.0
    floor_E2 = max((last_arr - first_arr) + solo_last,
                   rf["up"], rf["down"], rf["local"], rf["cloud"])
    binding = max(("up", rf["up"]), ("down", rf["down"]),
                  ("local", rf["local"]), ("cloud", rf["cloud"]),
                  key=lambda kv: kv[1])[0]
    fins = sorted(fin.values())
    straggler_slack = (fins[-1] - fins[-2]) if len(fins) > 1 else 0.0

    # ---- what-if levers (each re-scores with clamps; other axes held fixed)
    pot = {}
    for x in es_pcts:
        pot[f"dE-{x}%"] = score_from(c, E * (1 - x / 100.0), mean_tdr, mean_tpot) - score
    pot["dE->floor2"] = (score_from(c, floor_E2, mean_tdr, mean_tpot) - score
                         if floor_E2 < E else 0.0)
    pot["dtdr->floor"] = (score_from(c, E, max(tdr_floor, 0.0), mean_tpot) - score
                          if tdr_floor < mean_tdr else 0.0)
    for y in span_pcts:
        pot[f"dspan-{y}%"] = (score_from(c, E, mean_tdr, mean_tpot * (1 - y / 100.0))
                              - score if G > 0 else 0.0)
    pot["dspan->floor"] = (score_from(c, E, mean_tdr, span_floor) - score
                           if G > 0 and span_floor < mean_tpot else 0.0)

    return dict(
        group=grp, name=name, verdict=verd, R=R, sumL=c.sumL, G=G,
        w_tp=c.w_tp, w_c=c.w_c, tp_base=c.tp_base, tp_UB=c.tp_UB,
        SLO1=c.SLO1, SLO2=c.SLO2, dist_base=c.dist_base,
        E=E, arrival_span=last_arr - first_arr, tail=last_fin - last_arr,
        straggler_slack=straggler_slack,
        tp=tp, c_tp=(max(0.0, min(1.0, (tp - c.tp_base) / (c.tp_UB - c.tp_base)))
                     if c.tp_UB > c.tp_base else 0.0),
        tdr=mean_tdr, tpot=mean_tpot, e_tdr=e_tdr, e_tpot=e_tpot, dist=dist,
        c_w=(max(0.0, 1.0 - dist / c.dist_base) if c.dist_base > 0
             else (1.0 if dist == 0.0 else 0.0)),
        tp_pts=1000.0 * c.w_tp * (max(0.0, min(1.0, (tp - c.tp_base) /
                  (c.tp_UB - c.tp_base))) if c.tp_UB > c.tp_base else 0.0),
        w_pts=1000.0 * c.w_c * (max(0.0, 1.0 - dist / c.dist_base)
                  if c.dist_base > 0 else (1.0 if dist == 0.0 else 0.0)),
        score=score, tp_regime=tr, wait_regime=wr,
        dE=dE, dtdr=dtdr, dspan=dspan,
        tdr_floor=tdr_floor, span_floor=span_floor, floor_E2=floor_E2,
        binding=binding, **pot)


# ----------------------------------------------------------------- driver
def gather_cases(args):
    paths = list(args.case)
    if args.all:
        for d in ("cases/zoo", "cases/mimics", "cases/adv"):
            p = os.path.join(ROOT, d)
            paths += sorted(os.path.join(p, f) for f in os.listdir(p)
                            if f.endswith(".txt"))
    if not paths:
        sys.exit("no cases (use --case or --all)")
    return paths


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--case", action="append", default=[])
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--solver", default=DEFAULT_SOLVER)
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--timeout", type=float, default=600.0)
    ap.add_argument("--trace-dir", default="/tmp/sa07/traces")
    ap.add_argument("--csv", default=None)
    ap.add_argument("--table", default=None)
    ap.add_argument("--es", default="1,5,10",
                    help="comma list of E-reduction %% for what-if")
    ap.add_argument("--spans", default="10,25",
                    help="comma list of span-reduction %% for what-if")
    args = ap.parse_args()
    es_pcts = [float(x) for x in args.es.split(",")]
    span_pcts = [float(x) for x in args.spans.split(",")]
    os.makedirs(args.trace_dir, exist_ok=True)
    cases = gather_cases(args)

    def one(cp):
        tp = os.path.join(args.trace_dir,
                          os.path.basename(os.path.dirname(cp)) + "_" +
                          os.path.basename(cp).replace(".txt", ".trace"))
        try:
            return analyze(cp, args.solver, tp, args.timeout, es_pcts, span_pcts)
        except Exception as e:
            return dict(group=os.path.basename(os.path.dirname(cp)),
                        name=os.path.basename(cp), error=repr(e))

    with ThreadPoolExecutor(max_workers=args.jobs) as ex:
        rows = list(ex.map(one, cases))

    # ---- table
    lines = []
    hdr = (f"{'case':<26} {'R':>4} {'G':>5} {'w_tp':>4} {'tp':>9} {'base':>9} "
           f"{'UB':>9} {'c_tp':>5} {'tppts':>6} {'tdr':>9} {'SLO1':>8} {'tpot':>8} "
           f"{'SLO2':>7} {'dist':>8} {'db':>7} {'c_w':>5} {'wpts':>6} {'score':>7} "
           f"{'regime':<28} {'dE':>8} {'dtdr':>7} {'dspan':>7}")
    lines.append(hdr)
    for r in rows:
        if "error" in r:
            lines.append(f"{r['group'][:3]}/{r['name']:<22} ERROR {str(r['error'])[:70]}")
            continue
        lines.append(
            f"{r['group'][:3]}/{r['name']:<22} {r['R']:4d} {r['G']:5d} "
            f"{r['w_tp']:4.2f} {r['tp']:9.4g} {r['tp_base']:9.3g} {r['tp_UB']:9.3g} "
            f"{r['c_tp']:5.3f} {r['tp_pts']:6.1f} {r['tdr']:9.2f} {r['SLO1']:8.2f} "
            f"{r['tpot']:8.2f} {r['SLO2']:7.2f} {r['dist']:8.3f} {r['dist_base']:7.2f} "
            f"{r['c_w']:5.3f} {r['w_pts']:6.1f} {r['score']:7.2f} "
            f"{r['tp_regime']+'/'+r['wait_regime']:<28} "
            f"{r['dE']:8.4f} {r['dtdr']:7.3f} {r['dspan']:7.3f}")
    # what-if block
    potkeys = ([f"dE-{x}%" for x in es_pcts] + ["dE->floor2", "dtdr->floor"]
               + [f"dspan-{y}%" for y in span_pcts] + ["dspan->floor"])
    lines.append("")
    lines.append("POINTS ON THE TABLE (score delta, clamps respected):")
    lines.append(f"{'case':<26} " + " ".join(f"{k:>12}" for k in potkeys)
                 + f" {'E':>10} {'Efl2':>10} {'tdrfl':>9} {'spanfl':>8} {'bind':>6} {'slack':>9}")
    for r in rows:
        if "error" in r:
            continue
        lines.append(f"{r['group'][:3]}/{r['name']:<22} "
                     + " ".join(f"{r[k]:12.2f}" for k in potkeys)
                     + f" {r['E']:10.2f} {r['floor_E2']:10.2f} {r['tdr_floor']:9.2f} "
                     f"{r['span_floor']:8.2f} {r['binding']:>6} {r['straggler_slack']:9.2f}")
    text = "\n".join(lines)
    print(text)
    if args.table:
        with open(args.table, "w") as f:
            f.write(text + "\n")
    if args.csv:
        keys = []
        for r in rows:
            for k in r:
                if k not in keys:
                    keys.append(k)
        with open(args.csv, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=keys)
            w.writeheader()
            w.writerows(rows)
        print(f"\nwrote {args.csv} ({len(rows)} rows)")


if __name__ == "__main__":
    main()
