#!/usr/bin/env python3
"""tools/ab_test.py — score-gated A/B between two solver binaries (new protocol).

Runs --baseline and --candidate on the same case set (exact same frame stream,
since interactor.py is deterministic), gates BOTH to VERDICT ok, and compares
score per case. Candidate is promoted only if it does not regress.

Per-case verdict is authoritative (interactor.py). Score comparisons are
secondary: a candidate that crashes or violates is a hard fail regardless of
score. Promote gate (all must hold):
  - no case where candidate VERDICT != ok while baseline VERDICT == ok
    (candidate must not break correctness)
  - no case in --hard where score regressed at all (--delta 0)
  - aggregate score delta sum >= --min-net (default -0.0, i.e. not worse)

Output: per-case table, then WIN/LOSS/TIE summary, then PROMOTE/REJECT.

Usage:
  python3 tools/ab_test.py --baseline build/frozen_v1_1 --candidate build/solution \
      cases/zoo/*.txt cases/adv/*.txt
  python3 tools/ab_test.py --baseline B --candidate C --hard "burst_100,mixed" \
      --generate 200 --gen-seed 1 --delta 0.1 cases/zoo/*.txt
"""

import argparse
import concurrent.futures as cf
import glob
import json
import random
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent


def run_case(solver, case_path, timeout):
    p = subprocess.run(
        [sys.executable, str(HERE / "interactor.py"),
         "--case", str(case_path), "--solver", str(solver),
         "--timeout", str(timeout)],
        capture_output=True, text=True)
    out = (p.stdout or p.stderr or "").strip().splitlines()
    last = out[-1] if out else ""
    return last, p.returncode


def parse_verdict(line):
    if not line:
        return ("re", 0.0)
    # "VERDICT ok | score 500.000 | frames 10 | runtime 0.01s"
    # "VERDICT violation(...) | score 0.000 | ..."
    ok = line.startswith("VERDICT ok")
    score = 0.0
    try:
        score = float(line.split("score")[1].split("|")[0].strip())
    except Exception:
        pass
    verdict = "ok" if ok else line.split("|")[0].replace("VERDICT", "").strip() or "re"
    return verdict, score


def gen_random_case(path, rng):
    # small random cases for adversarial A/B (mirrors crash_sweep generator shape)
    K = rng.randint(1, 8)
    S = rng.choice([0.0, 0.5, 1.0, 2.0, 10.0])
    lat = rng.choice([0.1, 1.0, 10.0])
    bw = rng.choice([0.5, 1.0, 50.0, 100.0])
    bpt = rng.choice([1, 100, 1000])
    layers = rng.choice([1, 5, 16, 64])
    batches = sorted(set(rng.sample(range(1, 65), min(6, 4))))
    if len(batches) < 2:
        batches = [1, 8]
    rows = []
    for b in batches:
        rows.append([b, 2.0, 12.0, 2.0, 1.5, 8.0 + 0.5 * (b.bit_length() - 1), 1.5])
    R = rng.randint(1, 60)
    arrs = []
    t = 0.0
    for i in range(R):
        if rng.random() < 0.3:
            t += round(rng.uniform(0, 3), 3)
        arrs.append((round(t, 3), rng.randint(1, 128), rng.randint(1, 30)))
    scoring = [500, 100, 1000, 0, 0.1, 0.5, 0.5]
    lines = [f"{K} {S} {lat} {bw} {bpt} {layers}",
             " ".join(str(x) for x in scoring), str(len(rows))]
    for r in rows:
        lines.append(" ".join(str(x) for x in r))
    lines.append(str(R))
    for tt, ll, oo in arrs:
        lines.append(f"{tt} {ll} {oo}")
    Path(path).write_text("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--candidate", required=True)
    ap.add_argument("--timeout", type=float, default=30.0)
    ap.add_argument("-j", "--jobs", type=int, default=4)
    ap.add_argument("--delta", type=float, default=0.0,
                    help="allowed per-case score regression (candidate - baseline)")
    ap.add_argument("--min-net", type=float, default=-0.001,
                    help="minimum net (sum cand - sum base); reject if below")
    ap.add_argument("--hard", default="",
                    help="case names (stem) with zero tolerance, comma-sep")
    ap.add_argument("--generate", type=int, default=0,
                    help="also generate N random cases under /tmp")
    ap.add_argument("--gen-seed", type=int, default=1)
    ap.add_argument("--json", default=None)
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("cases", nargs="+")
    args = ap.parse_args()

    hard = set(s for s in args.hard.split(",") if s)
    cases = []
    for pat in args.cases:
        cases.extend(sorted(glob.glob(pat)))
    cases = sorted(set(cases))

    if args.generate > 0:
        rng = random.Random(args.gen_seed)
        for i in range(args.generate):
            p = f"/tmp/ab_rand_{args.gen_seed}_{i}.txt"
            gen_random_case(p, rng)
            cases.append(p)

    if not cases:
        print("no cases")
        return 2

    def work(case_path):
        b_line, b_rc = run_case(args.baseline, case_path, args.timeout)
        c_line, c_rc = run_case(args.candidate, case_path, args.timeout)
        return case_path, b_line, c_line, b_rc, c_rc

    start = time.monotonic()
    rows = {}
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        futs = {ex.submit(work, c): c for c in cases}
        for fut in cf.as_completed(futs):
            cp, bl, cl, brc, crc = fut.result()
            rows[Path(cp).name] = (bl, cl, brc, crc)

    wins = losses = ties = broken = regress = 0
    sum_base = sum_cand = 0.0
    fail_reasons = []
    for name in sorted(rows):
        bl, cl, brc, crc = rows[name]
        bv, bs = parse_verdict(bl)
        cv, cs = parse_verdict(cl)
        sum_base += bs
        sum_cand += cs
        is_hard = name.rsplit(".", 1)[0] in hard
        tag = ""
        # correctness gate: candidate must not break a passing baseline case
        if bv == "ok" and cv != "ok":
            broken += 1
            tag = "BROKEN"
            fail_reasons.append(f"{name}: candidate {cv} (baseline ok)")
        elif bv != "ok" and cv != "ok":
            tag = f"both-{bv}"
        else:
            d = cs - bs
            tol = 0.0 if is_hard else args.delta
            if d > 1e-6:
                wins += 1
                tag = "WIN"
            elif d < -1e-6:
                if (not is_hard and d < -tol - 1e-9) or (is_hard and d < -1e-9):
                    regress += 1
                    tag = "REGRESS"
                    fail_reasons.append(f"{name}: regress {d:+.3f}" +
                                         (" [HARD]" if is_hard else ""))
                else:
                    losses += 1
                    tag = "loss(within-tol)"
            else:
                ties += 1
                tag = "tie"
        if not args.quiet:
            print(f"  {name:<34} base {bs:9.3f} [{bv:>12}]  cand {cs:9.3f} [{cv:>12}]  {tag}")

    net = sum_cand - sum_base
    print(f"\nA/B summary: wins {wins}  ties {ties}  losses {losses}  regress(fail) {regress}  broken {broken}")
    print(f"  sum score: base {sum_base:.3f}  cand {sum_cand:.3f}  delta {net:+.3f}")
    promote = (broken == 0) and (regress == 0) and (net >= args.min_net)
    verdict = "PROMOTE" if promote else "REJECT"
    print(f"  => {verdict}")
    for r in fail_reasons:
        print(f"    ! {r}")
    print(f"  ({len(cases)} cases, {time.monotonic() - start:.1f}s)")
    if args.json:
        json.dump({
            "promote": promote, "wins": wins, "regress": regress, "broken": broken,
            "sum_base": sum_base, "sum_cand": sum_cand, "net": net,
            "rows": {n: {"base": parse_verdict(r[0])[1], "cand": parse_verdict(r[1])[1]}
                     for n, r in rows.items()},
        }, open(args.json, "w"), indent=2)
    return 0 if promote else 1


if __name__ == "__main__":
    sys.exit(main())
