#!/usr/bin/env python3
"""D-068 marginal-value probe: run each probe case through the interactor with
--oracle and report the solver's [MARGINAL] attack/region/V values plus the
judge-side verification. The probe configs come from discriminator.TP_PARAMS;
t3/t9/t10 (CF-sheet actionable, not previously probed) are added here with
CF-sheet weights and shape-approximate SLO/dist_base (landing the run in the
waiting region per the sheet's C_w values)."""
import argparse
import math
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from discriminator import TP_PARAMS, Table, DEFAULT_ROWS, _link, _local, build_tp_case  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PROBE_PARAMS = dict(TP_PARAMS)
PROBE_PARAMS["t3"] = dict(_link(300, 260.0, 10, 0.0, 1.0), SLO1=600.0, dist_base=1.95)
PROBE_PARAMS["t9"] = dict(_link(300, 220.0, 10, 0.1, 0.9), SLO1=600.0, dist_base=3.48)
PROBE_PARAMS["t10"] = dict(_link(300, 240.0, 10, 0.1, 0.9), SLO1=600.0, dist_base=3.48)

ACTIONABLE = ["t3", "t9", "t10", "t13", "t14", "t16"]
ALL_PROBES = ["t3", "t5", "t6", "t9", "t10", "t11", "t12", "t13", "t14", "t16", "t19", "t22"]


def write_case(path, name, p):
    rows = p.get("rows", DEFAULT_ROWS)
    table = Table(rows)
    c = build_tp_case(name, p)
    with open(path, "w") as f:
        f.write("%d %.9f %.9f %.9f %d %d\n" % (
            c["K"], c["S"], c["lat"], c["bw"], c["bpt"], c["layers"]))
        f.write("%.9f %.9f %.9f %.9f %.9f %.9f %.9f\n" % (
            c["SLO1"], c["SLO2"], c["tp_UB"], c["tp_base"],
            c["dist_base"], c["w_tp"], c["w_c"]))
        f.write("%d\n" % len(table.rows))
        for r in table.rows:
            f.write(table.line(r) + "\n")
        f.write("%d\n" % c["R"])
        for arr in c["arrivals"]:
            f.write("%.9f %d %d\n" % (arr["t"], arr["lin"], arr["lout"]))


def judge_components(c, tdr, tpot):
    e_tdr = max(0.0, (tdr - c["SLO1"]) / c["SLO1"]) if c["SLO1"] > 0 else 0.0
    e_tpot = max(0.0, (tpot - c["SLO2"]) / c["SLO2"]) if c["SLO2"] > 0 else 0.0
    dist = math.sqrt(e_tdr * e_tdr + e_tpot * e_tpot)
    c_w = max(0.0, 1.0 - dist / c["dist_base"]) if c["dist_base"] > 0 else (1.0 if dist == 0 else 0.0)
    return c_w


def run_probe(name, p, solver, tmpdir, verbose):
    case_path = os.path.join(tmpdir, name + ".txt")
    write_case(case_path, name, p)
    r = subprocess.run(
        [sys.executable, "tools/interactor.py", "--case", case_path,
         "--solver", solver, "--oracle", "--timeout", "600"],
        cwd=ROOT, capture_output=True, text=True, timeout=900)
    out = r.stdout
    verdict = next((l for l in out.splitlines() if l.startswith("VERDICT")), "VERDICT missing")
    check = next((l for l in out.splitlines() if l.startswith("ORACLE CHECK")), "ORACLE CHECK missing")
    if verbose:
        print("  " + check)
    m = re.search(r"V_tp solver=(\S+) judge=(\S+) \|d\|=\S+ \| "
                  r"V_tdr solver=(\S+) judge=(\S+) \|d\|=\S+ \| "
                  r"V_tpot solver=(\S+) judge=(\S+) \|d\|=\S+ \| "
                  r"rem_tp solver=(\S+) judge=(\S+) \|d\|=\S+ \| "
                  r"rem_c solver=(\S+) judge=(\S+) \|d\|=\S+ \| "
                  r"attack solver=(\w+) judge=(\w+) \| region=(\S+) \| "
                  r"dead solver=(\w+) judge=(\w+)", check)
    if m is None:
        return (name, p, None, verdict, check)
    v_tp = float(m.group(1))
    v_tdr = float(m.group(3))
    v_tpot = float(m.group(5))
    rem_tp = float(m.group(7))
    rem_c = float(m.group(9))
    attack = m.group(11)
    region = m.group(13)
    dead = m.group(15)
    jm = re.search(r"tdr solver=[-0-9.eE+]+ judge=([-0-9.eE+]+) \|d\|=[-0-9.eE+]+ \| "
                   r"tpot solver=[-0-9.eE+]+ judge=([-0-9.eE+]+)", check)
    j_tdr = float(jm.group(1)) if jm else float("nan")
    j_tpot = float(jm.group(2)) if jm else float("nan")
    c_w = judge_components(p, j_tdr, j_tpot)
    marg_ok = "marg PASS" in check
    return (name, p, (attack, region, v_tp, v_tdr, v_tpot, rem_tp, rem_c, c_w,
                      marg_ok, dead), verdict, check)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--solver", default=os.path.join(ROOT, "build/submission"))
    ap.add_argument("--case", default=None, help="single probe name")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    names = [args.case] if args.case else ALL_PROBES
    with tempfile.TemporaryDirectory() as tmpdir:
        rows_out = []
        for name in names:
            if name not in PROBE_PARAMS:
                print("%-5s unknown probe (have: %s)" % (name, " ".join(sorted(PROBE_PARAMS))))
                continue
            res = run_probe(name, PROBE_PARAMS[name], args.solver, tmpdir, args.verbose)
            name, p, marg, verdict, check = res
            if marg is None:
                print("%-5s %-7s | no [MARGINAL] (%s) | %s" % (name, p.get("bottleneck", "?"), verdict, check))
                continue
            attack, region, v_tp, v_tdr, v_tpot, rem_tp, rem_c, c_w, marg_ok, dead = marg
            flag = "" if marg_ok else "  <-- marg FAIL"
            rows_out.append((name, p, attack, region, rem_tp, rem_c, dead, c_w))
            print("%-5s %-7s w=%.2f/%.2f region=%-14s attack=%-4s dead=%-1s c_w=%.3f | "
                  "V_tp=%.4g V_tdr=%.4g V_tpot=%.4g | rem_tp=%.4g rem_c=%.4g%s"
                  % (name, p.get("bottleneck", "?"), p["w_tp"], p["w_c"], region, attack,
                     dead, c_w, v_tp, v_tdr, v_tpot, rem_tp, rem_c, flag))
    print()
    print("dashboard sorted by recoverable points (rem_tp + rem_c):")
    for name, p, attack, region, rem_tp, rem_c, dead, c_w in sorted(
            rows_out, key=lambda r: -(r[4] + r[5])):
        print("  %-5s attack=%-4s region=%-14s rem_tp=%7.1f rem_c=%7.1f total=%7.1f %s"
              % (name, attack, region, rem_tp, rem_c, rem_tp + rem_c,
                 "DEAD" if dead == "1" else ""))


if __name__ == "__main__":
    main()