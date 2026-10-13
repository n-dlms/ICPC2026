#!/usr/bin/env python3
"""SB03_sim_test.py — unit tests + sizer + funding ledger for the forward sim.

Self-contained: no repository scoring/judging tooling is used as evidence
(an earlier revision's interactor cross-check was removed to keep the harness
independent of the code under test); every check below is either a
statement-example replay (problem.md Examples 1/2) or a self-constructed
scenario inside the sim binary.

  1. ex1 / ex2   : statement Example 1/2 replays. ex1 additionally re-replayed
                   end-to-end through the FILE path (case + script -> `case`
                   mode) with a byte-identical stream diff — independent of the
                   in-binary hard-coded assertions.
  2. self-checks : fifo / split / chunk / tie / roll (a)-(e) specified in
                   SB03_simulator_implementation.md.
  3. stress      : R=2000, sum Lout=2e5 end-to-end run; parses sim-core ns/event,
                   extrapolated 2e6-frame time, peak RSS.
  4. size        : minified size of the sim's artifact core (own minifier that
                   mirrors tools/merge_submission.py's transforms; the rename
                   map is READ from that file, nothing scoring-related is run).
  5. ledger      : minified bytes of each heuristic block a rollout migration
                   deletes, vs the sim bytes it must add -> net table.

Usage: python3 research/techniques/SB03_sim_test.py [--skip-stress] [--only x]
"""
import argparse
import os
import re
import subprocess
import sys

REPO = "/home/dlamini/ICPC2026"
SIM_SRC = f"{REPO}/research/techniques/SB03_forward_sim.cpp"
SIM_BIN = "/tmp/sb03sim"      # -DSB03_DRIVER -DSB03_TRACE
BENCH_BIN = "/tmp/sb03bench"  # -DSB03_DRIVER
TMP = "/tmp/sb03_work"

# ----------------------------------------------------------------------------
# build (strict flags; with -Werror the audit is not done
# until this passes with zero warnings)
# ----------------------------------------------------------------------------
STRICT = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror"]


def build():
    os.makedirs(TMP, exist_ok=True)
    subprocess.run(["g++", *STRICT, "-DSB03_DRIVER", "-DSB03_TRACE",
                    "-o", SIM_BIN, SIM_SRC], check=True)
    subprocess.run(["g++", *STRICT, "-DSB03_DRIVER",
                    "-o", BENCH_BIN, SIM_SRC], check=True)


# ----------------------------------------------------------------------------
# 1. statement-example replays
# ----------------------------------------------------------------------------
# Example 1 (problem.md:499-556, 569-620) — full input side, byte-exact.
EX1_CASE = """\
# Example 1 startup: K=1 S=1 lat=2 bw=1 bpt=125000 layers=4 (problem.md:499)
1 1.0 2.0 1.0 125000 4
30 15 0.0625 0.0222 0 0.5 0.5
2
1 3 10 2 1 4 1
4 3 10 2 1 4 1
1
0 4 1
"""

EX1_SCRIPT = """\
; Example 1 response schedule (problem.md:539-556)
E P PRE 0 0

EMPTY

C0 P PROC 0 4 0 0

EMPTY

E P POST 0 0

E D PRE -1 1 0

EMPTY

C0 D PROC 0 1 0

EMPTY

E D POST -1 1 0

EMPTY
"""

# The judge stream the sim must reproduce (input column of problem.md:580-615).
EX1_WANT = """\
0.000000000
1
ARR 0 4
4.000000000
1
TDN E P PRE 0 0 3.000000000
10.000000000
1
XDN UP 0 500000 PRE 1 0
21.000000000
1
TDN C0 P PROC 0 4 0 0 10.000000000
27.000000000
1
XDN DOWN 0 500000 PRE 1 0
30.000000000
1
TDN E P POST 0 0 2.000000000
32.000000000
1
TDN E D PRE -1 1 0 1.000000000
35.000000000
1
XDN UP 0 125000 DEC 1 0
40.000000000
1
TDN C0 D PROC 0 1 0 4.000000000
43.000000000
1
XDN DOWN 0 125000 DEC 1 0
45.000000000
2
TDN E D POST -1 1 0 1.000000000
FIN 0
"""


def check_ex1_stream():
    """Replay Example 1 through the file path and diff the stream byte-exact."""
    case, script = f"{TMP}/ex1_case.txt", f"{TMP}/ex1_script.txt"
    open(case, "w").write(EX1_CASE)
    open(script, "w").write(EX1_SCRIPT)
    r = subprocess.run([SIM_BIN, "case", case, script],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return False, f"case rc={r.returncode}: {r.stderr.strip()}"
    got, want = r.stdout.splitlines(), EX1_WANT.splitlines()
    if len(got) != len(want):
        return False, f"stream lines {len(got)} != {len(want)}"
    for i, (g, w) in enumerate(zip(got, want)):
        if g != w:
            return False, f"line {i}: got '{g}' want '{w}'"
    m = re.search(r"score=([0-9.eE+-]+)", r.stderr)
    sc = float(m.group(1)) if m else float("nan")
    return True, (f"byte-identical stream ({len(want)} lines, 11 frames, 12 events); "
                  f"sim score={sc:.3f} (statement: TDR=30, token@45)")


def check_mode(mode):
    r = subprocess.run([SIM_BIN, *mode.split()], capture_output=True, text=True)
    return r.returncode == 0 and "PASS" in r.stdout, r.stdout.strip()


# ----------------------------------------------------------------------------
# 3. stress
# ----------------------------------------------------------------------------
def run_stress(rn, tot, seed):
    r = subprocess.run([BENCH_BIN, "stress", str(rn), str(tot), str(seed)],
                       capture_output=True, text=True)
    out = r.stdout
    ok = r.returncode == 0
    m = re.search(r"sim core ([0-9.]+)s = ([0-9.]+) ns/event, ([0-9.]+) us/frame", out)
    nsev = float(m.group(2)) if m else float("nan")
    m2 = re.search(r"extrapolated 2e6-frame test \(sim core only\): ([0-9.]+) s", out)
    ext = float(m2.group(1)) if m2 else float("nan")
    m3 = re.search(r"peak RSS: pass A ([0-9.]+) MB", out)
    rss = float(m3.group(1)) if m3 else float("nan")
    m4 = re.search(r"events=(\d+) frames=(\d+)", out)
    ev, fr = (int(m4.group(1)), int(m4.group(2))) if m4 else (0, 0)
    m5 = re.search(r"chk_mismatch=(\d+)", out)
    chkb = int(m5.group(1)) if m5 else -1
    good = (ok and nsev < 1000 and ext < 5.0 and rss < 100 and chkb == 0)
    return good, (f"events={ev} frames={fr} sim-core={nsev} ns/event "
                  f"({m.group(3)} us/frame), 2e6-frame extrapolation={ext}s, "
                  f"peakRSS={rss}MB, chk_mismatch={chkb}\n{out}")


# ----------------------------------------------------------------------------
# 4. minifier (mirrors tools/merge_submission.py transforms; map READ from it)
# ----------------------------------------------------------------------------
def load_solver_renames():
    """Read tools/merge_submission.py (READ-ONLY) and extract its _RENAME map
    in declaration order — the artifact applies exactly these renames."""
    txt = open(f"{REPO}/tools/merge_submission.py").read()
    body = re.search(r"_RENAME = \{(.*?)\n\}", txt, re.S).group(1)
    return [(a, b) for a, b in re.findall(r'"([^"]+)":\s*"([^"]+)"', body)]


SOLVER_RENAMES = load_solver_renames()

# sim-specific renames (z-prefix: cannot collide with the solver's q-tokens)
SIM_RENAMES = [
    ("next_event_time", "z0"), ("inject_arrival", "z1"), ("advance_to", "z2"),
    ("tdn_effect", "z3"), ("rollback", "z4"), ("jclear", "z5"),
    ("sum_tdr", "z6"), ("sum_gap", "z7"), ("sum_span", "z8"),
    ("last_fin", "z9"), ("first_arr", "za"), ("n_gap", "zb"),
    ("lfree", "zc"), ("cfree", "zd"), ("narr", "ze"), ("seqc", "zf"),
    ("evb", "zg"), ("jlive", "zh"), ("jpi", "zi"), ("jpx", "zj"),
    ("deliver", "zk"), ("enq_up", "zl"), ("enq_dn", "zm"),
    ("xfer", "zn"), ("TIE_EPS", "zo"), ("PPRE_R", "zp"), ("PPOST_R", "zq"),
    ("DPRE_R", "zr"), ("DPOST_R", "zs"), ("W_IUP", "zt"), ("W_IDN", "zu"),
    ("W_DUP", "zv"), ("W_DDN", "zw"), ("R_PPRE", "zA"), ("R_PPROC", "zB"),
    ("R_PPOST", "zC"), ("R_DPRE", "zD"), ("R_DPROC", "zE"), ("R_DPOST", "zF"),
    ("P_PRE", "zG"), ("P_PROC", "zH"), ("P_POST", "zI"), ("D_PRE", "zJ"),
    ("D_PROC", "zK"), ("D_POST", "zL"), ("ArrIn", "zM"), ("Xf", "zN"),
    ("HEv", "zO"), ("jw", "zP"), ("active", "zQ"), ("score", "zR"),
    ("dur", "zS"), ("has_dup", "zT"), ("enq_up_a", "zU"), ("enq_dn_a", "zV"),
    ("sc_slo1", "zW"), ("sc_slo2", "zX"), ("sc_ub", "zY"), ("sc_b", "zZ"),
    ("sc_db", "z1a"), ("sc_wt", "z1b"), ("sc_wc", "z1c"), ("Rq", "z1d"),
    ("nevt", "z1e"), ("ArrIn", "zM"),
]


def strip_comments(text):
    """String/char-literal-aware comment stripper (port of merge_submission's)."""
    out, i, n, state = [], 0, len(text), 0
    while i < n:
        c = text[i]
        nx = text[i + 1] if i + 1 < n else ""
        if state == 1:
            if c == "\n":
                state = 0
                out.append(c)
            i += 1
            continue
        if state == 2:
            if c == "*" and nx == "/":
                state = 0
                i += 2
                continue
            i += 1
            continue
        if state == 3:
            out.append(c)
            if c == "\\" and nx:
                out.append(nx)
                i += 2
                continue
            if c == '"':
                state = 0
            i += 1
            continue
        if state == 4:
            out.append(c)
            if c == "\\" and nx:
                out.append(nx)
                i += 2
                continue
            if c == "'":
                state = 0
            i += 1
            continue
        if c == "/" and nx == "/":
            state = 1
            i += 2
            continue
        if c == "/" and nx == "*":
            state = 2
            i += 2
            continue
        if c == '"':
            state = 3
            out.append(c)
            i += 1
            continue
        if c == "'":
            state = 4
            out.append(c)
            i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def _strip_strings(line):
    out, i, st = [], 0, 0
    while i < len(line):
        c = line[i]
        if st == 0:
            if c == '"':
                st = 1
            elif c == "'":
                st = 2
            elif c == "/" and i + 1 < len(line) and line[i + 1] == "/":
                break
            else:
                out.append(c)
        elif st == 1:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                st = 0
        else:
            if c == "\\":
                i += 2
                continue
            if c == "'":
                st = 0
        i += 1
    return "".join(out)


def apply_renames(text, renames):
    for long, short in renames:
        text = re.sub(r"\b" + re.escape(long) + r"\b", short, text)
    return text


def reindent(text):
    """merge_submission.minify's reindent: blank lines dropped, preprocessor at
    column 0, code indented one space per brace depth (leading '}' dedents)."""
    out, depth = [], 0
    for line in text.split("\n"):
        s = line.strip()
        if not s:
            continue
        if s.startswith("#"):
            out.append(s)
            continue
        code = _strip_strings(s)
        lead_close = len(code) - len(code.lstrip("}"))
        emit_depth = max(0, depth - lead_close)
        out.append(" " * emit_depth + s)
        depth += code.count("{") - code.count("}")
        if depth < 0:
            depth = 0
    return "\n".join(out) + "\n"


def minify_sim_core(minimal=False):
    """Minified sim core. minimal=True additionally drops the judge-mirroring
    legality checks and the has_dup guards: in-artifact, candidate actions are
    constructed legal (the live solver already trusts its own scheduler), so
    the artifact ships apply() without its error paths."""
    src = open(SIM_SRC).read()
    src = re.sub(r"#ifdef SB03_DRIVER\n.*\Z", "", src, flags=re.S)
    src = re.sub(r"#ifdef SB03_TRACE\n.*?\n#endif\n", "", src, flags=re.S)
    src = strip_comments(src)
    if minimal:
        # drop every `if (...) return "...";` legality line inside apply()
        src = re.sub(r'^\s*if \([^\n]*\) return "[^"]*";\s*$', "", src, flags=re.M)
        # drop the has_dup helper definition (its call sites were `return` lines)
        src = re.sub(r"static bool has_dup\(.*?\n\}\n", "", src, flags=re.S)
    src = re.sub(r"#include <[a-z./]+>\n", "", src)
    src = src.replace("namespace sb03 {", "").replace("}  // namespace sb03", "")
    src = apply_renames(src, SOLVER_RENAMES)
    src = apply_renames(src, SIM_RENAMES)
    src = re.sub(r"\n{2,}", "\n", src)
    return reindent(src).strip() + "\n"


def minify_block(path, a, b):
    """Minified char count of source lines [a,b] (1-based, inclusive) as they
    appear in the current artifact (comments stripped, artifact renames applied,
    reindented)."""
    lines = open(path).read().split("\n")
    chunk = "\n".join(lines[a - 1:b]) + "\n"
    chunk = strip_comments(chunk)
    chunk = apply_renames(chunk, SOLVER_RENAMES)
    chunk = re.sub(r"\n{2,}", "\n", chunk)
    return len(reindent(chunk).strip() + "\n")


def sizer():
    out = {}
    for tag, minimal in (("reference", False), ("minimal(in-artifact)", True)):
        mini = minify_sim_core(minimal)
        n_all = len(mini)

        def piece(pat, m=mini):
            mm = re.search(pat, m, re.S)
            return len(mm.group(0)) if mm else 0
        p_dur = piece(r"double zS\(int c, int b\) const \{.*?\n \}")
        p_xfer = piece(r"double zn\(double len\) const \{.*?\}")
        p_score = piece(r"double zR\(\) const \{.*?\n \}")
        p_sc = piece(r"double zW.*?z1c = 0;")
        shared = p_dur + p_xfer + p_score + p_sc
        print(f"[size] {tag:22s} core: {n_all} chars, reuse {shared} "
              f"[get {p_dur}/xfer {p_xfer}/score {p_score}/sc {p_sc}] "
              f"-> NEW {n_all - shared}")
        out[tag] = (n_all, n_all - shared)
    return out


# ----------------------------------------------------------------------------
# 5. funding ledger
# ----------------------------------------------------------------------------
ARTIFACT = len(open(f"{REPO}/src/submission.cpp").read())
CF_MAX = 65535

LEDGER = [
    ("M2  D PRE hold-or-issue -> rollout (SB02 4a)",
     f"{REPO}/src/policies.cpp", 310, 391,
     "NoDelay::hold_dpre + SloAwareDelay::hold_dpre (incl. true_period block)",
     70),   # stub residue: one `return false;` body must remain
    ("M2b D PRE size (crossing) -> rollout (SB02 4a)",
     f"{REPO}/src/policies.cpp", 220, 308,
     "MarginalBatch::group_size (SLO cap scan + Twist-1 crossing)",
     90),
    ("M3  P PRE cloud choice -> rollout (SB02 4b)",
     f"{REPO}/src/policies.cpp", 12, 115,
     "PrefillLoadBalanceCloud::choose (mega gate + Tier-1 + EFT + PoTC band)",
     80),
    ("M4  accumulate-vs-drain -> rollout (SB02 4c)",
     f"{REPO}/src/scheduler.cpp", 170, 195,
     "DPRE_ACCUMULATE gate (dpre_acc_skip block; #ifdef wrapper stays, +44)",
     44),
    ("M5  P POST arbitration -> L1 closed form",
     f"{REPO}/src/scheduler.cpp", 111, 133,
     "run_ppost arbitration (lambda_TDR urgency + cloud_starved/link_busy)",
     60),
    ("M6  cliff/endgame constant block (NOT recommended to delete)",
     f"{REPO}/src/Solution.cpp", 258, 284,
     "traffic-aware all-or-nothing cliff flip (D-050/D-115; load-bearing)",
     0),
]


def ledger(new_bytes=None):
    sizes = sizer()
    sim_new = sizes["minimal(in-artifact)"][1]
    print(f"[ledger] artifact now: {ARTIFACT} / {CF_MAX} chars "
          f"({CF_MAX - ARTIFACT} free)")
    rows, total = [], 0
    for title, path, a, b, what, residue in LEDGER:
        mini = minify_block(path, a, b)
        net = mini - residue
        total += net
        rows.append((title, mini, residue, net))
        print(f"[ledger] {mini:5d} min -{residue:3d} residue = {net:5d} net | {title}")
        print(f"[ledger]        = {what}")
    print(f"[ledger] TOTAL deletable (all six): {total} net minified chars")
    print(f"[ledger] sim needs (minimal in-artifact core): {sim_new} NEW chars")
    print(f"[ledger] headroom after adding sim, before any deletion: "
          f"{CF_MAX - ARTIFACT - sim_new} chars "
          f"({'FITS' if CF_MAX - ARTIFACT - sim_new >= 0 else 'DOES NOT FIT - deletion must come first'})")
    print(f"[ledger] per-migration running balance (order M2, M2b, M1-endgame, M3, M4, M5):")
    bal = CF_MAX - ARTIFACT
    # M1 (endgame F0 rollout) adds the sim but replaces nothing (SB02 4d)
    sim_add = sim_new
    print(f"[ledger]   start                          : {bal:+6d}")
    bal += -sim_add
    print(f"[ledger]   M1 endgame rollout (+sim core) : {bal:+6d}  "
          f"{'OK' if bal >= 0 else 'BUSTS CAP - reorder: delete M2 first'}")
    for title, mini, residue, net in rows:
        bal += net
        print(f"[ledger]   {title.split('  ')[0]:34s}: {bal:+6d}  "
              f"{'OK' if bal >= 0 else 'BUSTS CAP'}")
    return rows, total


# ----------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-stress", action="store_true")
    ap.add_argument("--only", default=None)
    args = ap.parse_args()
    build()
    ok = True
    if args.only in (None, "ex"):
        for mode in ("ex1", "ex2"):
            good, info = check_mode(mode)
            ok &= good
            print(("[PASS] " if good else "[FAIL] ") + info)
        good, info = check_ex1_stream()
        ok &= good
        print(("[PASS] " if good else "[FAIL] ") + "ex1 file-path stream diff: " + info)
    if args.only in (None, "self"):
        for mode in ("fifo", "split", "chunk", "tie", "roll 42", "roll 7", "roll 12345"):
            good, info = check_mode(mode)
            ok &= good
            print(("[PASS] " if good else "[FAIL] ") + info)
    if args.only in (None, "stress") and not args.skip_stress:
        good, info = run_stress(2000, 200000, 1)
        ok &= good
        print(("[PASS] " if good else "[FAIL] ") + "stress: " + info)
    if args.only in (None, "size"):
        ledger()
    print("ALL PASS" if ok else "FAILURES PRESENT")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
