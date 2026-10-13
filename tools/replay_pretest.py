#!/usr/bin/env python3
"""Replay the problem's pretest (judge I/O format) through our solver and
compare its assignments to the expected Output, frame by frame."""
import subprocess, sys, pathlib

PRETEST = pathlib.Path("pretest.txt")
SOLVER = sys.argv[1] if len(sys.argv) > 1 else "build/submission"

text = PRETEST.read_text().splitlines()

# Expected score (canonical answer for this pretest, under our local judge).
EXPECTED_SCORE = None
for ln in text:
    s = ln.strip()
    if s.startswith("EXPECTED_SCORE"):
        EXPECTED_SCORE = float(s.split()[1])
        break

def section(name):
    out, capture = [], False
    for ln in text:
        s = ln.strip()
        if s == name:
            capture = True
            continue
        if not capture:
            continue
        if s == "END":
            break
        if not s:
            continue
        out.append(ln)
    return out

inp = section("Input")
out_lines = section("Output")

# --- parse the pretest Input into an interactor case (config+scoring+table+arrival) ---
itok = []
for ln in inp:
    itok.extend(ln.split())
ci = 0
config = itok[ci:ci + 6]; ci += 6
scoring = itok[ci:ci + 7]; ci += 7
N = int(itok[ci]); ci += 1
rows = []
for _ in range(N):
    rows.append(" ".join(itok[ci:ci + 7])); ci += 7
# arrival: t, then "1", then "ARR id lin" -> lin; lout=1 (the example decodes 1 token)
t0 = itok[ci]; ci += 1
ci += 1  # skip the lone "1"
assert itok[ci] == "ARR", f"unexpected pretest arrival encoding: {itok[ci]}"
ci += 1
arr_id = itok[ci]; ci += 1
arr_lin = itok[ci]; ci += 1
ARR_LIN = int(arr_lin)
ARR_LOUT = 1  # pretest example decodes exactly 1 token

i = 5  # inp[0..4] = config/scoring/N/rows; inp[5] starts the frame list
       # (the first "frame" is the ARRIVAL: t=inp[5], e=inp[6]=1, ev=inp[7]="ARR 0 4")
frames = []
while i < len(inp):
    t = inp[i]; i += 1
    if i >= len(inp):
        break
    e = int(inp[i]); i += 1
    evs = [inp[j] for j in range(i, i + e)]; i += e
    frames.append((t, e, evs))

proc = subprocess.Popen([SOLVER], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                        text=True, bufsize=1)

def send(line):
    proc.stdin.write(line + "\n"); proc.stdin.flush()

send(" ".join(config))
send(" ".join(scoring))
send(str(N))
for r in rows:
    send(r)

solver_assigns = []
for (t, e, evs) in frames:
    send(t)
    send(str(e))
    for ev in evs:
        send(ev)
    nline = proc.stdout.readline()
    if not nline:
        print("SOLVER EOF mid-protocol"); break
    n = int(nline.split()[0])
    got = []
    for _ in range(n):
        al = proc.stdout.readline()
        if not al:
            print("SOLVER EOF mid-response"); break
        got.append(al.strip())
    solver_assigns.append(got)

proc.stdin.close()
proc.wait()

steps = ("P PRE", "P PROC", "P POST", "D PRE", "D PROC", "D POST")
expected = [ln.strip() for ln in out_lines if any(k in ln for k in steps)]
flat_solver = [a for fr in solver_assigns for a in fr]

print(f"frames replayed: {len(frames)}")
print(f"solver responses: {len(solver_assigns)}  total assignments: {len(flat_solver)}")
print(f"expected assignments: {len(expected)}")
print("\n--- solver output (frame by frame) ---")
for idx, fr in enumerate(solver_assigns):
    print(f"frame {idx}: {fr}")
print("\n--- expected (from pretest Output) ---")
for e in expected:
    print(f"  {e}")

ok = flat_solver == expected
print("\nRESULT:", "PASS (assignments match expected)" if ok else "MISMATCH")
if not ok:
    for a, b in zip(flat_solver, expected):
        mark = "" if a == b else "  <-- diff"
        print(f"  got=[{a}]  exp=[{b}]{mark}")

# --- Criterion 3: exact score match (3 decimals) ---
# Build an interactor case from the pretest Input and let our local judge score
# the solver's actual schedule. The expected score is the pretest's canonical
# answer (recorded at the top of pretest.txt as EXPECTED_SCORE).
score_ok = True
score_detail = ""
if EXPECTED_SCORE is None:
    score_ok = False
    score_detail = "EXPECTED_SCORE not found in pretest.txt"
else:
    import tempfile as _tf, os as _os
    case_lines = []
    case_lines.append(" ".join(config))
    case_lines.append(" ".join(scoring))
    case_lines.append(str(N))
    case_lines.extend(rows)
    case_lines.append("1")
    case_lines.append(f"{t0} {ARR_LIN} {ARR_LOUT}")
    with _tf.NamedTemporaryFile("w", suffix=".txt", delete=False) as cf:
        cf.write("\n".join(case_lines) + "\n")
        case_path = cf.name
    try:
        pr = subprocess.run([sys.executable, str(pathlib.Path(__file__).resolve().parent /
                            "interactor.py"), "--case", case_path, "--solver", SOLVER],
                           capture_output=True, text=True, timeout=60)
        got_score = None
        for line in (pr.stdout + pr.stderr).splitlines():
            if line.startswith("VERDICT"):
                for seg in line.split("|"):
                    seg = seg.strip()
                    if seg.startswith("score"):
                        try:
                            got_score = float(seg.split()[1])
                        except Exception:
                            pass
        if got_score is None:
            score_ok = False
            score_detail = "could not parse score from interactor"
        else:
            drift = abs(got_score - EXPECTED_SCORE)
            score_ok = drift <= 0.0005
            score_detail = f"got {got_score:.3f} expected {EXPECTED_SCORE:.3f} drift {drift:.4f}"
    finally:
        _os.unlink(case_path)

print(f"RESULT: {'PASS' if score_ok else 'FAIL'} (score: {score_detail})")
print("\nFINAL:", "PASS" if (ok and score_ok) else "FAIL")
sys.exit(0 if (ok and score_ok) else 1)
