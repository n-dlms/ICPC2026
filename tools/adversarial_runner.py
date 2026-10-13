#!/usr/bin/env python3
"""Adversarial gauntlet runner for the ICPC 2026 solver.

Runs the solver on each adversarial case, then enforces the Green Light Protocol:
  - zero RE (runtime error) / WA (violation) / TLE
  - score > 0 (no catastrophic failure)
  - axis-specific property assertions.

For the Superlinear axis it builds a GreedyAllBatch baseline and verifies the
solver (MarginalBatch) dominates it.

Usage:
    python3 tools/adversarial_generator.py --axis all
    python3 tools/adversarial_runner.py --solver build/submission --axis all
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INTERACTOR = os.path.join(ROOT, "tools", "interactor.py")

CASES = {
    "transfer": "adv_transfer.txt",
    "micro": "adv_micro.txt",
    "super": "adv_super.txt",
    "degenerate": "adv_degenerate.txt",
    "cliff": "adv_cliff.txt",
}


def run(case, solver, trace=None, timeout=600):
    cmd = [sys.executable, INTERACTOR, "--case", case, "--solver", solver]
    if trace:
        cmd += ["--trace-file", trace]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, cwd=ROOT)
    except subprocess.TimeoutExpired:
        return {"verdict": "TLE", "score": 0.0, "runtime": timeout, "frames": 0,
                "stderr": "", "raw": ""}
    out = p.stdout
    m = re.search(r"VERDICT (\S+?) \| score ([0-9.]+) \| frames (\d+) \| runtime ([0-9.]+)s", out)
    if not m:
        return {"verdict": "PARSE_ERR", "score": 0.0, "runtime": 0.0, "frames": 0,
                "stderr": p.stderr, "raw": out[-800:]}
    return {
        "verdict": m.group(1),
        "score": float(m.group(2)),
        "frames": int(m.group(3)),
        "runtime": float(m.group(4)),
        "stderr": p.stderr,
        "raw": out,
    }


def id_cloud(trace):
    # DECISION lines: "DECISION t=.. ASSIGN E P PRE <cloud> <id>"  (tokens split: P, PRE)
    #   toks[i]="PRE", toks[i-1]="P", toks[i+1]=cloud, toks[i+2]=id
    mp = {}
    with open(trace) as f:
        for line in f:
            if not line.startswith("DECISION"):
                continue
            toks = line.split()
            if "PRE" in toks:
                i = toks.index("PRE")
                if i >= 2 and toks[i - 1] == "P":  # P PRE
                    try:
                        mp[int(toks[i + 2])] = int(toks[i + 1])
                    except (ValueError, IndexError):
                        pass
    return mp


def dpre_stats(trace, mp):
    # "DECISION t=.. ASSIGN E D PRE -1 <m> <id1> <id2> ..."  (tokens split: D, PRE)
    #   toks[i]="PRE", toks[i-1]="D", toks[i+1]="-1", toks[i+2]=m, toks[i+3:]=ids
    n = 0
    max_span = 0
    max_size = 0
    viol = 0
    with open(trace) as f:
        for line in f:
            if not line.startswith("DECISION"):
                continue
            toks = line.split()
            if "PRE" in toks:
                i = toks.index("PRE")
                if i >= 2 and toks[i - 1] == "D":  # D PRE
                    try:
                        ids = [int(x) for x in toks[i + 3:]]
                    except ValueError:
                        ids = []
                    if not ids:
                        continue
                    n += 1
                    max_size = max(max_size, len(ids))
                    clouds = set(mp.get(x, -1) for x in ids)
                    max_span = max(max_span, len(clouds))
                    if len(clouds) > 3:
                        viol += 1
    return {"n": n, "max_span": max_span, "max_size": max_size, "viol": viol}


def config_K(trace):
    with open(trace) as f:
        for line in f:
            if line.startswith("CONFIG"):
                return int(line.split()[1])
    return -1


def build_greedy():
    src = open(os.path.join(ROOT, "src", "Solution.cpp")).read()
    patched = src.replace(
        "MarginalBatch marginal;", "MarginalBatch marginal;\n        GreedyAllBatch greedy;")
    patched = patched.replace("policies.batch = &marginal;", "policies.batch = &greedy;")
    patched = patched.replace("policies.batch = &size_one;", "policies.batch = &greedy;")
    tmp = os.path.join(ROOT, "build", "_greedy_Solution.cpp")
    open(tmp, "w").write(patched)
    out = "/tmp/solver_greedy"
    r = subprocess.run(
        ["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
         "-I", os.path.join(ROOT, "src"),
         "-o", out, tmp,
         os.path.join(ROOT, "src", "protocol.cpp"),
         os.path.join(ROOT, "src", "task_table.cpp"),
         os.path.join(ROOT, "src", "state.cpp"),
         os.path.join(ROOT, "src", "scheduler.cpp"),
         os.path.join(ROOT, "src", "policies.cpp")],
        capture_output=True, text=True)
    if r.returncode != 0:
        print("GREEDY BUILD FAILED:\n", r.stderr[-1200:])
        sys.exit(1)
    return out


def main():
    ap = argparse.ArgumentParser(description="Adversarial gauntlet runner.")
    ap.add_argument("--solver", default=os.path.join(ROOT, "build", "submission"))
    ap.add_argument("--cases", default=os.path.join(ROOT, "cases", "adv"))
    ap.add_argument("--axis", default="all")
    args = ap.parse_args()

    axes = CASES if args.axis == "all" else {args.axis: CASES[args.axis]}
    greedy = build_greedy() if "super" in axes else None

    results = []
    for name, fn in axes.items():
        path = os.path.join(args.cases, fn)
        if not os.path.exists(path):
            print(f"[skip] {name}: {path} missing (run generator first)")
            continue
        trace = os.path.join("/tmp", f"adv_{name}.trace") if name in ("transfer", "cliff") else None
        r = run(path, args.solver, trace)

        problems = []
        rec = {"axis": name, "verdict": r["verdict"], "score": r["score"], "runtime": r["runtime"]}

        if r["verdict"] != "ok":
            problems.append(f"verdict={r['verdict']} (RE/WA/TLE)")
        else:
            # transfer-saturation is impossible-by-design (8s transfers vs SLO~50ms):
            # score~0 is expected. The valuable signal there is no crash/WA/TLE and
            # the cross-cloud D PRE cap being respected (checked below), not a score.
            if name != "transfer" and r["score"] <= 0:
                problems.append("score<=0 (catastrophic)")
            if name == "micro":
                if r["runtime"] > 30:
                    problems.append(f"slow runtime {r['runtime']:.1f}s")
            if name == "transfer" and trace:
                mp = id_cloud(trace)
                st = dpre_stats(trace, mp)
                rec.update(st)
                if st["viol"] > 0:
                    problems.append(f"{st['viol']} D PRE groups span >3 clouds")
            if name == "cliff":
                # The regime line is emitted to solver stderr, which the interactor
                # does not forward. Detect THROUGHPUT behaviorally: it uses an
                # unbounded cross-cloud D PRE cap (spans all K clouds) and max
                # batching. BALANCED would cap D PRE at 3 clouds.
                K = config_K(trace) if trace else -1
                rec["K"] = K
                if trace:
                    mp = id_cloud(trace)
                    st = dpre_stats(trace, mp)
                    rec.update(st)
                    if K > 0 and st["max_span"] < K:
                        problems.append(
                            f"D PRE did not go max-throughput (max cloud span "
                            f"{st['max_span']} < K={K})")
                    if st["max_size"] < max(2, K // 2):
                        problems.append(f"D PRE not max-batching (max group {st['max_size']})")
            if name == "super" and greedy:
                rg = run(path, greedy)
                rec["greedy_score"] = rg["score"]
                # The trap is verified if the solver's MarginalBatch clearly beats
                # GreedyAllBatch. D_PROC is provably stopped at m=1 (optimal for a
                # convex decode_proc), so the solver must dominate the greedy baseline
                # by a comfortable margin regardless of the D_PRE/D_POST combine cost.
                if r["score"] < rg["score"] * 1.5:
                    problems.append(
                        f"solver not clearly better than greedy "
                        f"(solver={r['score']:.3f} greedy={rg['score']:.3f})")

        rec["problems"] = problems
        results.append(rec)
        status = "PASS" if not problems else "FAIL"
        extra = ""
        if "max_span" in rec:
            extra += f" dp_groups={rec['n']} max_cloud_span={rec['max_span']} max_dpre={rec['max_size']}"
        if "regime" in rec:
            extra += f" regime={rec['regime']}"
        if "greedy_score" in rec:
            extra += f" greedy={rec['greedy_score']:.3f}"
        print(f"[{status}] {name}: verdict={r['verdict']} score={r['score']:.3f} "
              f"rt={r['runtime']:.2f}s{extra}"
              + ("" if not problems else "  :: " + "; ".join(problems)))

    npass = sum(1 for r in results if not r["problems"])
    print(f"\n=== ADVERSARIAL GAUNTLET: {npass}/{len(results)} PASS ===")
    sys.exit(0 if npass == len(results) else 1)


if __name__ == "__main__":
    main()
