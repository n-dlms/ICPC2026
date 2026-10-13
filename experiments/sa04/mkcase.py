#!/usr/bin/env python3
"""SA04 crafted micro-case generator + run harness."""
import subprocess, sys, math, os

BASE = "/tmp/sa04"

# ---- micro template: K=2 S=0 lat=1 bw=1e9 bpt=1 layers=1 ----
# table flat: batch 1: PRE=1.0, PROC=0.001, POST=0.001, DPRE=0.001, DPROC=0.001, DPOST=0.001
# decode round ~= 2.003 ms, prefill ~= 3.002 ms


def write_case(name, arrivals, slo1, slo2, tp_ub, tp_base, dist_base, w_tp,
               empties_step=0.5, horizon=None, K=2, durs=0.001):
    if horizon is None:
        horizon = 60 + 60 * len(arrivals)
    lines = []
    lines.append(f"{K} 0.0 1.0 1e9 1 1")
    lines.append(f"{slo1} {slo2} {tp_ub} {tp_base} {dist_base} {w_tp} {1.0-w_tp}")
    lines.append("2")
    lines.append(f"1 1.0 {durs} {durs} {durs} {durs} {durs}")
    lines.append(f"4 1.0 {durs} {durs} {durs} {durs} {durs}")
    lines.append(str(len(arrivals)))
    for (t, lin, lout) in arrivals:
        lines.append(f"{t} {lin} {lout}")
    t = 0.0
    while t <= horizon:
        lines.append(f"EMPTY {t}")
        t += empties_step
    path = os.path.join(BASE, name + ".txt")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    return path


def run(name, extra_solver_args=(), case=None, solver=os.path.join(BASE, "mysolver.py")):
    case = case or os.path.join(BASE, name + ".txt")
    trace = os.path.join(BASE, name + ".trace")
    env = dict(os.environ)
    env["SA04_ARGS"] = " ".join(extra_solver_args)
    cmd = ["python3", "/home/dlamini/ICPC2026/tools/interactor.py",
           "--case", case, "--solver", solver, "--trace-file", trace]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=120, env=env)
    out = p.stdout.strip()
    verdict = out.splitlines()[-1] if out else "NO OUTPUT: " + p.stderr[-500:]
    return verdict, trace


def breakdown(trace):
    """Compute full component breakdown from a PART 9 trace."""
    sys.path.insert(0, "/tmp/sa04")
    cfg, arr, lout, ppost, first_t, last_t, fin_t = {}, {}, {}, {}, {}, {}, {}
    for line in open(trace):
        line = line.rstrip("\n")
        if line.startswith("CONFIG"):
            tk = line.split()
            cfg = dict(K=int(tk[1]), S=float(tk[2]), SLO1=float(tk[7]),
                       SLO2=float(tk[8]), tp_UB=float(tk[9]), tp_base=float(tk[10]),
                       dist_base=float(tk[11]), w_tp=float(tk[12]), w_c=float(tk[13]))
        elif not line.startswith("EVENT"):
            continue
        else:
            parts = line.split()
            t = float(parts[1].split("=")[1])
            kind = parts[2]
            if kind == "ARR":
                arr[int(parts[3])] = t
                lout[int(parts[3])] = int(parts[5])
            elif kind == "TDN":
                rest = line.split("TDN ", 1)[1]
                specstr, _ = rest.rsplit(" dur=", 1)
                st = specstr.split()
                if st[1] == "P" and st[2] == "POST":
                    ppost[int(st[4])] = t
                elif st[1] == "D" and st[2] == "POST":
                    for rid in [int(x) for x in st[5:]]:
                        if rid not in first_t or t < first_t[rid]:
                            first_t[rid] = t
                        if rid not in last_t or t > last_t[rid]:
                            last_t[rid] = t
            elif kind == "FIN":
                fin_t[int(parts[3])] = t
    R = len(arr)
    mean_tdr = sum(ppost.get(r, 0.0) - arr[r] for r in arr) / R
    gaps = sum(lout[r] - 1 for r in lout)
    span = sum(last_t.get(r, first_t.get(r, 0.0)) - first_t[r] for r in first_t)
    mean_tpot = span / gaps if gaps else 0.0
    tok = sum(lout.values())
    elapsed = (max(fin_t.values()) if fin_t else 0.0) - min(arr.values())
    tp = tok / elapsed if elapsed > 0 else 0.0
    c_tp = max(0.0, min(1.0, (tp - cfg["tp_base"]) / (cfg["tp_UB"] - cfg["tp_base"]))) \
        if cfg["tp_UB"] > cfg["tp_base"] else 0.0
    e_tdr = max(0.0, (mean_tdr - cfg["SLO1"]) / cfg["SLO1"])
    e_tpot = max(0.0, (mean_tpot - cfg["SLO2"]) / cfg["SLO2"])
    dist = math.hypot(e_tdr, e_tpot)
    c_w = max(0.0, 1.0 - dist / cfg["dist_base"]) if cfg["dist_base"] > 0 else \
        (1.0 if dist == 0 else 0.0)
    score = 1000.0 * (cfg["w_tp"] * c_tp + cfg["w_c"] * c_w)
    per_req_tdr = {r: ppost.get(r, 0.0) - arr[r] for r in sorted(arr)}
    per_req_span = {r: (last_t.get(r, first_t.get(r, 0.0)) - first_t[r]) if r in first_t else None
                    for r in sorted(arr)}
    first_tok = {r: first_t.get(r) for r in sorted(arr)}
    last_tok = {r: last_t.get(r) for r in sorted(arr)}
    return dict(R=R, mean_tdr=mean_tdr, mean_tpot=mean_tpot, gaps=gaps, tp=tp,
                elapsed=elapsed, c_tp=c_tp, e_tdr=e_tdr, e_tpot=e_tpot, dist=dist,
                c_w=c_w, score=score, per_req_tdr=per_req_tdr,
                per_req_span=per_req_span, first_tok=first_tok, last_tok=last_tok,
                ppost=ppost, cfg=cfg)


def show(tag, b):
    print(f"--- {tag}")
    print(f"  mean_tdr={b['mean_tdr']:.6f}  mean_tpot={b['mean_tpot']:.6f}  tp={b['tp']:.6f}")
    print(f"  e_tdr={b['e_tdr']:.6f}  e_tpot={b['e_tpot']:.6f}  dist={b['dist']:.6f}")
    print(f"  c_tp={b['c_tp']:.6f}  c_w={b['c_w']:.6f}  SCORE={b['score']:.4f}")
    print(f"  first={ {k: round(v,3) for k,v in b['first_tok'].items()} }")
    print(f"  last ={ {k: round(v,3) for k,v in b['last_tok'].items()} }")
    print(f"  tdr  ={ {k: round(v,3) for k,v in b['per_req_tdr'].items()} }")
    print(f"  span ={ {k: (round(v,3) if v is not None else None) for k,v in b['per_req_span'].items()} }")


if __name__ == "__main__":
    print("module")
