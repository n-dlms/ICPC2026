> **Post-contest archive note (Oct 2026):** Pre-contest preparation completed. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). The generic-contract survey below was completed pre-contest and reconciled against the official statement at contest start; "verify against the real spec" items are preserved as historical working notes. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# The Generic OJ Execution Contract (What the Judge Assumes About Your Program)

Research note for the ICPC Online Challenge 2026 (Huawei, "Scheduling Algorithm for Collaborative Inference System", Aug 14–28 2026).
Context: our submission is a C++17 solver driving an interactive per-tick protocol with a custom scoring function. The exact judge
protocol was NOT public until Aug 14 (contest start); everything below is the *generic* contract of mainstream contest judges
(DOMjudge — the ICPC standard, Kattis, Codeforces, ICPC World Finals practices), gathered from primary docs.

Evidence grading used throughout:

- **[Documented]** — stated in official docs/manuals/pages linked below.
- **[Anecdotal]** — community reports, unverified numbers.
- **[Inference]** — standard OS/pipe semantics applied to judge scenarios; verify against the real spec.

---

## 1. Time measurement: CPU time vs wall time

- **[Documented]** DOMjudge (used by ICPC): "Time limits are enforced primarily in CPU time, and secondly a more lax wall clock
  time limit … so submissions cannot idle and hog judgedaemons." A program sleeping/blocked does not consume CPU time; the wall
  limit exists to kill idle programs. [S1][S2]

- **[Documented]** Soft vs hard limit in DOMjudge: soft = problem time limit × language time factor (default 1). The config
  `timelimit_overshoot` (default `'1s|10%'` — i.e. max(1 s, 10% of the limit)) computes the hard limit. runguard kills the program
  when **either** the hard wall clock or CPU time has passed; the **verdict uses the actual (soft) limit** — a run that finishes
  between soft and hard is still TLE (overshoot exists so judges can see how close submissions were). [S3][S4]

- **[Documented]** Kattis measures "total CPU time used by your process, so you do not gain extra allotted running time by using
  multiple threads — it simply means that you reach the time limit faster." [S7]

- **[Documented]** Interactive problems: "The time limit … is how much time your submission may spend; the time spent by the
  judge program is not counted towards this." [S5]

- **[Documented]** `getrusage(RUSAGE_SELF)` exposes `ru_utime` (user) + `ru_stime` (system) CPU; on Linux this is the sum across
  all threads of the process. [S9]

- **[Inference]** For our per-tick protocol: if the judge counts CPU time of the whole process, stdin-blocking between ticks is
  FREE (wall-only cost). If it counts wall time, the per-tick judge latency is charged to us. The DOMjudge/Kattis standard is
  CPU-time-primary — so a solver that computes nothing while waiting for the next tick is safe under that contract; a solver that
  burns cycles while waiting is not.

## 2. Memory measurement

- **[Documented]** Linux `ru_maxrss` = peak resident set size (high-water mark, never decreases) in **kilobytes** on Linux
  (bytes on macOS, pages on Solaris). RSS ≠ heap: it includes binary, shared-lib pages, mmap'd files, and anonymous pages. [S9][S10]

- **[Documented]** Linux quirk: `ru_maxrss` is **inherited across execve** — the kernel copies `signal->maxrss` from the parent's
  memory map into the exec'd child (`fs/exec.c`, `exec_mmap`). A judge process that peaked before launching the submission can
  inflate the measured peak. Practical mitigation on the judge side is a lean runner; on our side: be aware that tiny allocations
  can't produce tiny RSS because the static binary itself counts. [S10]

- **[Documented]** ICPC Japan judge: "Memory usage is limited to 2 GB in the environment. Note it is the total amount, not the
  amount you can use exclusively in your programs"; stack is unlimited (C/C++), capped only by total memory; single processor
  core; max 64 processes; source ≤ 256 KB; compile ≤ 30 s; no file writes; no external commands. Platform: GCE c2-standard-4. [S6]

- **[Documented]** MLE is not always reported as MLE: Kattis — a failed `malloc` returns NULL (likely RTE if unchecked); a failed
  C++ `new` raises `std::bad_alloc` → uncaught exception → **SIGABRT → RTE**. WF23 judging notes list "out of memory" under
  Runtime Error causes. Expect "memory limit exceeded" *or* "runtime error" for OOM. [S7][S8]

- **[Documented]** DOMjudge has separate verdicts (`memory-limit`, `output-limit`, `run-error`, `timelimit`, `wrong-answer`,
  `no-output` all at priority 99 — first error wins; `correct` at 1). [S4]

- **[Documented]** AddressSanitizer adds roughly 2× slowdown and 2× memory overhead — sanitizer builds are for correctness
  sweeps only, never for timing/memory envelopes. [S19]

## 3. Input robustness

- **[Documented]** ICPC WF: "All input comes from standard input"; input constraints are part of the statement; a program "may be
  run on multiple input files" — the process must be stateless/re-entrant across runs. [S8]

- **[Documented]** Failed extraction on `std::cin`: pre-C++11 the target is left uninitialized (UB); C++11+ zero-initializes
  numeric types on failure. A failed read that is silently ignored then treated as data is a classic stale-value injection (our
  Day-5 input-loop bug: uninitialized `k` on first failed read → stale-k zero-request injection → unbounded wait list → TLE-like
  hang on truncated streams). Check the stream state / break on EOF every read. [S11]

- **[Documented]** In interactive mode, reading blocks: "your program will wait until more data are available or until the judge
  program terminates the input… if your program attempts to read more input than can currently be provided … the program will
  stall indefinitely and your submission will get Time Limit Exceeded." [S5]

- **[Documented]** Extra whitespace in output is ignored by standard ICPC validators "unless the resulting output file is large
  enough to affect judging system performance" (disqualification-level offense). No "Presentation Error" exists — formatting
  errors are WA. [S8]

- **[Documented]** stderr is not used for judging; file writes are prohibited in ICPC-style sandboxes. [S6]

## 4. Output & flushing

- **[Documented]** Program stdout is buffered for speed; when stdout is a pipe (always, for judges) stdio is **fully buffered**
  (block-sized, e.g. 4–8 KB), not line-buffered. Output stays in your buffer until it fills or you flush — the judge never sees
  it. Interactive problems require flushing after each exchange: `fflush(stdout)` in C, `cout.flush()` / `std::endl` in C++ (note
  `endl` forces a flush every line = slow; prefer `'\n'` + explicit `flush()`). [S5]

- **[Documented]** Writing to a pipe whose read end is closed delivers **SIGPIPE** to the writer; default action terminates the
  process → exit status 128+13=141. If SIGPIPE is ignored, `write()` returns EPIPE. A judge that terminates early (e.g. on a
  detected violation or its own TLE) closes our stdin-pipe; our next output attempt can kill us with SIGPIPE (or if we ignore
  it, we keep "succeeding" with EPIPE errors — worse). [S13][S14]

- **[Documented]** Output limits exist: Kattis OLE = "program has produced too much output and we decided to shoot it down
  before it flooded our hard drive"; DOMjudge config has output-storage limits (default 50 KB of error/system output stored) and
  compile-output limits (default ~2.5 GB). [S7][S4]

- **[Inference]** For a score-reporting judge, an early-exit verdict may arrive while we're still mid-run; be able to exit cleanly
  (rc=0) on EOF of stdin and never write after EOF.

## 5. Termination & signal→verdict mapping

- **[Documented]** Shell/judge convention: a process killed by signal N exits with status 128+N. Core table (Linux signal(7)):

| Signal  | #   | Exit status | Typical cause                                                                   |
| ------- | --- | ----------- | ------------------------------------------------------------------------------- |
| SIGSEGV | 11  | 139         | OOB access, null deref, stack overflow from guard-page hit                      |
| SIGABRT | 6   | 134         | `abort()`, failed `assert`, glibc heap-corruption check, uncaught C++ exception |
| SIGKILL | 9   | 137         | OOM killer, or judge's hard kill                                                |
| SIGBUS  | 7   | 135         | misaligned access, truncated mmap                                               |
| SIGFPE  | 8   | 136         | integer divide by zero, integer overflow trap                                   |
| SIGPIPE | 13  | 141         | write to closed pipe                                                            |
| SIGTERM | 15  | 143         | judge's graceful kill                                                           |
| SIGXCPU | 24  | 152         | CPU limit (setrlimit RLIMIT_CPU)                                                |
| SIGXFSZ | 25  | 153         | file size limit                                                                 |

  [S14][S21][S22]

- **[Documented]** Kattis: "Run Time Error means your program crashed during execution… it terminated with a non-zero exit code,
  or with an uncaught exception… it is important that your main function … does not return a non-zero value." Returning non-zero
  (or `exit(1)`) from a C++ main is itself an RTE, even without a crash. [S7]

- **[Documented]** WF23: Runtime Error "may have many causes, including but not limited to division by zero, subscript out of
  bounds, out of memory." There is no PE; TLE on ANY test case → TLE (unless an earlier error already decided). [S8]

- **[Documented]** First-error semantics: DOMjudge `results_prio` — memory/output/run-error/timelimit/wrong-answer all equal
  priority, first incorrect (by rank) determines the final result; Kattis: "judging stops at the first error encountered, and
  your program may be too slow or crash on a later test case." For interactive: "the judgement may be Wrong Answer, if the
  solution exhibits an incorrect answer before it runs out of time." [S4][S7][S5]

- **[Documented]** `std::exit` vs return: destructors/atexit handlers run only on normal exit paths, never when a signal kills
  you. Buffered data still unwritten at signal-death is lost — flush before long compute stretches, not in a destructor. [S14]

## 6. Determinism & rejudges

- **[Documented]** `std::sort` is not stable: "the order of equal elements is not guaranteed to be preserved." Tie-order-dependent
  decisions (e.g. which request gets the slot when keys tie) are implementation-defined and can change across compiler versions —
  a rejudge risk if the judge rebuilds with a different libstdc++. Our Day-6 asort work is exactly this hazard class: sorted-key
  admission was kept because measured pools showed no tie divergence; that safety is empirical, not contractual. [S12]

- **[Documented]** DOMjudge judges with "Disable CPU frequency scaling and Intel 'Turbo Boost'" and "Disable address-space
  randomization to make programs with memory addressing bugs give more reproducible results" — reproducibility is engineered by
  the judge, but that only covers CPU speed and ASLR, not your own nondeterminism. [S3]

- **[Documented]** Unordered containers: iteration order is unspecified; hashing pointer addresses (e.g. `std::hash<T*>`) makes
  results address-dependent. With ASLR disabled addresses are stable per-machine but still not portable; iterate in sorted order
  if order matters. [S12]

- **[Documented]** Floating point: default `-O2` preserves IEEE semantics; `-ffast-math`/`-Ofast` allows reassociation
  `(A*B)*C → A*(B*C)`, removes NaN/Inf checks (`if (isnan(x))` → `if(false)`), can flush subnormals, and destroys compensated
  (Kahan-style) summation entirely. ICPC compiles with plain `-O2` (g++ 11.4, `-std=gnu++20 -static` on the Japan judge) —
  keep it that way. Never use fast-math in a scoring binary whose output is compared bit-wise or with tight tolerances. [S15][S16][S6]

- **[Documented]** Rejudges are normal: a program "may be run on multiple input files", and DOMjudge supports lazy evaluation
  (stop at first error). Nothing about scoring guarantees stability of your partial scores across runs if the code is
  nondeterministic. [S8][S4]

## 7. Integer overflow / UB checklist (our solver's arithmetic)

- **[Documented]** Signed integer overflow is undefined behavior; the compiler may assume it never happens and produce
  surprising results (loop bounds, comparisons). `-fwrapv`/`-fno-strict-overflow` opt out. [S17]

- **[Inference]** Our score function mixes microseconds and seconds (`max(completion)*1e-6`), times up to T=50000, N up to 2000,
  per-tick budgets (SIM_WORK_CAP 1.8e9), and sums over many cases. Every time/tick/product should be `int64`; the only float in
  the hot path is the final score conversion (a few doubles — fine). Watch: `depth × per-episode charge` products in the work
  ledger (already capped), and `N*T` sized loops.

- **[Documented]** Integer divide-by-zero → SIGFPE (exit 136); dereferencing OOB → SIGSEGV (139); heap corruption → SIGABRT
  (134). All three map to "runtime error" on the scoreboard, i.e. a hard zero for the whole run. [S22][S7]

- **[Documented]** Uncaught exceptions in C++ → `std::terminate` → SIGABRT → RTE (Kattis: "uncaught exception" is explicitly an
  RTE cause). Wrap the per-tick body or never throw. [S7]

## 8. Resource limits (beyond time & memory)

- **[Documented]** ICPC-style sandbox (Japan judge): 2 GB total memory, unlimited C/C++ stack (capped by total memory), single
  processor core, max 64 processes total (including system-created), source ≤ 256 KB, compile ≤ 30 s, no file opens/writes
  ("You will never have to write to (open) a file, and are not allowed to do so"), external commands "technically possible but
  probably does not work as you expect". Compile flags: `g++ -x c++ -g -O2 -std=gnu++20 -static`. [S6]

- **[Anecdotal]** Codeforces reports a "huge" judge stack — "maybe 256 MB or even more" — sized so 200k-deep DFS works. This is
  NOT universal: default Linux `ulimit -s` is 8 MB; only judges that set it (or unlimited, per ICPC) are recursion-safe. Do not
  rely on deep recursion; our iterative event loop is fine regardless. [S18]

- **[Documented]** Kattis/DOMjudge measure **total CPU of the process** — threads don't buy time, only contention. [S7][S1]
- **[Documented]** Output-storage limits (DOMjudge default 50 KB retained for display; Kattis kills flooders with OLE) —
  per-tick action logs must stay tiny; huge debug output on stderr is not judged but can still be stored/limited. [S4][S7]

- **[Documented]** ASan/UBSan builds: ~2× time and ~2× memory (ASan) — correct for bug-sweeps, never for envelope measurements
  (our crash_sweep discipline already assumes this). [S19]

## 9. Interactive protocol deadlock patterns (the ones that actually kill submissions)

- **[Documented]** Pattern 1 — *read-ahead beyond available input*: in interactive mode each read blocks until data exists or
  the judge closes input. Reading N requests when the current tick carries k<N (or reading a second line that hasn't been sent
  yet) stalls forever → TLE. Read exactly the per-tick payload, in order, and only when expected. [S5]

- **[Documented]** Pattern 2 — *forgotten flush*: your output sits in your buffer; the judge blocks waiting for it; you block
  waiting for its next input → classic mutual deadlock → TLE (judge timeout kills your process). This is the #1 documented
  interactive failure mode; the Sharm addendum calls out flushing "after each write". [S5]

- **[Documented]** Pattern 3 — *judge-side adaptivity*: "the judge program may behave in an adversarial way and adapt the input
  provided to your program based on your previous output, with the intent to discover errors." Treat the per-tick stream as
  hostile: always produce a valid answer for any feasible tick, never depend on benign patterns in the input stream. [S5]

- **[Documented]** Pattern 4 — *verdict-timing confusion*: with interactive problems "the judgement may be Wrong Answer, if the
  solution exhibits an incorrect answer before it runs out of time" — an invalid action can be judged WA *before* the TLE
  clock matters, and first-error ordering means a later crash/TLE is invisible once WA fired. Never emit a protocol-violating
  line "to make progress". [S5]

- **[Documented]** Pattern 5 — *EOF/malformed stream handling*: when the judge terminates input early (aborts the run, or a
  truncated stream), a solver that treats the failed read as data injects stale values (our Day-5 uninitialized-k bug) and can
  hang or emit garbage instead of exiting cleanly. On EOF: flush, exit rc=0 immediately. [S11][S5]

- **[Documented]** Pattern 6 — *judge time is free, yours isn't*: judge compute is not charged to you; your blocked-read time
  is wall-only. Spending CPU on long simulations between ticks is what burns the budget — bounded-lookahead disciplines
  (our sim_work ledger) are exactly right for the CPU-time contract. [S5][S7]

- **[Documented]** Pattern 7 — *output flood*: an infinite loop emitting per-tick lines → OLE kill; also "face disqualification
  if the output is interpreted as an attempt to disrupt the competition" (ICPC WF). Bound total output. [S7][S8]

- **[Inference]** Pattern 8 — *pipe back-pressure*: pipes have bounded buffers (~64 KB on Linux). If we never read stdin while
  writing large replies, the judge can block writing to us → cross-blockage. Irrelevant at our output sizes; noted for
  completeness.

- **[Documented]** Test-support pattern: judges often ship a local testing tool for interactive problems (usable via `-h`);
  it only implements "some test scenarios and only some functionality of the real judge". Our fast_judge mirrors this role. [S5]

---

## 10. Sources

1. DOMjudge manual — Judging topics (time limits, wall-clock secondary limit):
   https://www.domjudge.org/docs/manual/main/judging.html · https://github.com/DOMjudge/domjudge/blob/main/doc/manual/judging.rst

2. DOMjudge 7.2 manual — soft/hard timelimit, runguard kill condition, judging consistency (CPU scaling, Turbo Boost, ASLR):
   https://www.domjudge.org/docs/old_manual/judging.html

3. DOMjudge config reference — `timelimit_overshoot` default `'1s|10%'`, `results_prio`, `output_storage_limit`, compile output
   limits: https://www.domjudge.org/docs/manual/main/configuration-reference.html

4. DOMjudge config reference (DEV snapshot), same keys, `compile_penalty`/`lazy_eval_results`:
   https://www.domjudge.org/snapshot/manual/configuration-reference.html

5. ICPC World Finals 2023 — Addendum to Judging Notes: Interactive Problems (buffering/flushing, stall→TLE, WA-before-TLE,
   judge-time exclusion, adversarial judge, testing tool): https://docs.icpc.global/wp-content/uploads/2023/09/22-23Sharm.JudgingNotes.Addendum.pdf

6. ICPC Japan 2023 — Judging Details (2 GB total memory, unlimited stack, single core, 64 processes, 256 KB source, 30 s
   compile, no files, g++ 11.4 `-O2 -std=gnu++20 -static`, GCE c2-standard-4):
   https://storage.googleapis.com/wp-content.icpc.jp/sites/11/2023/11/judging-details.pdf

7. Kattis — List of possible judgements (RTE = non-zero exit/uncaught exception, total CPU time, OLE, malloc-NULL/new→SIGABRT
   MLE quirk, first-error stopping): https://support.kattis.com/support/solutions/articles/79000141261-list-of-possible-judgements

8. ICPC World Finals 2023 — Judging Notes (verdicts, no PE, RE includes OOM, multiple input files, tolerance-based floats,
   output-disruption disqualification): https://image.icpc.global/nac2023/NAC2023JudgingNotes.pdf

9. getrusage(2) man page — ru_utime/ru_stime/ru_maxrss semantics (Linux, sums all threads):
   https://man7.org/linux/man-pages/man2/getrusage.2.html

10. "Random Linux Oddity #1: ru_maxrss is Inherited" — maxrss inherited across execve, kernel links:
    https://jkz.wtf/random-linux-oddity-1-ru_maxrss

11. "iostream EOF misuse" — pre-C++11 uninitialized ints on failed read vs C++11 zero-init:
    https://sqlpey.com/c/solved-iostream-eof-misuse/

12. cppreference — std::sort (equal elements not order-preserved): https://en.cppreference.com/w/cpp/algorithm/sort
    (and unordered containers: https://en.cppreference.com/w/cpp/container/unordered_map)

13. Codeforces blog — interactive problems require explicit flushing (fflush/cout.flush/endl):
    https://codeforces.com/blog/entry/63707

14. SIGPIPE/Broken pipe (default termination, EPIPE when ignored): https://stackoverflow.com/questions/6824265/sigpipe-broken-pipe
    and signal(7): https://man7.org/linux/man-pages/man7/signal.7.html

15. "Beware of fast-math" — what -ffast-math/-Ofast actually do (reassociation, finite-math-only, FTZ, Kahan destroyed):
    https://simonbyrne.github.io/notes/fastmath

16. GCC Optimize Options — -ffast-math/-ffinite-math-only/-fassociative-math documentation:
    https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html

17. cppreference — C++ undefined behavior (signed overflow UB):
    https://en.cppreference.com/w/cpp/language/ub

18. Codeforces blog — huge judge stack size (~256 MB+, anecdotal): https://codeforces.com/blog/entry/118107
19. AddressSanitizer wiki — ~2× slowdown, ~2× memory overhead:
    https://github.com/google/sanitizers/wiki/AddressSanitizer

20. ICPC Challenge powered by Huawei announcement (contest dates/format context, no judge spec yet):
    https://codeforces.com/blog/entry/155646

21. uv issue on subprocess signal semantics — 128+N wait-status convention, shell vs OS level:
    https://github.com/astral-sh/uv/issues/11886

22. Signal→exit-code table (139=SIGSEGV, 137=SIGKILL/OOM, 134=SIGABRT, 135=SIGBUS, 136=SIGFPE), verified against signal(7):
    https://www.alertmend.io/blog/sigsegv-error

## Open questions to confirm against the real spec (Aug 14)

- CPU vs wall time measurement (DOMjudge-style CPU-primary is the ICPC norm; Kattis-style total-process-CPU likewise).
- Per-tick judge latency: is judge think-time charged to us?
- Memory envelope (2 GB style cap? ru_maxrss? cgroup?) and whether MLE/RTE distinguishability matters.
- Whether stdin is closed at T=end (EOF-then-exit) or kept open; how the judge signals a protocol violation (immediate kill
  vs WA).

- Per-tick input availability: strictly one line per tick, or a full-case header + streaming (affects read-ahead risk).
- Stack limit (unlimited per ICPC precedent), process/thread limits, output size limits, source size and compile-time limits.
