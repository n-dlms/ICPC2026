#!/usr/bin/env python3
"""SA09 ceiling analysis: rigorous lower bounds on E and upper bounds on score.

For each case (zoo/adv/mimics) compute:
  (a) LINK bounds   E >= max(UP-serial, DOWN-serial)
      UP-serial   = sum_i xfer(Lin_i)              [prefill UP, 1/req, len=Lin]
                  + n_rounds_min*lat + VolD        [decode UP: >=1 header/round,
                                                   volume = sumLout*bpt exactly]
      DOWN-serial = same shape (prefill DOWN len=Lin; decode DOWN len sums to m/round)
      VERIFIED (problem.md Output Steps + interactor.py): per-remote UP len =
      that remote's member count; per-group DOWN len = D PROC group size => each
      member costs exactly bpt UP + bpt DOWN per round REGARDLESS of group size.
      Batching reduces only latency headers and table durations, never volume.
  (b) CLOUD bound   E >= (sum_i pproc(Lin_i) + sumLout*min_m dproc(m)/m) / K
  (c) LOCAL bound   E >= sum_i (S+ppre_i + S+ppost_i)
                     + n_rounds_min*(min_m(S+dpre(m)) + min_m(S+dpost(m)))
  (d) DEPENDENCY    E >= max_i (ARR_i - minARR + solo_floor_i)
      solo_floor_i = solo prefill chain + Lout_i * min-stage decode chain
  Piecewise-linear table => min of dur(m)/m and dur(m) over m in [1,R] is at an
  endpoint of {1} u listed sizes u {R} (clamped), so evaluating that finite set
  is exact.

Ceiling score = 1000*(w_tp*clamp(sumLout/E_floor) + w_c*c_w(tdr_floor, tpot_floor))
with tdr_floor = mean solo prefill (queue-free), tpot_floor = mean (Lout-1)*chain_min.
Each aggregate is floored independently => the ceiling is a valid UPPER bound on
any achievable score (score is componentwise monotone in E, tdr, tpot).

Join with SA07 achieved values (scorecard_full.csv, same binary/tree).

Usage: python3 ceiling.py --csv out.csv --table out.txt
"""
import argparse
import csv
import math
import os
import sys

ROOT = "/home/dlamini/ICPC2026"


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
        self.arrivals = []
        for _ in range(self.R):
            t = float(toks[i]); lin = int(float(toks[i + 1])); lo = int(float(toks[i + 2]))
            self.arrivals.append((t, lin, lo))
            i += 3
        self._cols = {}
        for col in range(1, 7):
            rs = sorted((r for r in self.rows if r[col] >= 0), key=lambda r: r[0])
            self._cols[col] = rs

    def dur(self, col, size):
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


def candidate_sizes(c):
    """Endpoints where piecewise-linear table minima live: {1,R} u listed (clamped)."""
    R = max(1, c.R)
    ms = {1, R}
    for r in c.rows:
        m = min(max(int(r[0]), 1), R)
        ms.add(m)
    return sorted(ms)


def bounds(c):
    R = c.R
    sumL = sum(lo for (_, _, lo) in c.arrivals)
    maxL = max((lo for (_, _, lo) in c.arrivals), default=0)
    n_rounds = max(maxL, math.ceil(sumL / R)) if R else 0
    ms = candidate_sizes(c)

    prefill_xfer = sum(c.xfer(lin) for (_, lin, _) in c.arrivals)
    decode_vol = 8.0 * c.bpt / (c.bw * 1e6) * sumL
    up = prefill_xfer + n_rounds * c.lat + decode_vol
    down = up  # symmetric: prefill DOWN len=Lin; decode DOWN len sums to m/round

    local_pre = sum((c.S + c.dur(1, lin)) + (c.S + c.dur(3, lin))
                    for (_, lin, _) in c.arrivals)
    min_dpre = min(c.S + c.dur(4, m) for m in ms)
    min_dpost = min(c.S + c.dur(6, m) for m in ms)
    # two valid forms; the per-token LP kills the V-table illusion where
    # min(S+dpre) sits at m=1 while huge groups carry the volume cheaply
    local_dec_groups = n_rounds * (min_dpre + min_dpost)
    local_dec_tokens = sumL * min((2 * c.S + c.dur(4, m) + c.dur(6, m)) / m
                                  for m in ms)
    local = local_pre + max(local_dec_groups, local_dec_tokens)

    cloud_div = (sum(c.dur(2, lin) for (_, lin, _) in c.arrivals)
                 + sumL * min(c.dur(5, m) / m for m in ms)) / max(1, c.K)
    # Partition/atomicity-aware bound: a D PROC group is atomic on ONE cloud and
    # a request's cloud is fixed at P PRE, so cloud j (n_j requests, T_j tokens)
    # has decode busy >= T_j * r_min(n_j), r_min(n) = min_{m<=n} dproc(m)/m
    # (non-increasing in n). For ANY partition over k' nonempty clouds, pigeonhole
    # gives max T_j >= sumL/k' and n_j >= T_j/maxL => r_min(n_j) >= r_min(ceil(R/k')),
    # so E >= max_j busy_j >= min_k (sumL/k) * r_min(ceil(R/k)). Certified.
    def r_min(n):
        mm = sorted({1, n} | {min(max(int(r[0]), 1), n) for r in c.rows})
        return min(c.dur(5, m) / m for m in mm)

    cloud_part = min((sumL / k) * r_min(math.ceil(R / k))
                     for k in range(1, max(1, c.K) + 1))
    cloud = max(cloud_div, cloud_part)

    min_dproc = min(c.S + c.dur(5, m) for m in ms)
    chain_min = min_dpre + c.xfer(1) + min_dproc + c.xfer(1) + min_dpost

    def solo_pref(lin):
        return ((c.S + c.dur(1, lin)) + c.xfer(lin) + (c.S + c.dur(2, lin))
                + c.xfer(lin) + (c.S + c.dur(3, lin)))

    t0 = min((t for (t, _, _) in c.arrivals), default=0.0)
    dep = 0.0
    solo_pref_sum = 0.0
    for (t, lin, lo) in c.arrivals:
        s = solo_pref(lin) + lo * chain_min
        dep = max(dep, (t - t0) + s)
        solo_pref_sum += solo_pref(lin)

    E_floor = max(up, down, local, cloud, dep)
    binding = max(("up", up), ("down", down), ("local", local),
                  ("cloud", cloud), ("dep", dep), key=lambda kv: kv[1])[0]

    G = sum(max(0, lo - 1) for (_, _, lo) in c.arrivals)
    tdr_floor = solo_pref_sum / R if R else 0.0
    tpot_floor = (sum((lo - 1) * chain_min for (_, _, lo) in c.arrivals) / G
                  if G > 0 else 0.0)
    return dict(up=up, down=down, local=local, cloud=cloud, dep=dep,
                E_floor=E_floor, binding=binding, n_rounds=n_rounds,
                tdr_floor=tdr_floor, tpot_floor=tpot_floor, sumL=sumL, G=G,
                chain_min=chain_min)


def c_w_at(c, tdr, tpot):
    e_tdr = max(0.0, (tdr - c.SLO1) / c.SLO1) if c.SLO1 > 0 else 0.0
    e_tpot = max(0.0, (tpot - c.SLO2) / c.SLO2) if c.SLO2 > 0 else 0.0
    dist = math.hypot(e_tdr, e_tpot)
    if c.dist_base > 0.0:
        return max(0.0, 1.0 - dist / c.dist_base)
    return 1.0 if dist == 0.0 else 0.0


def ceiling_score(c, b):
    tp = b["sumL"] / b["E_floor"] if b["E_floor"] > 0 else 0.0
    c_tp = max(0.0, min(1.0, (tp - c.tp_base) / (c.tp_UB - c.tp_base))) \
        if c.tp_UB > c.tp_base else 0.0
    cw = c_w_at(c, b["tdr_floor"], b["tpot_floor"])
    tp_pts = 1000.0 * c.w_tp * c_tp
    w_pts = 1000.0 * c.w_c * cw
    return tp_pts, w_pts, tp_pts + w_pts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", default=None)
    ap.add_argument("--table", default=None)
    ap.add_argument("--achieved",
                    default=f"{ROOT}/experiments/sa07/scorecard_full.csv")
    args = ap.parse_args()

    achieved = {}
    for r in csv.DictReader(open(args.achieved)):
        achieved[(r["group"], r["name"])] = r

    paths = []
    for d in ("cases/zoo", "cases/mimics", "cases/adv"):
        p = os.path.join(ROOT, d)
        paths += sorted(os.path.join(p, f) for f in os.listdir(p)
                        if f.endswith(".txt"))

    rows = []
    for path in paths:
        grp = os.path.basename(os.path.dirname(path))
        name = os.path.basename(path).replace(".txt", "")
        c = Case(path)
        b = bounds(c)
        tp_pts, w_pts, ceil_tot = ceiling_score(c, b)
        a = achieved.get((grp, name), {})
        E = float(a["E"]) if a else None
        score = float(a["score"]) if a else None
        gap = ceil_tot - score if score is not None else None
        ratio = E / b["E_floor"] if E else None
        tight = ("TIGHT" if ratio <= 1.02 else
                 "NEAR" if ratio <= 1.10 else "LOOSE") if ratio else "?"
        rows.append(dict(
            group=grp, name=name, R=c.R, sumL=b["sumL"], G=b["G"],
            w_tp=c.w_tp, w_c=c.w_c, tp_base=c.tp_base, tp_UB=c.tp_UB,
            SLO1=c.SLO1, SLO2=c.SLO2, dist_base=c.dist_base,
            b_up=b["up"], b_down=b["down"], b_local=b["local"],
            b_cloud=b["cloud"], b_dep=b["dep"], E_floor=b["E_floor"],
            binding=b["binding"], n_rounds=b["n_rounds"],
            E=E, ratio=ratio, tight=tight,
            score=score, ceil_tp=tp_pts, ceil_w=w_pts, ceil=ceil_tot,
            gap_tp=(tp_pts - float(a["tp_pts"]) if a else None),
            gap_w=(w_pts - float(a["w_pts"]) if a else None),
            gap=gap, tdr_floor=b["tdr_floor"], tpot_floor=b["tpot_floor"],
            chain_min=b["chain_min"],
            tdr=(float(a["tdr"]) if a else None),
            tpot=(float(a["tpot"]) if a else None)))

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
        print(f"wrote {args.csv} ({len(rows)} rows)")

    if args.table:
        lines = [f"{'case':<24} {'bind':>6} {'E_floor':>12} {'E':>12} "
                 f"{'E/fl':>6} {'tight':>6} {'score':>8} {'ceil_tp':>8} "
                 f"{'ceil_w':>8} {'CEIL':>8} {'gap_tp':>7} {'gap_w':>7} "
                 f"{'GAP':>7}"]
        for r in sorted(rows, key=lambda r: -(r["gap"] or -1)):
            lines.append(
                f"{r['group'][:3]}/{r['name']:<20} {r['binding']:>6} "
                f"{r['E_floor']:12.4g} {r['E'] if r['E'] is None else r['E']:12.4g} "
                f"{r['ratio'] if r['ratio'] is None else r['ratio']:6.3f} "
                f"{r['tight']:>6} "
                f"{r['score'] if r['score'] is None else r['score']:8.2f} "
                f"{r['ceil_tp']:8.2f} {r['ceil_w']:8.2f} {r['ceil']:8.2f} "
                f"{r['gap_tp']:7.1f} {r['gap_w']:7.1f} {r['gap']:7.1f}")
        text = "\n".join(lines)
        open(args.table, "w").write(text + "\n")
        print(text)

    bad = [r for r in rows if r["gap"] is not None and r["gap"] < -0.5]
    print(f"\nsanity: ceiling < achieved on {len(bad)} cases "
          + (str([(r['group'], r['name'], r['gap']) for r in bad]) if bad else "(OK)"))
    tot_gap = sum(r["gap"] for r in rows if r["gap"] is not None)
    tot_gap_tp = sum(r["gap_tp"] for r in rows if r["gap_tp"] is not None)
    tot_gap_w = sum(r["gap_w"] for r in rows if r["gap_w"] is not None)
    print(f"total local contested (ceiling-achieved): {tot_gap:.1f} "
          f"(tp {tot_gap_tp:.1f} + wait {tot_gap_w:.1f}) over {len(rows)} cases")


if __name__ == "__main__":
    main()
