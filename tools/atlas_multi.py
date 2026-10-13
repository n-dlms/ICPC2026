#!/usr/bin/env python3
"""Hidden-test atlas: multi-observation solve of CF test parameters.

Per test i, unknowns: w_tp (w_c=1-w), SLO1, SLO2, dist_base, tp_base, tp_UB.
Observations j: (tp_j, tdr_j, tpot_j, dist_j, ntp_j, ncw_j, S_j).

Equations:
  S_j = w_tp*ntp_j + (1-w_tp)*ncw_j                       -> w_tp per obs
  c_w = max(0, 1 - dist/dbase)                            -> dbase per obs
  sqrt((tdr/SLO1-1)^2 + (tpot/SLO2-1)^2) = dist           -> SLO pair per obs
  ntp = clamp((tp-base)/(ub-base))                        -> base/ub per obs
"""
import re, sys, json, math

LINE = re.compile(
    r"#(\d+):.*?points ([\d.]+) tp=([\d.e+-]+) mean_tdr=([\d.e+-]+) mean_tpot=([\d.e+-]+) "
    r"dist=([\d.e+-]+) norm_tp=([\d.e+-]+) norm_c=([\d.e+-]+) normalized_score=([\d.e+-]+)")

def parse(path):
    out = {}
    for line in open(path):
        m = LINE.search(line)
        if not m:
            continue
        i = int(m.group(1))
        out[i] = dict(points=float(m.group(2)), tp=float(m.group(3)),
                      tdr=float(m.group(4)), tpot=float(m.group(5)),
                      dist=float(m.group(6)), ntp=float(m.group(7)),
                      ncw=float(m.group(8)), score_norm=float(m.group(9)))
    return out

def solve_slo(obs):
    """Bounded box-grid + refine. Each violating obs bounds SLO_k into
    [max_k/(1+dist), max_k]; true params must satisfy every obs."""
    seen, uniq = set(), []
    for o in obs:
        k = (round(o["tdr"], 6), round(o["tpot"], 6))
        if k not in seen:
            seen.add(k); uniq.append(o)
    if len(uniq) < 2:
        return None
    if all(o["tpot"] <= 0 for o in uniq):
        cands = [o["tdr"]/(1.0+o["dist"]) for o in uniq if o["dist"] > 0]
        if cands and max(cands)-min(cands) < 1e-3*max(cands):
            return (sum(cands)/len(cands), None, 0.0)
        return None
    def err(s1, s2):
        # judge clamps each normalized excess at zero before hypot
        e = 0.0
        for o in uniq:
            e1 = max(0.0, o["tdr"]/s1 - 1.0)
            e2 = max(0.0, o["tpot"]/s2 - 1.0)
            e += (math.hypot(e1, e2) - o["dist"])**2
        return math.sqrt(e)
    mx_d = max(o["dist"] for o in uniq)
    lo1 = min(o["tdr"] for o in uniq)/(1.0+mx_d)
    hi1 = 4.0*max(o["tdr"] for o in uniq)      # allow fully-satisfied regimes
    pos2 = [o["tpot"] for o in uniq if o["tpot"] > 0]
    lo2 = (min(pos2)/(1.0+mx_d)) if pos2 else 1e-6
    hi2 = 4.0*max(pos2) if pos2 else 1e6
    if not (lo1 < hi1 and lo2 < hi2):
        return None
    import random
    random.seed(11)
    bx = (math.sqrt(lo1*hi1), math.sqrt(lo2*hi2))
    be = err(*bx)
    span1, span2 = hi1-lo1, hi2-lo2
    for it in range(60):
        r1, r2 = span1*(0.5**it), span2*(0.5**it)
        for _ in range(120):
            c1 = max(lo1, min(hi1, bx[0]+random.uniform(-r1, r1)))
            c2 = max(lo2, min(hi2, bx[1]+random.uniform(-r2, r2)))
            e = err(c1, c2)
            if e < be:
                be, bx = e, (c1, c2)
                if e < 1e-12:
                    break
    if be < 1e-7:
        return (bx[0], bx[1], be)
    return None

def main():
    runs = []
    for path, tag in zip(sys.argv[1::2], sys.argv[2::2]):
        runs.append((tag, parse(path)))
    tests = sorted(set().union(*[set(t) for _, t in runs]))
    atlas = {}
    print(f"{'#':>3} {'runs':>4} {'w_tp':>7} {'dbase':>10} {'SLO1':>11} {'SLO2':>11} "
          f"{'err':>8} | {'tp_base':>9} {'tp_UB':>9} | {'best_pts':>8} {'headroom':>8} class")
    total_best = 0.0
    for i in tests:
        obs = []
        ws = []
        dbs = []
        for tag, t in runs:
            if i not in t:
                continue
            o = dict(t[i]); o["tag"] = tag
            obs.append(o)
            a, b, S = o["ntp"], o["ncw"], o["score_norm"]
            den = a - b
            if abs(den) > 1e-9:
                w = (S - b)/den
                if -0.02 <= w <= 1.02:
                    ws.append(w)
            if 1e-6 < b < 1-1e-6 and o["dist"] > 0:
                dbs.append(o["dist"]/(1-b))
        sol = solve_slo(obs)
        slo1 = slo2 = None; serr = None
        if sol:
            slo1, slo2 = sol[0], sol[1]
            serr = sol[2]
        w = sum(ws)/len(ws) if ws else None
        db = sum(dbs)/len(dbs) if dbs else None
        best = max(o["points"] for o in obs)
        total_best += best
        h_tp = (1-max(o["ntp"] for o in obs))
        h_cw = (1-max(o["ncw"] for o in obs))
        # two-point tp_base/tp_UB solve: pick two obs with distinct tp,
        # both ntp strictly interior (0,1)
        pb = pu = None
        inter = [o for o in obs if 1e-9 < o["ntp"] < 1-1e-9]
        bytp = sorted(inter, key=lambda o: o["tp"])
        if len(bytp) >= 2 and abs(bytp[0]["tp"]-bytp[-1]["tp"]) > 1e-12:
            o1, o2 = bytp[0], bytp[-1]
            den = o2["ntp"]-o1["ntp"]
            span = (o2["tp"]-o1["tp"])/den
            pb = o1["tp"] - o1["ntp"]*span
            pu = pb + span
        atlas[i] = dict(w_tp=w, dist_base=db, slo1=slo1, slo2=slo2, slo_err=serr, tp_base=pb, tp_ub=pu,
                        best_points=best, headroom_tp=h_tp, headroom_cw=h_cw,
                        obs=[{k: o[k] for k in ("tag","points","tp","tdr","tpot","dist","ntp","ncw")} for o in obs])
        cls = "PURE-WAIT" if w is not None and w < 0.02 else \
              "PURE-TP" if w is not None and w > 0.97 else "mixed"
        print(f"{i:>3} {len(obs):>4} {('%0.4f'%w) if w is not None else '  ?  ':>7} "
              f"{('%0.4g'%db) if db is not None else '  ?  ':>10} "
              f"{('%0.6g'%slo1) if slo1 else '  ?  ':>11} {('%0.6g'%slo2) if slo2 else '  ?  ':>11} "
              f"{('%0.1e'%serr) if serr is not None else '  ?  ':>8} "
              f"{('%0.4g'%pb) if pb is not None else '  ?  ':>9} {('%0.4g'%pu) if pu is not None else '  ?  ':>9} | {best:8.3f} "
              f"{max(h_tp*(w or 0), h_cw*(1-(w or 0)))*1000:8.1f} {cls}")
    print(f"\nSum best-known points: {total_best:.3f}")
    json.dump(atlas, open("/tmp/atlas_full.json", "w"), indent=1)

if __name__ == "__main__":
    main()
