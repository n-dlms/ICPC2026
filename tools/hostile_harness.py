#!/usr/bin/env python3
"""tools/hostile_harness.py — Protocol-Level Stress Harness (mimics Codeforces).

The Codeforces interactor is a hostile environment. This harness adds three
stress modes on top of our local judge (tools/interactor.py):

  1. PIPE-DEADLOCK  (--pipe-bytes N): shrink the solver's stdout pipe buffer to
     N bytes and run a case that produces a large single-frame response. The
     solver must stream/flush its output or it blocks on std::cout while the
     interactor waits -> TLE. We assert VERDICT ok and runtime < timeout.

  2. FLOATING-POINT GHOST (--ghost): run with arrival timestamps shifted by a
     constant EPS so every frame timestamp ends in ...000000001 (EPS=1e-9) or
     ...999999999 (EPS=1-1e-9). A solver that compares times with == or a
     wrong EPS desyncs -> silent WA. We assert VERDICT ok (the interactor
     validates every assignment; a desync yields a violation, not ok).

  3. OOM TRIGGERER (--measure-rss): run a max-scale case and track the solver's
     peak RSS (VmHWM). We assert peak < 200000 kB (200 MB; 256 MB limit).

Usage:
  python3 tools/hostile_harness.py pipe   --solver build/submission
  python3 tools/hostile_harness.py float  --solver build/submission
  python3 tools/hostile_harness.py oom    --solver build/submission
  python3 tools/hostile_harness.py all    --solver build/submission

The harness GENERATES its own cases; it does not depend on the zoo/adv sets.
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
INTERACTOR = os.path.join(HERE, "interactor.py")

# ---- case emitter (mirrors tools/adversarial_generator.py:emit) ----
def emit(path, K, S, lat, bw, bpt, layers, slo, table, arrivals):
    lines = []
    lines.append(f"{K} {S} {lat} {bw} {bpt} {layers}")
    lines.append(" ".join(str(x) for x in slo))
    lines.append(str(len(table)))
    for r in table:
        lines.append(" ".join(str(x) for x in r))
    lines.append(str(len(arrivals)))
    for (t, lin, lo) in arrivals:
        lines.append(f"{t} {lin} {lo}")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def default_table():
    # batch prefill_pre prefill_proc prefill_post decode_pre decode_proc decode_post
    return [[1, 1, 1, 1, 1, 1, 1], [8, 1, 1, 1, 1, 1, 1]]


def default_slo():
    return [100, 100, 100, 1, 1, 0.5, 0.5]


def run_interactor(case, solver, extra, timeout=120.0):
    cmd = [sys.executable, INTERACTOR, "--case", case, "--solver", solver] + extra
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    out = p.stdout.strip()
    return p.returncode, out, p.stderr.strip()


def check(ok, msg, detail=""):
    status = "PASS" if ok else "FAIL"
    print(f"  [{status}] {msg}" + (f"  ({detail})" if detail else ""))
    return ok


def mode_pipe(solver):
    print("=== PIPE-DEADLOCK STRESS ===")
    ok = True
    # High-R immediate burst forces a large first frame response; tiny pipe
    # buffer (256B) means the solver must stream/flush or block.
    case = "/tmp/hostile_pipe.txt"
    K, S, lat, bw, bpt, layers = 8, 1.0, 1.0, 1.0, 1, 8
    arrivals = [(0, 1, 1) for _ in range(500)]
    emit(case, K, S, lat, bw, bpt, layers, default_slo(), default_table(), arrivals)
    for nbytes in (256, 4096):
        rc, out, err = run_interactor(case, solver, ["--pipe-bytes", str(nbytes)])
        verdict_ok = "VERDICT ok" in out
        no_tle = "VERDICT tle" not in out
        detail = out.splitlines()[-1] if out else err[:200]
        ok &= check(verdict_ok and no_tle,
                    f"pipe-bytes={nbytes}: solver streamed without deadlock",
                    detail)
    return ok


def mode_float(solver):
    print("=== FLOATING-POINT GHOST STRESS ===")
    ok = True
    case = "/tmp/hostile_float_base.txt"
    K, S, lat, bw, bpt, layers = 8, 1.0, 1.0, 1.0, 1, 8
    # Mixed arrivals (spread + burst) so the sim exercises prefill + decode.
    arrivals = []
    for i in range(200):
        arrivals.append((float(i), 1 + (i % 7), 1 + (i % 5)))
    arrivals += [(0, 64, 64) for _ in range(20)]  # a few big ones
    emit(case, K, S, lat, bw, bpt, layers, default_slo(), default_table(), arrivals)
    for eps, label in ((1e-9, "...000000001"), (1 - 1e-9, "...999999999")):
        rc, out, err = run_interactor(case, solver, ["--ghost", str(eps)])
        verdict_ok = "VERDICT ok" in out
        no_violation = "VERDICT violation" not in out
        detail = out.splitlines()[-1] if out else err[:200]
        ok &= check(verdict_ok and no_violation,
                    f"ghost EPS={eps:.9g} ({label}): no time-desync / silent WA",
                    detail)
    return ok


def mode_oom(solver):
    print("=== OOM TRIGGERER STRESS ===")
    ok = True
    case = "/tmp/hostile_oom.txt"
    K, S, lat, bw, bpt, layers = 8, 1.0, 1.0, 1.0, 1, 64
    arrivals = [(float(i), 1 + (i % 16), 1 + (i % 8)) for i in range(2000)]
    emit(case, K, S, lat, bw, bpt, layers, default_slo(), default_table(), arrivals)
    rc, out, err = run_interactor(case, solver, ["--measure-rss"])
    # parse max_rss_kb from the verdict line
    max_rss = None
    for line in out.splitlines():
        if "max_rss_kb" in line:
            max_rss = int(line.split("max_rss_kb")[1].split()[0])
    verdict_ok = "VERDICT ok" in out
    detail = out.splitlines()[-1] if out else err[:200]
    ok &= check(verdict_ok, "max-scale case completed", detail)
    if max_rss is None:
        ok &= check(False, "RSS reported", "max_rss_kb not found in output")
    else:
        ok &= check(max_rss < 200000, f"peak RSS {max_rss} kB < 200000 kB (200 MB)",
                    f"peak {max_rss} kB")
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["pipe", "float", "oom", "all"])
    ap.add_argument("--solver", required=True)
    args = ap.parse_args()
    if args.mode == "pipe":
        ok = mode_pipe(args.solver)
    elif args.mode == "float":
        ok = mode_float(args.solver)
    elif args.mode == "oom":
        ok = mode_oom(args.solver)
    else:
        ok = mode_pipe(args.solver) and mode_float(args.solver) and mode_oom(args.solver)
    print("\nHOSTILE HARNESS:", "ALL PASS" if ok else "FAILURE")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
