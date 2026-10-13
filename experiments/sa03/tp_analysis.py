#!/usr/bin/env python3
"""SA03 tp-component analysis harness.

Runs build/submission on a case via tools/interactor.py --trace-file, parses
the trace, and computes every quantity relevant to the output-rate component:

  tp          = sum(Lout) / (last final-token time - earliest ARR)
  c_tp        = clamp((tp - tp_base)/(tp_UB - tp_base); 0, 1)
  serial T    = analytic one-at-a-time reference schedule elapsed
                (1-chunk prefill, m=1 decode rounds, solo FIFO transfers)
  tp_base_hat = sum(Lout) / T_serial   (reconstruction of the judge's tp_base)
  structural floor = arrival_span + minimal_processing_tail
  sensitivity dScore/dE = 1000*w_tp*sumLout/(E^2*(tp_UB-tp_base))  (linear region)
"""
import math
import re
import subprocess
import sys
import os

ROOT = "/home/dlamini/ICPC2026"


# ---------------------------------------------------------------- case parsing
class Case:
    def __init__(self, path):
        with open(path) as f:
            toks = f.read().split()
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
        # optional empty frames "t 0"
        self.empties = []

    def dur(self, col, size):
        """Task-table interpolation. col: 1=ppre 2=pproc 3=ppost 4=dpre 5=dproc 6=dpost."""
        rows = [r for r in self.rows if r[col] >= 0]
        rows.sort(key=lambda r: r[0])
        if not rows:
            return 1.0
        if size <= rows[0][0]:
            return rows[0][col]
        if size >= rows[-1][0]:
            return rows[-1][col]
        for a, b in zip(rows, rows[1:]):
            if a[0] <= size <= b[0]:
                if b[0] == a[0]:
                    return a[col]
                f = (size - a[0]) / (b[0] - a[0])
                return a[col] + f * (b[col] - a[col])
        return rows[-1][col]

    def xfer(self, length):
        return self.lat + 8.0 * length * self.bpt / (self.bw * 1e6)


def solo_time(c, lin, lout):
    """One request completely alone, 1-chunk prefill, m=1 decode rounds.
    Serial pipeline; every leg must wait for its predecessor."""
    p = (c.S + c.dur(1, lin))          # P PRE
    p += c.xfer(lin)                   # UP len=Lin
    p += (c.S + c.dur(2, lin))         # P PROC (1 chunk = full proc)
    p += c.xfer(lin)                   # DOWN len=Lin
    p += (c.S + c.dur(3, lin))         # P POST
    dec = ((c.S + c.dur(4, 1)) +       # D PRE (m=1)
           c.xfer(1) +                 # UP len=1
           (c.S + c.dur(5, 1)) +       # D PROC
           c.xfer(1) +                 # DOWN len=1
           (c.S + c.dur(6, 1)))        # D POST -> 1 token
    return p + lout * dec


def serial_T(c):
    return sum(solo_time(c, lin, lo) for (t, lin, lo) in c.arrivals)


def resource_floors(c):
    """Per-resource serial-time lower bounds on E (all resource use lies in
    [first_arr, last_finish], each resource is serial):
      up   : prefill UPs xfer(Lin) + >=1 decode UP per round + member bytes
      down : prefill DOWNs xfer(Lin) + >=1 decode DOWN per round + member bytes
      local: prefill local (S+ppre)+(S+ppost) per req; decode rounds each
             (S+dpre(m)+S+dpost(m)); #rounds >= max(maxLout, ceil(sumL/R))
      cloud: (sum pproc + minimal decode proc work)/K
    """
    R = len(c.arrivals)
    sumL = sum(lo for (_, _, lo) in c.arrivals)
    maxL = max((lo for (_, _, lo) in c.arrivals), default=1)
    # decode rounds
    import math as _m
    nrounds_lb = max(maxL, _m.ceil(sumL / R)) if R else 0
    # local decode per-round cost: group of size m has cost S+dpre(m)+S+dpost(m)
    ms = sorted({max(1, min(int(r[0]), R)) for r in c.rows} | {1, R})
    best_round = min((c.S + c.dur(4, m) + c.S + c.dur(6, m)) for m in ms)
    best_dproc_per_tok = min(c.dur(5, m) / m for m in ms)
    local_prefill = sum((c.S + c.dur(1, lin)) + (c.S + c.dur(3, lin))
                        for (_, lin, _) in c.arrivals)
    local_decode = nrounds_lb * best_round
    up = sum(c.xfer(lin) for (_, lin, _) in c.arrivals)
    up += nrounds_lb * c.lat + sumL * 8.0 * c.bpt / (c.bw * 1e6)
    down = up  # symmetric byte model: prefill len=Lin both ways, tokens both ways
    cloud = (sum(c.dur(2, lin) for (_, lin, _) in c.arrivals)
             + sumL * best_dproc_per_tok) / c.K
    return dict(up=up, down=down, local=local_prefill + local_decode,
                cloud=cloud, nrounds_lb=nrounds_lb)


# ---------------------------------------------------------------- trace parsing
def run_case(case_path, solver=f"{ROOT}/build/submission", timeout=600.0):
    tr = "/tmp/sa03/_trace.txt"
    r = subprocess.run(
        ["python3", f"{ROOT}/tools/interactor.py", "--case", case_path,
         "--solver", solver, "--timeout", str(timeout), "--trace-file", tr],
        capture_output=True, text=True)
    if "VERDICT ok" not in r.stdout:
        return None, r.stdout.strip() + " || " + r.stderr.strip()[-300:]
    arr, lout, ppost, first_tok, last_tok, fin = {}, {}, {}, {}, {}, {}
    cfg = None
    with open(tr) as f:
        for line in f:
            if line.startswith("CONFIG"):
                cfg = line.split()
                continue
            if not line.startswith("EVENT"):
                continue
            parts = line.split()
            t = float(parts[1].split("=")[1])
            kind = parts[2]
            if kind == "ARR":
                rid, lin, lo = int(parts[3]), int(parts[4]), int(parts[5])
                arr[rid] = t; lout[rid] = lo
            elif kind == "TDN":
                m = re.search(r"TDN E ([PD]) (PRE|POST)", line)
                if not m:
                    continue
                if m.group(1) == "P" and m.group(2) == "POST":
                    rid = int(line.split("TDN E P POST ")[1].split()[1])
                    ppost[rid] = t
                elif m.group(1) == "D" and m.group(2) == "POST":
                    ids = line.split("TDN E D POST -1 ")[1].split(" dur=")[0]
                    ids = [int(x) for x in ids.split()]
                    for rid in ids:
                        first_tok.setdefault(rid, t)
                        last_tok[rid] = t
            elif kind == "FIN":
                rid = int(parts[3])
                fin[rid] = t
    return dict(arr=arr, lout=lout, ppost=ppost, first_tok=first_tok,
                last_tok=last_tok, fin=fin, stdout=r.stdout), None


def analyze(case_path, solver=f"{ROOT}/build/submission", timeout=600.0):
    c = Case(case_path)
    d, err = run_case(case_path, solver, timeout)
    if d is None:
        raise RuntimeError(f"{case_path}: {err}")
    sumL = sum(d["lout"].values())
    first_arr = min(d["arr"].values())
    last_arr = max(d["arr"].values())
    last_fin = max(d["fin"].values()) if d["fin"] else 0.0
    E = last_fin - first_arr
    tp = sumL / E if E > 0 else 0.0
    ctp = max(0.0, min(1.0, (tp - c.tp_base) / (c.tp_UB - c.tp_base))) \
        if c.tp_UB > c.tp_base else 0.0
    Ts = serial_T(c)
    tp_base_hat = sumL / Ts if Ts > 0 else float("inf")
    # structural floor: arrival span + minimal tail = solo time of the LAST
    # arrival's request (it cannot finish before last_arr + its solo pipeline)
    last_arr_idx = max(range(len(c.arrivals)), key=lambda i: c.arrivals[i][0])
    t_l, lin_l, lo_l = c.arrivals[last_arr_idx]
    solo_l = solo_time(c, lin_l, lo_l)
    floor_E = (last_arr - first_arr) + solo_l
    rf = resource_floors(c)
    floor_E2 = max(floor_E, rf["up"], rf["down"], rf["local"], rf["cloud"])
    tp_floor = sumL / floor_E if floor_E > 0 else float("inf")
    tp_floor2 = sumL / floor_E2 if floor_E2 > 0 else float("inf")
    # sensitivity (linear region only)
    sens = None
    if c.tp_base < tp < c.tp_UB:
        sens = 1000.0 * c.w_tp * sumL / (E * E * (c.tp_UB - c.tp_base))
    # per-request finish ordering (tail diagnosis)
    finishes = sorted(d["fin"].items(), key=lambda kv: kv[1])
    # slack: how much earlier could E have ended = E - (2nd last finish)
    second_fin = finishes[-2][1] if len(finishes) > 1 else first_arr
    return dict(case=os.path.basename(case_path), c=c, sumL=sumL,
                first_arr=first_arr, last_arr=last_arr, last_fin=last_fin,
                E=E, tp=tp, ctp=ctp, Ts_serial=Ts, tp_base_hat=tp_base_hat,
                tp_base_file=c.tp_base, tp_UB=c.tp_UB, w_tp=c.w_tp, w_c=c.w_c,
                floor_E=floor_E, tp_floor=tp_floor, solo_last=solo_l,
                floor_E2=floor_E2, tp_floor2=tp_floor2, rf=rf,
                arrival_span=last_arr - first_arr, tail_after_last_arr=last_fin - last_arr,
                sens_pts_per_ms=sens, last_finisher=finishes[-1],
                second_fin=second_fin, straggler_slack=last_fin - second_fin,
                R=c.R, d=d)


def fmt_row(r, with_headroom=True):
    c = r["c"]
    head = ""
    if with_headroom:
        ctp_floor2 = max(0.0, min(1.0, (r["tp_floor2"] - c.tp_base) / (c.tp_UB - c.tp_base))) \
            if c.tp_UB > c.tp_base else 0.0
        head = f" floor2={r['floor_E2']:.3g} ctp@floor2={ctp_floor2:.4f} dctp={ctp_floor2 - r['ctp']:+.4f}"
    return (f"{r['case']:<28} R={r['R']:>4} sumL={r['sumL']:>6} "
            f"span={r['arrival_span']:>10.2f} tail={r['tail_after_last_arr']:>10.2f} "
            f"E={r['E']:>11.2f} floorE={r['floor_E']:>11.2f} "
            f"tp={r['tp']:.6g} base={r['tp_base_file']:g} UB={r['tp_UB']:g} "
            f"c_tp={r['ctp']:.4f}{head}")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        try:
            r = analyze(p)
        except Exception as e:
            print(f"{p}: ERROR {e}")
            continue
        c = r["c"]
        # straggler cost: c_tp if the clock had ended at the 2nd-last FIN
        E2 = r["second_fin"] - r["first_arr"]
        tp2 = r["sumL"] / E2 if E2 > 0 else float("inf")
        ctp2 = max(0.0, min(1.0, (tp2 - c.tp_base) / (c.tp_UB - c.tp_base))) \
            if c.tp_UB > c.tp_base else 0.0
        # relative sensitivity: points per 1% reduction of E
        rel = (10.0 * r["w_tp"] * r["sumL"] / (r["E"] * (c.tp_UB - c.tp_base))
               ) if (c.tp_base < r["tp"] < c.tp_UB) else None
        bind = max(r["rf"], key=r["rf"].get)
        ceil_rate = r["sumL"] / r["floor_E2"]
        ub_over_ceil = c.tp_UB / ceil_rate if ceil_rate > 0 else float("nan")
        ach_over_ceil = r["tp"] / ceil_rate if ceil_rate > 0 else float("nan")
        print(fmt_row(r))
        print(f"    last finisher rid={r['last_finisher'][0]} at {r['last_finisher'][1]:.3f} "
              f"(2nd-last fin {r['second_fin']:.3f}, straggler slack {r['straggler_slack']:.3f}) "
              f"| c_tp@2ndlastFIN={ctp2:.4f} (+{1000*c.w_tp*(ctp2-r['ctp']):.1f} pts) "
              f"| tp_base_hat={r['tp_base_hat']:.6g} file={r['tp_base_file']:g} "
              f"| floor2 binding: {bind} "
              f"| ceiling={ceil_rate:.6g} UB/ceiling={ub_over_ceil:.3f} ach/ceiling={ach_over_ceil:.3f} "
              f"| pts/ms {'%.3g' % r['sens_pts_per_ms'] if r['sens_pts_per_ms'] else 'n/a'} "
              f"| pts per 1% E {'%.3g' % rel if rel else 'n/a'}")
