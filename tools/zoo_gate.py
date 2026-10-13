#!/usr/bin/env python3
"""tools/zoo_gate.py — zoo gate (new protocol, interactor backend).

Runs a solver binary against every case under --zoo-dir.

PASS requires, per case:
  - VERDICT ok (any violation/stuck/tle/re = FAIL), AND
  - if a reference (V1) is supplied: candidate score >= REGRESS * V1 score
    (default REGRESS=0.9). Below that the case is flagged as a SCORE REGRESSION
    even though the verdict is ok — catching candidates that "pass the zoo"
    but quietly score worse on throughput (Failure 4: no score-quality tracking).

Reference can be a live binary (--ref, re-run) or a cached JSON of V1 scores
(--ref-json). Use --ref-json-out to persist a freshly-run --ref as JSON.

Usage:
  python3 tools/zoo_gate.py --solver build/frozen_v5 [--ref build/frozen_v1] \\
      [--ref-json cases/zoo_v1_scores.json] [--regress 0.9] [-j 4]
"""

import argparse
import concurrent.futures as cf
import json
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

SCORE_RE = re.compile(r"VERDICT\s+(\w+).*score\s+([-0-9.eE+]+)")


def run_one(solver, case_path, timeout):
    p = subprocess.run(
        [sys.executable, str(HERE / "interactor.py"),
         "--case", str(case_path), "--solver", str(solver),
         "--timeout", str(timeout)],
        capture_output=True, text=True)
    last = (p.stdout or p.stderr).strip().splitlines()[-1] if (p.stdout or p.stderr) else ""
    return case_path.name, last


def parse_line(line):
    """Return (verdict_ok, score_or_None)."""
    m = SCORE_RE.search(line)
    if not m:
        return False, None
    verdict_ok = (m.group(1) == "ok")
    try:
        score = float(m.group(2))
    except ValueError:
        score = None
    return verdict_ok, score


def run_all(solver, cases, timeout, jobs):
    results = {}
    with cf.ThreadPoolExecutor(max_workers=jobs) as ex:
        futs = {ex.submit(run_one, solver, c, timeout): c for c in cases}
        for fut in cf.as_completed(futs):
            name, line = fut.result()
            results[name] = line
    return results


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--solver", required=True)
    ap.add_argument("--zoo-dir", default=str(HERE.parent / "cases" / "zoo"))
    ap.add_argument("-j", "--jobs", type=int, default=4)
    ap.add_argument("--timeout", type=float, default=20.0)
    ap.add_argument("--json", default=None,
                    help="write per-case candidate scores to this JSON")
    ap.add_argument("--ref", default=None,
                    help="reference solver binary (e.g. build/frozen_v1) for score gating")
    ap.add_argument("--ref-json", default=None,
                    help="cached reference scores JSON (name->score); skips re-running --ref")
    ap.add_argument("--ref-json-out", default=None,
                    help="save freshly-run --ref scores to this JSON")
    ap.add_argument("--regress", type=float, default=0.9,
                    help="flag candidate if score < REGRESS * ref_score (default 0.9)")
    args = ap.parse_args()

    cases = sorted(Path(args.zoo_dir).glob("*.txt"))
    if not cases:
        print(f"no cases in {args.zoo_dir}")
        return 2

    cand = run_all(args.solver, cases, args.timeout, args.jobs)

    if args.json:
        out = {n: parse_line(l)[1] for n, l in cand.items()}
        Path(args.json).write_text(json.dumps(out, indent=2))

    # Reference scores
    ref_scores = {}
    if args.ref_json:
        ref_scores = {k: float(v) for k, v in json.loads(Path(args.ref_json).read_text()).items()}
    elif args.ref:
        ref_lines = run_all(args.ref, cases, args.timeout, args.jobs)
        ref_scores = {}
        for n, l in ref_lines.items():
            ok, sc = parse_line(l)
            if ok and sc is not None:
                ref_scores[n] = sc
        if args.ref_json_out:
            Path(args.ref_json_out).write_text(json.dumps(ref_scores, indent=2))
            print(f"  (wrote reference scores -> {args.ref_json_out})")

    gating = bool(ref_scores)
    if gating:
        print(f"score regression gate: candidate < {args.regress} * V1 => FLAG "
              f"(reference cases: {len(ref_scores)})\n")

    ok = 0
    fails = []
    regress_flags = []
    for name in sorted(cand):
        line = cand[name]
        verdict_ok, score = parse_line(line)
        if not verdict_ok:
            fails.append(f"  FAIL {name}: {line}")
            continue
        if gating and name in ref_scores and ref_scores[name] is not None:
            ref_sc = ref_scores[name]
            if ref_sc > 0 and score < args.regress * ref_sc - 1e-9:
                regress_flags.append(
                    f"  SCORE-REGRESS {name}: cand {score:.4f} < "
                    f"{args.regress}*V1 {ref_sc:.4f} = {args.regress*ref_sc:.4f}")
                continue
        ok += 1
        print(f"  ok   {name:<24} score {score}")

    print(f"\nzoo gate: {ok}/{len(cases)} passed"
          + (f" (gated on V1 score)" if gating else " (verdict-only; no --ref)"))
    for f in fails:
        print(f)
    for f in regress_flags:
        print(f)
    return 0 if (not fails and not regress_flags) else 1


if __name__ == "__main__":
    sys.exit(main())
