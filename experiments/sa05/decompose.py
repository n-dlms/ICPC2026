#!/usr/bin/env python3
"""SA05 loss decomposition: run the sealed D-148 leader on mimic cases,
extract component values (c_tp, c_w) from the interactor trace, and
attribute missing-from-1000 points to (a) tp component, (b) waiting component.
Read-only wrt the repo; writes traces to /tmp/sa05/."""
import math, os, subprocess, sys

REPO = "/home/dlamini/ICPC2026"
SOLVER = "/tmp/sa05/sub_d148"
OUT = "/tmp/sa05"

CASES = [
    ("cases/mimics/cf9_shape.txt", "#9"),
    ("cases/mimics/cf10_shape.txt", "#10"),
    ("cases/mimics/cf14_shape.txt", "#14"),
    ("cases/zoo/cf17_shape.txt", "#17"),
    ("cases/zoo/cf19_prefillsteep.txt", "#19"),
    ("cases/mimics/m3.txt", "#3-like"),
    ("cases/mimics/m3b.txt", "#3-like-b"),
    ("cases/mimics/m5_order.txt", "#5-order"),
    ("cases/mimics/m6.txt", "#6-fast-starve"),
    ("cases/mimics/m6b.txt", "#6-slow-starve"),
    ("cases/mimics/m9.txt", "#9-TDR"),
    ("cases/mimics/m9b.txt", "#10-TDR"),
    ("cases/mimics/m9c.txt", "#18/21-TDR"),
    ("cases/mimics/m15.txt", "#15-Lout1-fatlink"),
    ("cases/mimics/m17.txt", "#17-a"),
    ("cases/mimics/m17b.txt", "#17-b"),
    ("cases/mimics/m17c.txt", "#17-c-fatlink"),
    ("cases/mimics/m17d.txt", "#17-d"),
    ("cases/mimics/m19.txt", "#19-a"),
    ("cases/mimics/m19b.txt", "#19-b"),
    ("cases/mimics/m19c.txt", "#19-c"),
    ("cases/mimics/m_lat.txt", "latency"),
]


def parse_tdn(line):
    rest = line.split("TDN ", 1)[1]
    specstr, _ = rest.rsplit(" dur=", 1)
    st = specstr.split()
    if st[1] == "P" and st[2] == "POST":
        return ("P_POST", int(st[4]))
    if st[1] == "D" and st[2] == "POST":
        return ("D_POST", [int(x) for x in st[5:]])
    return (None, None)


def analyze(trace):
    cfg, arr, lout, ppost, first_t, last_t, fin_t = {}, {}, {}, {}, {}, {}, {}
    for line in open(trace):
        line = line.rstrip("\n")
        if not line:
            continue
        if line.startswith("CONFIG"):
            t = line.split()
            for k, i in (("SLO1", 7), ("SLO2", 8), ("tp_UB", 9), ("tp_base", 10),
                         ("dist_base", 11), ("w_tp", 12), ("w_c", 13)):
                cfg[k] = float(t[i])
            continue
        if not line.startswith("EVENT"):
            continue
        p = line.split()
        t = float(p[1].split("=")[1])
        k = p[2]
        if k == "ARR":
            arr[int(p[3])] = t; lout[int(p[3])] = int(p[5])
        elif k == "TDN":
            sk, v = parse_tdn(line)
            if sk == "P_POST":
                ppost[v] = t
            elif sk == "D_POST":
                for rid in v:
                    first_t[rid] = min(first_t.get(rid, 1e18), t)
                    last_t[rid] = max(last_t.get(rid, -1e18), t)
        elif k == "FIN":
            fin_t[int(p[3])] = t
    R = len(arr)
    mean_tdr = sum(ppost.get(r, 0.0) - arr[r] for r in arr) / R if R else 0.0
    total_gaps = sum(lout[r] - 1 for r in lout)
    sum_span = sum((last_t.get(r, first_t.get(r, 0.0)) - first_t[r])
                   for r in lout if r in first_t)
    mean_tpot = sum_span / total_gaps if total_gaps > 0 else 0.0
    tok = sum(lout.values())
    fa = min(arr.values()) if arr else 0.0
    lf = max(fin_t.values()) if fin_t else 0.0
    tp = tok / (lf - fa) if lf > fa else 0.0
    c_tp = max(0.0, min(1.0, (tp - cfg["tp_base"]) / (cfg["tp_UB"] - cfg["tp_base"]))) \
        if cfg["tp_UB"] > cfg["tp_base"] else 0.0
    e_tdr = max(0.0, (mean_tdr - cfg["SLO1"]) / cfg["SLO1"])
    e_tpot = max(0.0, (mean_tpot - cfg["SLO2"]) / cfg["SLO2"])
    dist = math.hypot(e_tdr, e_tpot)
    if cfg["dist_base"] > 0.0:
        c_w = max(0.0, 1.0 - dist / cfg["dist_base"])
    else:
        c_w = 1.0 if dist == 0.0 else 0.0
    score = 1000.0 * (cfg["w_tp"] * c_tp + cfg["w_c"] * c_w)
    return dict(tp=tp, c_tp=c_tp, tdr=mean_tdr, tpot=mean_tpot, e_tdr=e_tdr,
                e_tpot=e_tpot, dist=dist, c_w=c_w, score=score,
                miss_tp=1000 * cfg["w_tp"] * (1 - c_tp),
                miss_w=1000 * cfg["w_c"] * (1 - c_w))


def main():
    print(f"{'case':24s} {'tag':16s} {'score':>8s} {'tp':>8s} {'c_tp':>6s} "
          f"{'tdr':>10s} {'tpot':>9s} {'e_tdr':>9s} {'e_tpot':>8s} {'dist':>9s} "
          f"{'c_w':>6s} {'miss_tp':>8s} {'miss_w':>8s}  verdict")
    for rel, tag in CASES:
        case = os.path.join(REPO, rel)
        name = os.path.basename(rel).replace(".txt", "")
        tr = os.path.join(OUT, f"tr_{name}.txt")
        r = subprocess.run(
            ["python3", f"{REPO}/tools/interactor.py", "--case", case,
             "--solver", SOLVER, "--trace-file", tr, "--timeout", "600"],
            capture_output=True, text=True, timeout=900)
        out = r.stdout.strip().splitlines()
        verdict = out[-1] if out else "NO-OUTPUT"
        vshort = "ok" if "VERDICT ok" in verdict else verdict[:40]
        try:
            d = analyze(tr)
            print(f"{name:24s} {tag:16s} {d['score']:8.2f} {d['tp']:8.4f} "
                  f"{d['c_tp']:6.3f} {d['tdr']:10.1f} {d['tpot']:9.2f} "
                  f"{d['e_tdr']:9.3f} {d['e_tpot']:8.3f} {d['dist']:9.3f} "
                  f"{d['c_w']:6.3f} {d['miss_tp']:8.1f} {d['miss_w']:8.1f}  {vshort}")
        except Exception as e:
            print(f"{name:24s} {tag:16s} ANALYZE-FAIL {e}  {vshort}")


if __name__ == "__main__":
    main()
