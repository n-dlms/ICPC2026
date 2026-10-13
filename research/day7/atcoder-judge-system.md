> **Post-contest archive note (Oct 2026):** Pre-contest preparation completed. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). This judge survey was completed pre-contest and is preserved as a historical working note. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# AtCoder Judge System — Execution & Judging Mechanics

Research date: 2026-08-06. Status of environment: the **new judge system** (launched at ABC430, 2025-11-01) is the current one; the old judge (pre-2025-11) is mentioned only where behavior changed. Facts vs community claims are labeled [OFFICIAL] / [COMMUNITY] / [INFERENCE].

---

## 1. Execution model

- Each test case runs the submitted program as a **fresh, isolated process** reading one input file from stdin and writing stdout, which the judge compares/checks. [INFERENCE — strongly supported by: (a) the official memory-test task (14 cases where case 14 is *designed to be un-ACable* — per-case verdicts must be independent, atcoder.jp/contests/language-test-202505/tasks/memorytest_b); (b) AHC per-case scoring where a TLE on one case yields 0 for that case only (atcoder.jp/contests/ahc019/tasks_print); (c) per-case status shown on submission pages.]
- Execution is **sequential per submission**; the judge runs compile once, then executes the program once per test case. No official document states this in one sentence, but all evidence above is consistent.
- Interactive tasks: the judge runs a separate judge program that exchanges lines with the submitted program via pipes (example: PracticeB "Interactive Sorting" in the language-test suite, 2 sec/1024 MiB — atcoder.jp/contests/language-test-202505/tasks; ARC218 C problem statement shows the interactive protocol — atcoder.jp/contests/arc218/tasks_print).
- First-run warm-up: status can stay WJ for 1–2 minutes on the first judging run of a contest; no effect on results. [OFFICIAL — contest rules page, e.g. atcoder.jp/contests/language-test-202505/rules]

## 2. Time limit mechanics

- **Execution time = max(real (wall-clock) time, CPU time).** [OFFICIAL — contest rules, "並列化" (parallelization) section: 「実行時間は実時間とCPU時間の大きい方で計測されます。並列化は禁止されていませんが、実行時間を短くすることには繋がりません。」("Execution time is measured as the larger of real time and CPU time. Parallelization is not prohibited, but it does not help reduce execution time.") — atcoder.jp/contests/APG4b/rules and atcoder.jp/contests/language-test-202505/rules]
- Consequences: `sleep()`/I/O-waiting counts against the limit (wall-clock side); multi-threading is allowed but useless for beating the limit (CPU-time side dominates when parallel).
- **No per-language time-limit multipliers exist.** The official rules list no multiplier table anywhere; time limits like "2 sec" apply to all languages (Python and C++ get the same limit). This differs from Codeforces-style multiplier systems. [INFERENCE from absence in official rules + uniform TL display; contrast: Codeforces does have extended TLs — codeforces.com/blog/entry/45228]
- Kill behavior (when TLE verdict fires): [COMMUNITY] the process is killed at ~1.1× the time limit (e.g. 2 s limit → killed ≈ 2200 ms; observed "2055 ms" = exceeded-but-not-killed vs "2204 ms" = killed). Author explicitly notes this is measured, not officially guaranteed — qiita.com/tomii9273/items/0a6da545670afcf5de1c.
- New judge display (from ABC430): exceeded values shown with a ">" prefix, e.g. `> 2000 ms`. [OFFICIAL — atcoder.jp/posts/1594, atcoder.jp/posts/1593]
- Reported times vary run to run; identical resubmissions can flip 0 ms ↔ a few ms (community, Qiita "AtCoderで実行時間0msを狙う" — qiita.com/kotatsugame/items/0a9a1f7b19cb5be74c12).
- Typical limits: ABC problems 2 sec (some 3–12 sec, e.g. ABC345G = 12 sec), AHC usually 3–6 sec, language-test tasks range 2–12 sec (task list: atcoder.jp/contests/language-test-202505/tasks).

## 3. Memory limit mechanics

- Memory limits are in **binary units (MiB/KiB)**, not decimal MB/KB. [OFFICIAL] Since ~ABC408 (2025-05), task pages show "1024 MiB" (unit changed from MB; community blog documents the change — yuulis.hatenablog.com/entry/atcoder-cpp-env-error-fix1). 1 MiB = 1048576 bytes.
- Typical limits: 1024 MiB (most ABC/ARC/AGC/AHC), 256 MiB (many early ABC/ATBC tasks), up to 2500 MiB (memory-test task). Examples: atcoder.jp/contests/APG4b/tasks/APG4b_cb (2 sec/256 MiB), atcoder.jp/contests/ahc001/tasks/ahc001_a (5 sec/1024 MiB), atcoder.jp/contests/language-test-202505/tasks/memorytest_b (10 sec/2500 MiB).
- **New judge MLE behavior changed (2025-11):** the process is **forcibly terminated the instant the memory limit is exceeded** (previously it was allowed to continue and only marked at the end). Display shows `> 1048576 KiB` on exceed. [OFFICIAL — atcoder.jp/posts/1594: 「新ジャッジでは メモリ制限を超えた時点で強制的に実行が打ち切られます」] Note: the announcement says depending on conditions the old judge may still be used on a given day.
- The exact measurement metric (peak RSS of the process vs cgroup memory peak) is **not officially documented**. Practical checks exist: the official "Memory Test 2" task (14 cases, escalating memory; last case un-ACable) was created precisely so participants could measure per-language memory usage and MLE behavior — see its submissions (e.g. the Factor entry in the language list carries a "workaround to avoid MLE" comment referencing submission 69450872 — atcoder.jp/contests/language-test-202505/submissions/69450872).
- Stack: judge stack size is **1048576 KiB (1 GiB)** via `ulimit -s` (checkable in code-test with the Bash language). [COMMUNITY — verified on code-test; qiita.com/reika727/items/8fa6f5944b183c0a28f9]
- Python pitfall: a `MemoryError` raised by the interpreter before actually reaching the limit is reported as **RE, not MLE**. [COMMUNITY — mem.nhiro.org/AtCoderのPythonでMemoryErrorを出すとREになる]

## 4. Sandbox and resource limits

- AtCoder does **not publish** sandbox internals (no official statement on isolate/seccomp/cgroups/containers). What is knowable:
  - Code runs in a Linux container whose image defines `/etc/atcoder/install_dir.txt` (referenced in official run scripts) and per-language install scripts — visible in the official language list (img.atcoder.jp/file/language-update/2025-10/language-list.html). The environment is therefore a **custom container image**, rebuilt per language update.
  - New-judge "kill on memory exceed" behavior is consistent with a cgroup v2 `memory.max` limit. [INFERENCE]
  - Parallelism: unlimited threads/processes allowed; useless for time (max(real, CPU)). [OFFICIAL rules]
  - Libraries: very large set available (Boost, GMP, Eigen, absl, OR-Tools/SCIP/HiGHS, PyTorch for C++, ac-library, ...) — per-language list at img.atcoder.jp/file/language-update/2025-10/language-list.html; rules link to it (atcoder.jp/contests/language-test-202505/rules).
  - Judge hardware ≈ AWS EC2 instance class (chokudai demonstrated probing CPU model via code-test). [COMMUNITY — yunix-kyopro.hatenablog.com/entry/2024/05/27/235836]
  - Contests may still run on the old judge "depending on the situation of the day". [OFFICIAL — atcoder.jp/posts/1594]

## 5. Verdict semantics

Official glossary (identical wording on all contests, e.g. atcoder.jp/contests/abc074/glossary):

- **CE** Compilation Error — compile failed. No penalty time added.
- **MLE** Memory Limit Exceeded — exceeded problem's memory limit.
- **TLE** Time Limit Exceeded — program did not finish within the limit.
- **RE** Runtime Error — error during execution (stack overflow, division by zero, ...).
- **OLE** Output Limit Exceeded — "the output size exceeded the limit specified by the problem" (note: it is per-problem, so OLE exists as a verdict; the numeric default cap is not published).
- **IE** Internal Error — judge system error.
- **WA** Wrong Answer.
- **AC** Accepted — passed all tests; the only correct status.
- **WJ** Waiting for Judging; **WR** Waiting for Re-judging (rejudge pending).

Scoring aggregation:

- Standard contests (ABC/ARC/AGC): score = **sum of the point values of AC'd problems** (配点); tiebreak = time (last AC time + 5 min × wrong submissions before each AC; CE adds no penalty). Winner = highest total within contest time. [OFFICIAL — atcoder.jp/contests/APG4b/rules]
- Scoring/heuristic tasks (AHC/マラソン): per-case scores are **summed**; each case is scored by an official scorer (special judge). For AHC019 (representative): per-case relative score = round(1e9 × (min score among all contestants for that case) / (own score for that case)); provisional = 50 cases, system test = 2000 cases; invalid output or TLE on a case → that case = 0 points and excluded from the "min" computation; system test runs **only on the last non-CE submission**; standings show relative scores (recomputed per new submission), submission list shows raw sums. [OFFICIAL — atcoder.jp/contests/ahc019/tasks_print; AHC003 rules at atcoder.jp/contests/ahc003]
- Two invalid-output policies in AHC (varies by contest): long contests → invalid case gets 0, other cases still count; short contests → any invalid case invalidates the whole submission. [COMMUNITY — qiita.com/tanaka-a/items/ab1c1f539a826606dc65]

## 6. Output-protocol rules

- **Whitespace relaxation (official):** for the correct output, replacing CR/LF/Space/Tab with other whitespace, or adding whitespace at the beginning/end/adjacent to whitespace, is still judged correct. Some problems opt out and state so in the statement. [OFFICIAL — contest rules: atcoder.jp/contests/language-test-202505/rules]
- Output size: exceeding the problem-specified output limit → **OLE** (see §5). No public default byte cap; practical guidance: keep output modest; nothing else officially specified.
- Interactive problems: must **flush stdout** after each answer (standard interactive requirement; ARC218 statement: atcoder.jp/contests/arc218/tasks_print).
- Note: `.txt` case dumps from some tools are not the interactive protocol format; interactive judge programs exchange per-turn lines.

## 7. Special judge / interactive model

- AHC/scoring tasks use a **special judge (official scorer program)** that reads input + your output and produces a per-case score (evidence: scoring formulas in statements, atcoder.jp/contests/ahc019/tasks_print). ABC/ARC/AGC use exact (whitespace-relaxed) output comparison per §6.
- Interactive: judge program runs concurrently and communicates via stdin/stdout pipes (PracticeB "Interactive Sorting" in language-test-202505, atcoder.jp/contests/language-test-202505/tasks).

## 8. Known gotchas & war stories

1. **Wall-clock counts.** Because time = max(real, CPU): `sleep()`, spin on I/O, or `usleep` in interactive solvers burns your limit; debug prints that block also count. (Official §2.)
2. **Parallelism is a trap.** `#pragma omp` etc. is allowed but cannot beat the limit (CPU-time side), and threads add overhead/instability (official §2).
3. **Kill margin:** observed kill ≈1.1× TL — never rely on the last 5–10% of the limit; runs vary run-to-run. (Community §2.)
4. **Recursion depth:** stack = 1 GiB, so deep recursion (e.g. 1e6 frames ≈ tens of MB) usually survives on AtCoder while segfaulting on a default 8 MB local stack — test locally with `ulimit -s 1048576`. (Community §3.)
5. **Memory unit trap:** "1024 MB" = 1024 MiB = 1048576 KiB; new pages display MiB/KiB. (Official/community §3.)
6. **Python MemoryError → RE**, not MLE — don't grep for MLE. (Community §3.)
7. **New judge zero-init:** C++23 builds include `-ftrivial-auto-var-init=zero` (uninitialized local vars are zeroed) and `-march=native` — uninitialized-read bugs may behave differently locally vs judge, and judge CPU ≠ your CPU. (Official language list §4; trap.jp/post/2589.)
8. **First submission warm-up:** WJ for 1–2 min at contest start is normal. (Official §1.)
9. **OLE threshold unknown** — if a task allows huge output, probe with the memory-test-style mindset; the verdict exists but the cap is per-problem and unpublished. (§5)
10. **For scoring tasks:** only your last non-CE submission is system-tested; TLE/invalid on one case = 0 for that case only (AHC019); submission interval ≥30 min in AHC003-style rules. (Official §5.)
11. Old judge may still be in use "depending on the day" (announced 2025-11). (Official §4.)

## 9. Sources

1. New judge launch (ABC430, 2025-11-01): https://atcoder.jp/posts/1594
2. New judge announcement JP (2025-10-31): https://atcoder.jp/posts/1593
3. Language update 2024–25 overview: https://atcoder.jp/posts/1342
4. Official language list (2025/10 update, incl. compile/run commands, memory placeholders, install scripts): https://img.atcoder.jp/file/language-update/2025-10/language-list.html
5. Official contest rules (time measurement = max(real, CPU); parallelization note; whitespace relaxation; penalty; WJ warm-up): https://atcoder.jp/contests/APG4b/rules / https://atcoder.jp/contests/language-test-202505/rules
6. Official verdict glossary (CE/MLE/TLE/RE/OLE/IE/WA/AC/WJ/WR): https://atcoder.jp/contests/abc074/glossary
7. Memory Test 2 task (MLE-behavior verification; 10 s/2500 MiB; un-ACable last case): https://atcoder.jp/contests/language-test-202505/tasks/memorytest_b
8. Language-test task list (interactive PracticeB; TL spread 2–12 s): https://atcoder.jp/contests/language-test-202505/tasks
9. AHC019 scoring model (relative per-case score, 50/2000 cases, invalid case = 0, last non-CE submission): https://atcoder.jp/contests/ahc019/tasks_print
10. AHC003 rules (30-min submission interval, provisional vs system test): https://atcoder.jp/contests/ahc003
11. AHC001 (5 sec/1024 MiB example): https://atcoder.jp/contests/ahc001/tasks/ahc001_a
12. TLE kill at ~1.1× limit (community): https://qiita.com/tomii9273/items/0a6da545670afcf5de1c
13. Stack size 1 GiB on judge (community): https://qiita.com/reika727/items/8fa6f5944b183c0a28f9
14. MiB/KiB unit change ~ABC408 (community): https://yuulis.hatenablog.com/entry/atcoder-cpp-env-error-fix1
15. Python MemoryError → RE (community): https://mem.nhiro.org/AtCoder%E3%81%AEPython%E3%81%A7MemoryError%E3%82%92%E5%87%BA%E3%81%99%E3%81%A8RE%E3%81%AB%E3%81%AA%E3%82%8B
16. C++ new-judge flags (GCC 15.1/Clang 20.1.3; community): https://trap.jp/post/2589/ ; install scripts: https://github.com/uni-kakurenbo/AtCoder-Language-Update_2024-11_CXX23_proposal
17. Judge hardware ≈ AWS EC2; probing via code-test (community): https://yunix-kyopro.hatenablog.com/entry/2024/05/27/235836
18. Runtime variance; 0 ms hunting (community): https://qiita.com/kotatsugame/items/0a9a1f7b19cb5be74c12
19. Interactive problem example (ARC218 C): https://atcoder.jp/contests/arc218/tasks_print
20. Codeforces extended time limits (contrast; not AtCoder): https://codeforces.com/blog/entry/45228
