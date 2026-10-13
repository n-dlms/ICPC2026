#!/usr/bin/env python3
"""SA04 Exp D: per-case waiting-component operating point for build/submission."""
import subprocess, sys, os, math, json
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, "/tmp/sa04")
import mkcase

SOLVER = "/home/dlamini/ICPC2026/build/submission"
ROOT = "/home/dlamini/ICPC2026"


def case_params(path):
    toks = []
    for line in open(path):
        line = line.split("#")[0].strip()
        if line:
            toks.extend(line.split())
    i = 13
    N = int(toks[i]); i += 1 + 7 * N
    R = int(toks[i]); i += 1
    arrs = []
    for _ in range(R):
        arrs.append((float(toks[i]), int(toks[i+1]), int(toks[i+2]))); i += 3
    return dict(SLO1=float(toks[6]), SLO2=float(toks[7]), tp_UB=float(toks[8]),
                tp_base=float(toks[9]), dist_base=float(toks[10]),
                w_tp=float(toks[11]), w_c=float(toks[12]), R=R, arrs=arrs)


def one(case):
    name = os.path.basename(case).replace(".txt", "")
    grp = os.path.basename(os.path.dirname(case))
    trace = f"/tmp/sa04/D_{grp}_{name}.trace"
    cmd = ["python3", f"{ROOT}/tools/interactor.py", "--case", case,
           "--solver", SOLVER, "--trace-file", trace, "--timeout", "300"]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=330)
        out = p.stdout.strip().splitlines()
        verd = out[-1] if out else "FAIL"
        score = float(verd.split("score ")[1].split()[0]) if "score " in verd else 0.0
        ok = "VERDICT ok" in verd
        b = mkcase.breakdown(trace) if ok else None
    except Exception as ex:
        return dict(group=grp, name=name, error=str(ex))
    if b is None:
        return dict(group=grp, name=name, error=verd[:120])
    cp = case_params(case)
    b.update(group=grp, name=name, cf_score=score, **{k: cp[k] for k in
             ("SLO1","SLO2","tp_UB","tp_base","dist_base","w_tp","w_c")})
    os.remove(trace)
    return b


def classify(b):
    e1, e2, d, db = b["e_tdr"], b["e_tpot"], b["dist"], b["dist_base"]
    if db <= 0:
        return "binary-cliff"
    if d == 0:
        return "SLO-met (c_w=1)"
    if d >= db:
        return "saturated-BAD (c_w=0)"
    return "linear"


def marginals(b):
    """dScore/dx at the operating point (0 outside linear regions)."""
    c = b
    v_tp = 1000*c["w_tp"]/(c["tp_UB"]-c["tp_base"]) if c["tp_base"] < c["tp"] < c["tp_UB"] and c["tp_UB"] > c["tp_base"] else 0.0
    v1 = v2 = 0.0
    if c["w_c"] > 0 and c["dist_base"] > 0 and 0 < c["dist"] < c["dist_base"]:
        if c["e_tdr"] > 0:
            v1 = 1000*c["w_c"]*c["e_tdr"]/(c["dist_base"]*c["dist"]*c["SLO1"])
        if c["e_tpot"] > 0:
            v2 = 1000*c["w_c"]*c["e_tpot"]/(c["dist_base"]*c["dist"]*c["SLO2"])
    # dScore/d(elapsed ms): tp = T/elapsed -> dtp/delapsed = -tp/elapsed
    vel = -v_tp * b["tp"]/b["elapsed"] if b["elapsed"] > 0 else 0.0
    return v_tp, v1, v2, vel


if __name__ == "__main__":
    cases = []
    for d in ("cases/zoo", "cases/mimics", "cases/adv"):
        p = os.path.join(ROOT, d)
        cases += sorted(os.path.join(p, f) for f in os.listdir(p) if f.endswith(".txt"))
    with ThreadPoolExecutor(max_workers=8) as ex:
        res = list(ex.map(one, cases))
    rows = []
    for b in res:
        if "error" in b:
            rows.append(b); continue
        v_tp, v1, v2, vel = marginals(b)
        rows.append(dict(group=b["group"], name=b["name"], R=b["R"], score=b["score"],
                         SLO1=b["SLO1"], tdr=b["mean_tdr"], e_tdr=b["e_tdr"],
                         SLO2=b["SLO2"], tpot=b["mean_tpot"], e_tpot=b["e_tpot"],
                         dist=b["dist"], dist_base=b["dist_base"], c_w=b["c_w"],
                         w_tp=b["w_tp"], w_c=b["w_c"], tp=b["tp"], tp_base=b["tp_base"],
                         tp_UB=b["tp_UB"], c_tp=b["c_tp"], elapsed=b["elapsed"],
                         gaps=b["gaps"], regime=classify(b),
                         V_tp=v_tp, V_tdr=v1, V_tpot=v2, V_elapsed=vel))
    with open("/tmp/sa04/expD.json", "w") as f:
        json.dump(rows, f, indent=1)
    hdr = f"{'case':26s} {'R':>4} {'tdr':>9} {'SLO1':>8} {'e_tdr':>7} {'tpot':>8} {'SLO2':>8} {'e_tpot':>7} {'dist':>8} {'dbase':>7} {'c_w':>6} {'w_c':>4} {'c_tp':>6} {'score':>8}  regime"
    print(hdr)
    for r in rows:
        if "error" in r:
            print(f"{r['group']}/{r['name']:26s} ERROR {r['error'][:60]}"); continue
        print(f"{r['group'][:3]}/{r['name']:22s} {r['R']:4d} {r['tdr']:9.3f} {r['SLO1']:8.2f} "
              f"{r['e_tdr']:7.3f} {r['tpot']:8.3f} {r['SLO2']:8.2f} {r['e_tpot']:7.3f} "
              f"{r['dist']:8.3f} {r['dist_base']:7.2f} {r['c_w']:6.3f} {r['w_c']:4.2f} "
              f"{r['c_tp']:6.3f} {r['score']:8.2f}  {r['regime']}")
