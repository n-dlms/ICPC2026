#!/usr/bin/env python3
"""SA06 admission experiment runner: policies x cases, extract E/tdr/tpot/score
from the trace + judge verdict line."""
import os
import re
import subprocess
import sys

E = "/home/dlamini/ICPC2026/experiments/sa06"
I = "/home/dlamini/ICPC2026/tools/interactor.py"
SOLVER = f"{E}/probe_solver.py"

POLICIES = [
    ("imm", "--decode-batch"),
    ("holdppre25", "--decode-batch --hold-ppre=rid=0,ms=25"),
    ("holdppre50", "--decode-batch --hold-ppre=rid=0,ms=50"),
    ("holdppre75", "--decode-batch --hold-ppre=rid=0,ms=75"),
    ("park1_50", "--decode-batch --hold-dpre1=rid=0,ms=50"),
]


def parse_trace(path):
    arr, fins, ppost, tok = {}, {}, {}, {}
    with open(path) as f:
        for line in f:
            m = re.match(r"EVENT t=([\d.]+) (\w+) (.*)", line.strip())
            if not m:
                continue
            t = float(m.group(1))
            ev = (m.group(2) + " " + m.group(3)).split()
            if ev[0] == "ARR":
                arr[int(ev[1])] = (t, int(ev[2]), int(ev[3]))
            elif ev[0] == "FIN":
                fins[int(ev[1])] = t
            elif ev[0] == "TDN" and ev[2] == "P" and ev[3] == "POST":
                ppost[int(ev[5])] = t
            elif ev[0] == "TDN" and ev[2] == "D" and ev[3] == "POST":
                for rid in [int(x) for x in ev[6:-1]]:
                    tok.setdefault(rid, []).append(t)
    R = len(arr)
    lout = {r: arr[r][2] for r in arr}
    E_l = max(fins.values()) - min(a[0] for a in arr.values())
    tdr = sum(ppost[r] - arr[r][0] for r in arr) / R
    gaps = sum(tok[r][-1] - tok[r][0] for r in arr if lout[r] > 1)
    n_g = sum(lout[r] - 1 for r in arr if lout[r] > 1)
    tpot = gaps / n_g if n_g else 0.0
    return dict(R=R, E=E_l, tdr=tdr, tpot=tpot, last_rid=max(fins, key=fins.get))


def main():
    cases = sys.argv[1:]
    for c in cases:
        print(f"##### {c}")
        for nm, a in POLICIES:
            tr = f"{E}/tr2_{c}_{nm}.txt"
            env = dict(os.environ, SA06_ARGS=a)
            r = subprocess.run(
                ["python3", I, "--case", f"{E}/cases/{c}", "--solver", SOLVER,
                 "--trace-file", tr],
                capture_output=True, text=True, env=env, timeout=120)
            last = r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr.strip().splitlines()[-1]
            vm = re.search(r"score ([\d.]+)", last)
            vdm = re.search(r"VERDICT (\S+)", last)
            score = float(vm.group(1)) if vm else -1
            verd = vdm.group(1) if vdm else "?"
            try:
                st = parse_trace(tr)
                print(f"  {nm:14s} {verd:6s} score={score:9.3f} E={st['E']:9.3f} "
                      f"tdr={st['tdr']:8.3f} tpot={st['tpot']:7.3f} last=rid{st['last_rid']}")
            except Exception as ex:
                print(f"  {nm:14s} {verd:6s} score={score:9.3f} (no trace: {ex})")


if __name__ == "__main__":
    main()
