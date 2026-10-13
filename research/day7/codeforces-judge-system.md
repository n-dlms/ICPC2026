> **Post-contest archive note (Oct 2026):** Pre-contest preparation completed. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). This judge survey was completed pre-contest and is preserved as a historical working note. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# Codeforces Judge System — Mechanics Research

Research date: 2026-08-06. Scope: how the Codeforces online judge actually executes, measures, and judges submissions (batch and interactive). All facts labeled `[documented]` (official rules/announcements by MikeMirzayanov / CF staff / Polygon/testlib docs) or `[community]` (user-measured observations, expert write-ups, hypotheses). Everything below is about the CF judge, not any specific contest.

---

## Execution model

- **One fresh process per test.** Official rules: "Judging is performed by consecutively running the program on each test" and the time/memory limits are "maximum … a program can take to work on **one test**". A failure shows as "WA on test 5", "TLE on test 31" etc., i.e. each test is an independent run; nothing persists between tests. `[documented]` — https://codeforces.com/blog/entry/4088
- **Stdio batch protocol.** Input is fed to the program's stdin (or a named file when the statement says so, e.g. `input.txt`/`output.txt`); output is captured from stdout and compared by a checker. Programs "should only interact with the console". `[documented]` — https://codeforces.com/help (FAQ), https://codeforces.com/blog/entry/4088
- **Pretests then system tests.** During a contest, submissions run on a small set of pretests; after the contest, all pretest-passing, unhacked submissions run on the full main test set ("system testing"). Hacks add tests. `[documented]` — https://codeforces.com/blog/entry/4088
- **Windows OS is the execution platform.** Rules (2010): "Solutions are compiled and run under Windows OS"; still true today — all current compile/run command lines use `.exe`, `cl.exe`, `editbin64.exe`, `msys2`, MinGW-w64 (`[documented]` https://codeforces.com/blog/entry/121114, updated 2025). Consequence: Linux-isms (`/dev/null`, `sched_getcpu`, `getrusage`, `/proc`) do not work as on Linux. `[community]` — e.g. https://codeforces.com/topic/78013/en1
- **Historically a 32-bit sandbox.** "CF runs its submissions on a 64 bit computer — however … it actually runs those submissions within a 32 bit sandbox" (2019, about the 32-bit compilers; the newer G++20/G++23 64-bit compilers run 64-bit binaries but MinGW-w64 still uses the LLP64 model, so `sizeof(long)==4` even in 64-bit mode). `[community]` — https://codeforces.com/topic/78013/en1
- **Custom Invocation** uses the same compilers and the same runner; it does not know about interactors for interactive problems. `[documented]` — https://codeforces.com/blog/entry/45307

---

## Time limit mechanics

- **Stated time limit is per test** ("time limit per test", e.g. 1 s / 2 s); the process is run once per test with that test's input. `[documented]` — https://codeforces.com/blog/entry/4088
- **Measured time is CPU time of the solution process, not wall-clock.** On Windows the natural accounting is `GetProcessTimes` (kernel+user, summed over all threads; "this value can exceed the amount of real time elapsed … if the process executes across multiple CPU cores"). Community measurement on CF (neal, 2020): `clock()`, wall time and `std::chrono::steady_clock` all agreed on ~1.0 s wall while CF reported "Used: 1014 ms" — and in ~1 of 10 runs CF reported **half** the wall time. Implication: sleeping/I/O waits don't help (CPU time only), and multithreading does not stretch the budget — the summed CPU time of all threads counts. `[community, consistent with Windows API docs]` — https://codeforces.com/blog/entry/85677, https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes
- **"Compatibility mode" / effective 2×TL cutoff.** Official announcement (2012, on ~2× faster hardware): "all the time constraints before the start of the program are divided into 2, the program is executed, and at the end a running time is multiplied by 2." I.e., the process is allowed to run up to **2×TL of real time** before being killed, and the reported/scored time is scaled back by 0.5 against the stated TL. `[documented]` — https://codeforces.com/blog/entry/8457
- **Heterogeneous judge pool.** neal's 2020 experiment replicated the 2× effect on ~1/10 of runs (older, ~2× slower machines; on those, 1 s of real CPU = "0.5 s" of budget), and links it to "Attention" notes on old problems that the runtime is reported doubled (e.g. 1C Ancient Berland Circus). Consequence: a solution that relies on running right up to `TL` (e.g. iterating until `clock()` hits 3.8 s) can WA/TLE/behave non-deterministically. `[community hypothesis, careful replication]` — https://codeforces.com/blog/entry/85677
- **Reported time granularity ≈ 15 ms.** "The machine tester exhibits time resolution about 15 ms"; fastest submissions all show 15 ms, others k·15 ms (2013). `[community]` — https://codeforces.com/blog/entry/7470
- **No per-language multiplier in official rounds.** CF rounds use the same TL for every language; PyPy/Python/Java get no extra time by default. `[documented by absence; community consensus]` — discussion e.g. https://codeforces.com/blog/entry/45228, https://codeforces.com/blog/entry/55177
- **Optional per-contest "time limits scaling policy"** exists for groups/gym/mashups: when an admin enables it, multipliers apply — C# 1.5, F# 1.5, Java 1.5, Kotlin 1.5, Scala 2.25, Haskell 3, PyPy 2/3 3, JavaScript/Node 3, PascalABC.NET 3, Perl 5, PHP 5, Python 2/3 5, Ruby 3 5 — capped at 30 s. (Note: the widely-quoted "Java 2×/PyPy 2×" folklore does not match this official table, which is Java 1.5×/PyPy 3× when enabled.) `[documented]` — https://codeforces.com/group/MWSDmqGsZm/contest/219774/extraTimeFactors
- **Interactive problems:** the interactor (jury program) runs in parallel with the solution; responses are delivered only after a flush. How Polygon/CF exactly attribute time between solution and interactor is officially acknowledged as murky ("I still don't understand how Polygon and Codeforces measure time for interactive problems" — problem-setter of Good Bye 2018 G). `[documented ambiguity]` — https://codeforces.com/blog/entry/64203
- **Historical judge hardware:** Core 2 Duo E6750 2.66 GHz / 3 GB (2010 rules) → i5-3470 boosted to 3.5 GHz ≈ 2× (2012). Community rule of thumb "≈1e8 simple ops/s" is folklore, not official. `[documented]` — https://codeforces.com/blog/entry/4088, https://codeforces.com/blog/entry/8457

---

## Memory limit mechanics

- **Stated memory limit is per test** (typically 256 MB, statement per problem). `[documented]` — https://codeforces.com/blog/entry/4088
- **Measured as peak committed memory (pagefile usage), not working set/RSS.** CF-ecosystem write-up "Accurate memory accounting for programming contests": `PeakWorkingSetSize` is the wrong metric (working set is physical pages currently resident; swapped-out allocations are invisible); on Windows the correct counter is **`PeakPagefileUsage`** (commit size), because Windows is a no-overcommit system — every committed page is backed by the pagefile, so commit == "real" memory usage of the process. `[documented (CF-admin-era technical post)]` — https://codeforces.com/blog/entry/6
- **Custom Invocation/judge report format is "Used: X ms, Y KB"** (KB). `[community]` — e.g. https://codeforces.com/blog/entry/7508, https://codeforces.com/blog/entry/85677
- **Java memory is capped explicitly.** JVM run line: `-XX:+UseSerialGC -XX:TieredStopAtLevel=1 -XX:NewRatio=5 -Xms8M -Xmx{MEMORY_LIMIT_MB}M -Xss64M`; a javaagent intercepts `OutOfMemoryError` and converts it to an **MLE verdict** (does not affect StackOverflowError); a hard per-process limit ≈ **ML + 20 MB** is also enforced because of off-heap allocations (20 MB ≈ footprint of a nearly empty JVM). With `-XX:NewRatio=5` and ML=256M, the tenured (old) generation is ~200 MB, so you cannot allocate a single 210 MB array. `[documented]` — https://codeforces.com/blog/entry/43696, https://codeforces.com/blog/entry/121114
- **Stack is huge for C/C++:** all GNU C++/C/D lines link with a 256 MB stack reservation (`-Wl,--stack=268435456` / `-L/STACK:268435456`; Rust likewise `/STACK:268435456`; Haskell runs `+RTS -K256m -A8m -RTS`; Ruby sets `RUBY_THREAD_VM_STACK_SIZE=268435456`; C# uses `editbin64 /stack:67108864` = 64 MB). So deep recursion is fine up to ~256 MB on GNU C++, and stack overflow → RE, not MLE. `[documented]` — https://codeforces.com/blog/entry/121114
- **32-bit compilers constrain address space**: with the historical 32-bit toolchains, the process address space is ~2 GB, so allocations near 2 GB fail even if ML allowed more; `long` is 32-bit even on the 64-bit MinGW builds. `[community]` — https://codeforces.com/topic/78013/en1
- **Compiled executable size cap: 32 MiB.** "Can't compile file: Compiled file is too large [34412832 bytes], but maximal allowed size is 33554432 bytes" — triggered by template-heavy code / large global arrays with initializers; also "Compilation process timed out" exists as a compile failure mode. `[documented, multiple sightings]` — https://codeforces.com/blog/entry/67156, https://codeforces.com/blog/entry/7263

---

## Sandbox + resource limits

- **No public sandbox name** (Codeforces does not use "isolate"/runc/seccomp — those are Linux judges; CF runs on Windows). The CF-ecosystem description: each submission runs as a separate low-privilege account under a Windows Job Object; CPU time and memory read via `GetProcessTimes` / `GetProcessMemoryInfo`. Job objects are used for isolation/accounting, not for hard memory denial (a job-object memory cap makes `malloc` return NULL, which CF explicitly rejected as a mechanism). `[documented (CF-ecosystem technical post) + standard Windows OJ practice]` — https://codeforces.com/blog/entry/6, https://www.infinitescript.com/project/verwandlung-online-judge/
- **Explicitly forbidden actions (rules):** network access, spawning other programs/processes, writing outside the current directory, changing file/dir permissions, registry access, GUI elements, external devices, anything that destabilizes judging. Programs may only do console I/O (or the files named in the statement). `[documented]` — https://codeforces.com/blog/entry/4088
- **File I/O is possible in the working directory** — some problems require reading/writing files (e.g. `input.txt`/`output.txt`). A classic failure: solving such a problem with stdin and getting MLE/WA on the judge while "Custom test" looks fine. `[documented via war story]` — https://codeforces.com/blog/entry/7508
- **Multithreading is not blocked** and does help wall time on CF (user experiments: 9 threads ≈ half the wall time of 2 threads). But since the TL is **CPU time summed across threads**, parallelizing a compute-bound loop consumes budget at the same total rate — no free TL. `[community]` — https://codeforces.com/blog/entry/56034; measurement semantics https://codeforces.com/blog/entry/85677
- **Resource-limit APIs** (`setrlimit` etc.) are Linux-only; they are irrelevant inside the CF Windows sandbox. `[context]` — https://codeforces.com/blog/entry/82541
- **Custom Invocation** runs in the same restricted environment as judging (same compiler, same runner; user-verified identical behavior between submission and custom run; results for identical input are **cached** — change the input to force a re-run). `[community]` — https://codeforces.com/blog/entry/85677, https://codeforces.com/blog/entry/43941

---

## Verdict semantics

Official verdict table (rules): `[documented]` — https://codeforces.com/blog/entry/4088

| Verdict                 | Meaning (official)                                                                                                                                                                                             |
| ----------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Memory limit exceeded   | Program consumes more memory than stated (per test)                                                                                                                                                            |
| Time limit exceeded     | Program didn't terminate within the stated time (per test)                                                                                                                                                     |
| Runtime error           | Program terminated with a non-zero return code (array out of bounds, division by zero, stack overflow, bad pointers, …)                                                                                        |
| Wrong answer            | Wrong answer                                                                                                                                                                                                   |
| Idleness limit exceeded | Program didn't use CPU time for a considerable time (interactive)                                                                                                                                              |
| Denial of judgement     | Solution couldn't be run / judging-side failure (e.g. malformed binary from huge Delphi arrays); historically "verdicts like Compilation failed, Denial of judgement, Judgement failed are ignored in scoring" |

- **A test passes only if: exit code 0 + within time + within memory + correct output.** `[documented]` — https://codeforces.com/blog/entry/4088
- **RE ↔ exit codes / signals:** on Windows there are no Unix signals; crashes surface as NTSTATUS exit codes, all shown on the submission page and all mapping to RE:
  - `-1073741819` = 0xC0000005 STATUS_ACCESS_VIOLATION (segfault-equivalent) — the most common RE on CF. `[community, very common]` — https://codeforces.com/blog/entry/128865, https://codeforces.com/blog/entry/116537, https://stackoverflow.com/questions/66076949
  - `exit code 1` also → RE (e.g. `return 1` or uncaught exception). `[community]` — https://stackoverflow.com/questions/61087436
  - Other NTSTATUSes in the same family: 0xC0000094 (divide by zero), 0xC00000FD (stack overflow), 0xC000001D (illegal instruction), 0xC0000005 (access violation). `[community inference from rules' list of causes]`
- **MLE vs RE for OOM:** for native code, exceeding ML = measured commit > ML → MLE (checker never runs); an allocation failure that the code mishandles (e.g. `vector` throwing `bad_alloc` uncaught in C++) → RE. For Java, heap exhaustion → MLE (javaagent intercepts OOMError); StackOverflowError → RE, not MLE. `[documented]` — https://codeforces.com/blog/entry/43696
- **No "Presentation Error" verdict** — format mismatches are Wrong Answer. `[documented]` — https://codeforces.com/help (FAQ), https://codeforces.com/blog/entry/18431
- **CHALLENGED** = a submitted solution that was successfully hacked; it is excluded from system-test scoring ("haven't been hacked" is a system-test eligibility criterion). `[documented]` — https://codeforces.com/blog/entry/4088
- **Denial of judgement** in practice = judge-side failure (checker/interactor/test crash, broken tests), often problem-wide until fixed; the submission page may show a test with verdict "CRASHED". `[community]` — https://codeforces.com/blog/entry/7517

---

## Output/protocol rules

- **stdout is buffered; flush semantics matter only for interactive** problems (data is transferred to the interactor only on flush; `cout << endl` flushes; forgetting to flush → ILE). `[documented]` — https://codeforces.com/blog/entry/45307
- **No official output-size limit is published.** The rules (2010) do not list an OLE verdict; modern CF statements never state an output cap; there is no public CF documentation of an "Output Limit Exceeded" threshold (contrast: DMOJ documents ~256 MB, POJ 2× expected-size — those are *not* CF). Practical guidance: keep output far below tens of MB; an infinite print loop will eventually be stopped by the TL/2×TL cutoff (printing is fast and burns CPU). `[documented by absence; labeled]` — rules https://codeforces.com/blog/entry/4088; non-CF analogies (context only) https://dmoj.ca/about/codes/, http://poj.org/faq.htm
- **stderr:** the sandbox captures program output (stdout) for the checker; there is no documented public display of stderr on verdict pages. Do not rely on stderr being shown; on CF the checker only sees stdout. `[community consensus; undocumented formally]`
- **Output is compared by a checker program** (see below); whitespace tolerance depends on the checker (standard checkers ignore trailing whitespace/newlines). `[documented]` — https://codeforces.com/blog/entry/18431
- **Custom Invocation** shows "Used: X ms, Y KB" and caches results per exact input. `[community]` — https://codeforces.com/blog/entry/85677

---

## Checker / interactive model

- **Checker model (testlib):** a checker is a separate program run with three arguments: test-input file, contestant-output file, jury-answer file. It returns a verdict via exit code / testlib `quitf`: `_ok`, `_wa`, `_pe` (on Codeforces `_pe` is treated as WA), `_pc(score)` (partial scoring, integer 0–200; "Partially Correct"), `_fail` (checker/jury-side error → investigated by jury; also used when the contestant found a better answer than the jury's). All read functions enforce ranges and EOF/EOLN; strictness is checker-controlled. `[documented]` — https://codeforces.com/blog/entry/18431, https://github.com/MikeMirzayanov/testlib
- **Validators** check the test input itself (`registerValidation`; wrong test → non-zero exit → problem is broken). `[documented]` — https://github.com/MikeMirzayanov/testlib
- **Interactive model:** jury writes an interactor (`registerInteraction`); the interactor's stdout feeds the solution's stdin, the solution's stdout feeds the interactor's stdin; the interactor writes the interaction transcript to `tout`, which the checker verifies afterwards; the interactor can terminate the interaction early with `quitf(_wa, …)`. The solution must flush after every query; a solution that stays silent (or forgets to flush) gets **ILE**; exceeding the query limit / invalid queries → interactor sends a poison value (usually `-1` or `0`), and the solution **must exit immediately**, otherwise "you can get an arbitrary verdict because your solution will continue to read from a closed stream". `[documented]` — https://codeforces.com/blog/entry/45307, problem statements e.g. https://codeforces.com/problemset/problem/1713/D, https://codeforces.com/problemset/problem/1990/E2
- **ILE timing:** no numeric timeout is published ("didn't use CPU time for considerable time"). Known community observations: silence during interaction eventually yields ILE rather than TLE. `[documented (definition) / community (mechanics)]` — https://codeforces.com/blog/entry/4088
- **Interactor runtime hazards are real:** Good Bye 2018 G — an interactor bug (slow big-integer GCD) caused mysterious "30 ms" solutions to TLE/ILE; the setter admits CF-side time accounting for interactive problems is unclear; query-limit errors (setting limit 100 when 1000 needed) made it worse; Polygon imposes its own CPU quota on interactors (5-minute lockout). Lessons: interactive TL budgets are consumed by *both* sides, and interactor bugs surface as bizarre solution verdicts. `[documented war story]` — https://codeforces.com/blog/entry/64203
- **Partial scoring:** testlib supports `_pc(score)` (0–200); Codeforces rounds are otherwise binary-pass/fail with time-based points. `[documented]` — https://codeforces.com/blog/entry/18431

---

## Known gotchas & failure-mode war stories

1. **`clock()`-bounded solutions fail non-deterministically on CF** (neal): ~1/10 runs land on a ~2× slower judge machine; CF then halves the reported time and gives effectively 2× TL, so a loop "until clock() > 3.8 s" does only half the intended work → WA; same code passes in Custom Invocation. Also: Custom Invocation caches results per identical input (change the input when re-testing). `[community, careful experiment]` — https://codeforces.com/blog/entry/85677
2. **Solution works locally, RE with `exit code: -1073741819 (STATUS_ACCESS_VIOLATION)` on CF** — almost always UB: out-of-bounds, negative index, stack issues; the Windows toolchain/`-O2` catches what local IDEs hide; debug via Custom Invocation + sanitizers. `[community]` — https://codeforces.com/blog/entry/128865, https://codeforces.com/blog/entry/116537
3. **File-I/O problems (input.txt/output.txt) MLE/WA if you use stdin** — judging feeds files, not stdin; "Custom test" runs the same binary so it can look identical. `[community]` — https://codeforces.com/blog/entry/7508
4. **Compiled binary > 32 MiB → "Compilation error"** (not an execution verdict): template metaprogramming, huge initialized global arrays; move initializers into `main`/`std::fill`. Compile timeouts also possible. `[documented]` — https://codeforces.com/blog/entry/67156, https://codeforces.com/blog/entry/7263
5. **Multithreading works** (no process/thread ban) but does not stretch the TL — time is summed CPU time across threads, so brute-force parallelism burns the budget at the same rate. `[community]` — https://codeforces.com/blog/entry/56034, https://codeforces.com/blog/entry/85677
6. **`sizeof(long) == 4` even on CF's 64-bit G++** (MinGW-w64 LLP64); pointers/`size_t` are 8 bytes; `bitset` and other libstdc++ internals keyed on `long` are artificially limited. `[community]` — https://codeforces.com/topic/78013/en1
7. **Windows semantics leak through:** no `/dev/null` (use `NUL` or ignore), `sched_getcpu`/`getrusage`/`setrlimit` unavailable or shims, `clock()` in MinGW is CPU time (MSVC's `clock()` is wall — don't port MSVC habits). `[community + platform docs]` — https://codeforces.com/blog/entry/82541
8. **Interactive: forgetting flush → ILE; reading after `-1` → arbitrary verdict.** Must `exit(0)` immediately on poison input. `[documented]` — https://codeforces.com/problemset/problem/1713/D
9. **Java: uncaught `OutOfMemoryError` → MLE, not RE** (javaagent), and the single largest allocation is capped by the tenured generation (~200 MB at ML=256 MB with `NewRatio=5`), not the full heap. `[documented]` — https://codeforces.com/blog/entry/43696
10. **The 2×TL real cutoff (compatibility mode)** means a program can run up to ~2×TL before the kill — so near-TL code may *appear* to work on a slow test and TLE unpredictably across the judge pool. `[documented]` — https://codeforces.com/blog/entry/8457

---

## Sources

1. Codeforces Contest Rules (verdict table, per-test judging, Windows, 64 KB source, forbidden actions, DoJ) — https://codeforces.com/blog/entry/4088
2. Codeforces Help/FAQ (no Presentation Error; testing-system notes) — https://codeforces.com/help (FAQ post https://codeforces.com/blog/entry/1336)
3. About the programming languages (old compile lines, Windows, stack 256 MB) — https://codeforces.com/blog/entry/79
4. Codeforces Command Lines 2023-10-06 (current compile/run lines incl. Java flags, Rust, C#, Haskell, Ruby) — https://codeforces.com/blog/entry/121114
5. Codeforces: new computers for testing and testing compatibility mode (TL halved/doubled mechanism, i5-3470) — https://codeforces.com/blog/entry/8457
6. Problems with clock timing on Codeforces (CPU-time accounting, 2× slower machines, custom-invocation caching) — https://codeforces.com/blog/entry/85677
7. Accurate memory accounting for programming contests (PeakPagefileUsage vs working set, job objects, Java agent) — https://codeforces.com/blog/entry/6
8. Codeforces: Java command lines have been changed (javaagent → MLE, hard limit ML+20 MB, NewRatio=5) — https://codeforces.com/blog/entry/43696
9. Checkers with testlib.h (checker model, verdicts incl. _pc/_pe/_fail) — https://codeforces.com/blog/entry/18431
10. Interactive Problems: Guide for Participants (flush, ILE, interactor model) — https://codeforces.com/blog/entry/45307
11. Good Bye 2018 Problem G post-mortem (interactive time measurement ambiguity, interactor TLE, Polygon CPU quota) — https://codeforces.com/blog/entry/64203
12. About Time Scaling (official per-language multiplier table for contests that enable it; 30 s cap) — https://codeforces.com/group/MWSDmqGsZm/contest/219774/extraTimeFactors
13. Resolution of execution time (~15 ms time granularity) — https://codeforces.com/blog/entry/7470
14. Compilation Error: "Compiled file is too large" (32 MiB binary cap, compile timeout, global-init fix) — https://codeforces.com/blog/entry/67156
15. a fault in memory limit (compiled size cap report; file-I/O MLE confusion) — https://codeforces.com/blog/entry/7263
16. Memory limit exceeded? How? (Custom Invocation "Used: 0 ms, 55404 KB"; file I/O gotcha) — https://codeforces.com/blog/entry/7508
17. Should Multi-threading be disabled on codeforces server? (multithreading actually runs and speeds up wall time) — https://codeforces.com/blog/entry/56034
18. How to get actual 64 bit bitsets on Codeforces (32-bit sandbox, LLP64 long=4 bytes) — https://codeforces.com/topic/78013/en1
19. What is denial of judgement? (DoJ = judge-side crash; "CRASHED" in report) — https://codeforces.com/blog/entry/7517
20. exit code: -1073741819 (STATUS_ACCESS_VIOLATION) — https://codeforces.com/blog/entry/128865 ; RUNTIME ERROR: exit code: -1073741819 — https://codeforces.com/blog/entry/116537 ; StackOverflow Q&A (exit code 1 → RE) — https://stackoverflow.com/questions/66076949, https://stackoverflow.com/questions/61087436
21. Interactive problem statements (ILE/poison-value protocol) — https://codeforces.com/problemset/problem/1713/D, https://codeforces.com/problemset/problem/1990/E2
22. testlib repository (checker/interactor/validator APIs) — https://github.com/MikeMirzayanov/testlib
23. GetProcessTimes API (kernel+user CPU, can exceed wall) — https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes
24. Verwandlung OJ write-up (standard Windows OJ pattern: low-priv account + Job Object + GetProcessTimes/GetProcessMemoryInfo) — https://www.infinitescript.com/project/verwandlung-online-judge/
25. Non-CF output-limit references for context (DMOJ ~256 MB; POJ 2× expected) — https://dmoj.ca/about/codes/, http://poj.org/faq.htm
