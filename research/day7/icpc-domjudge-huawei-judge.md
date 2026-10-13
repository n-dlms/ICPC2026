> **Post-contest archive note (Oct 2026):** Pre-contest preparation completed. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). The "verify against the real spec" items below were reconciled at contest start (Aug 14 2026) and are preserved as historical working notes. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# How ICPC / DOMjudge / Huawei Online Challenge Judges Execute and Score Submissions

Research note for the ICPC 2026 Online Challenge powered by Huawei ("Scheduling Algorithm for Collaborative Inference System", Aug 14–28 2026). The exact judge protocol was NOT public until contest start; this note maps the *execution + scoring contract* of every known precedent — the official ICPC judging system (DOMjudge), the Codeforces-hosted ICPC Huawei challenges (2022–2025), and the Universal-Cup-hosted 2026 Huawei Cloud scheduling challenge — so the day-1 spec parse knows which assumptions to check first.

Companion note (generic judge-contract details — I/O, flushing, signals, UB, determinism): `research/day7/oj-execution-contract.md`.

Evidence grading: **[Documented]** = official docs/announcements/pages; **[Anecdotal]** = community reports; **[Inference]** = inference from documented facts; verify against the real spec.

---

## 1. ICPC official / DOMjudge execution model

- **[Documented]** DOMjudge is the official judging system of ICPC: used for World Finals, regionals, and many other contests; roughly one judgehost per ~20 teams is enough. Execution: each submitted solution is compiled once, then **run against each test case** in a sandboxed judgehost, and the output is compared to the expected output (default validator) or a problem-specific (special) judge. [S1][S2][S4]
- **[Documented]** ICPC problem packages (PPF `legacy-icpc`, the ICPC subset of the Kattis package format) define the problem-side contract: submissions **read stdin, write stdout**; test data lives in exactly two groups — `data/sample/` and `data/secret/` — used in **lexicographic order** (numbered prefixes like `00, 01, 02` guarantee order); a package must include at least one accepted judge solution; input validators (exit code 42 = valid) verify every `.in` file; output validators return **42 = accepted, 43 = wrong answer**, and *any other exit code, including 0*, means the judge program itself malfunctioned. [S5]
- **[Documented]** PPF `limits` keys (typical system defaults): `memory` 2048 MiB, `output` 8 MiB, `code` 128 KiB, `compilation_time` 60 s, `compilation_memory` 2048 MiB, `validation_time` 60 s, `validation_memory` 2048 MiB, `validation_output` 8 MiB, `time_multiplier` 5, `time_safety_margin` 2. For scoring problems the validator may write a `score.txt` into the feedback dir (Kattis convention — explicitly "though not in the ICPC"); Codeforces-hosted Huawei challenges instead implement scoring as custom judge verdicts (see §5). [S5]
- **[Documented]** ICPC WF judging notes: all input comes from standard input; a program "may be run on multiple input files" (i.e. the process is restarted per test case — stateless runs); there is **no Presentation Error**; floating-point comparison is done with tolerances by the judge; out-of-memory and other crashes are Runtime Error. [S6]
- **[Documented]** ICPC Japan 2023 judging environment (published reference): GCE c2-standard-4; **2 GB total memory** (all processes, not per-program); unlimited C/C++ stack (bounded by total memory); single processor core; max 64 processes; source ≤ 256 KB; compile ≤ 30 s; no file I/O; `g++ -x c++ -g -O2 -std=gnu++20 -static` (g++ 11.4). [S8]
- **[Inference]** For our challenge: expect the same shape — one binary, per-test-case execution, stdin/stdout, no files, custom (special) judge scoring, and a hard per-case (or aggregate, see §6) CPU budget on judge hardware we don't control.

## 2. DOMjudge time / memory / verdict mechanics

- **[Documented]** Time limits are set **per problem in seconds**, optionally scaled by a per-language time factor. Enforced "primarily in CPU time, and secondly a more lax wall clock time limit … so submissions cannot idle and hog judgehosts." A program that sleeps or blocks burns wall time, not CPU time. [S2][S3]
- **[Documented]** Soft vs hard limit: soft = problem limit × time factor; `timelimit_overshoot` (default `'1s|10%'`, i.e. max(1 s, 10% of limit)) defines the hard limit at which runguard kills the process. The **verdict is based on the soft limit** — finishing between soft and hard is still TLE (overshoot exists so organizers can see how close submissions were). [S3]
- **[Documented]** Defaults in the config reference: `memory_limit` **2 GB**, `output_limit` **8 MB** (output beyond it is truncated), `process_limit` (default 1 in recent versions — a multi-thread/process program hits the process limit, not extra time), `sourcesize_limit`, `output_storage_limit` 50 KB (stored output kept for display), `lazy_eval_results` (stop judging at the first error). [S3]
- **[Documented]** Verdicts and precedence (`results_prio`): `memory-limit`, `output-limit`, `run-error`, `timelimit`, `wrong-answer`, `no-output` are all priority 99 — **the first error encountered decides the final result**; `correct` is priority 1. No Presentation Error exists (formatting errors → WA). [S3][S6]
- **[Documented]** Special judges: a validator program is run over (input file, answer file, team output) and returns a verdict; interactive problems use a separate judge program whose stdout is piped to the solution's stdin and vice versa. [S4]
- **[Documented]** Kattis (same PPF family, same measurement style): judges "total CPU time used by your process" — threads do not buy time; an uncaught exception or non-zero exit is Runtime Error; a failed `new` → `std::bad_alloc` → SIGABRT → RTE. [S9]

## 3. Interactive judge protocol conventions

- **[Documented]** PPF interactive problems: `validation: custom interactive`. The interactor (output validator in interactive mode) is invoked as `validator input answer feedback_dir [flags] < team_output > team_input` — i.e. **everything the validator writes on stdout is piped to the submission's stdin, and the submission's stdout is piped to the validator's stdin; the submission never sees the input file directly**. `.in`/`.ans` are not shown to teams; samples are shown as `.interaction` protocol transcripts with lines marked `<` (judge→team) and `>` (team→judge). [S5]
- **[Documented]** Codeforces interactive boilerplate (the exact contract wording, quoted from an ILE-help thread): *"If the operation is a query, you should output the result of the query and flush the output stream before proceeding to the next operation. Otherwise, you may get the verdict Idleness Limit Exceed."* — i.e. **a response must be flushed before the judge sends the next operation**; not flushing or failing to respond → ILE verdict. [S10]
- **[Documented]** ILE also fires when the submission **reads more than the judge has sent**: reading a test-case count when there is only one test case, or reading ahead of the current tick, blocks forever on the pipe and the judge kills the run (classic confirmed failure in the ILE threads). [S10][S11]
- **[Documented]** ICPC WF23 interactive addendum: the time limit applies to **the submission's time only — judge-program time is not counted**; the judge may behave adversarially (adapting input based on previous output); a wrong answer can be judged *before* a later TLE would fire; flush after each write; judges ship a local testing tool ("only some test scenarios and only some functionality of the real judge"). [S7]
- **[Documented]** The 2023 Spring Buffer ICPC Huawei challenge (contest 1813) is the direct per-tick precedent: **interactive special problem**; the statement requires printing the i-th answer **before receiving the (i+1)-th operation**; flush or ILE; time limit "15 s per test". [S16]
- **[Documented]** The 2026 Huawei Cloud scheduling challenge on Universal Cup shipped `fast_judge.py` implementing exactly this shape: judge writes per-tick lines to the solver's stdin, reads one response line per tick, validates every assignment, enforces one case-wide time budget. [S18]

## 4. Huawei Online Challenge platforms

Two hosting regimes have been used by Huawei's algorithmic challenges; both are "special judge + custom scoring", but the budget accounting differs (see §6).

- **[Documented]** Codeforces-hosted ICPC Online Challenges (2022 HUAWEI, 2023 XR, 2023 Spring Buffer, 2023 Post-WF, 2025 Winter, and the 2026 edition — blog entry 155646 announces "ICPC Challenge powered by Huawei" as a Codeforces contest, id 2251): batch or interactive **special problems** with a **per-test-case time limit and per-test-case memory limit**, scored by a custom judge that reports per-case points; the contest score = sum over cases. [S12][S13][S14][S15]
- **[Documented]** Universal-Cup-hosted Huawei Cloud challenges (e.g. frankthinker/huawei-llm-scheduling-challenge, "内存受限的大模型推理优化"): **online interactive** problem with **one aggregate budget for the whole suite** — "所有测试用例合计 120 秒，内存 4 GB" (all test cases combined 120 s, 4 GB memory), per the README and postmortem. [S18]
- **[Documented]** ICPC 2025 Winter (contest 2177) concrete numbers from the contest page: Problem A "OXC Ports Arrangement Problem", **standard input/output, 5 s, 1024 MB**; contest materials zip with public tests 01–05; "special problem" (custom scoring) tag in the blog. [S13][S14]
- **[Documented]** ICPC 2023 XR (contest 1885): batch I/O, **score = sum over test cases** of a custom objective; per-case report format "Accepted [421 ms, 8 MB, 714.999 points] with total score: 16446.661" (see §5, §7); preliminary set + final set; final standings based on a re-run of the latest non-zero-scoring submission on a hidden set. [S15][S17]
- **[Inference]** The 2026 ICPC challenge is Codeforces-hosted (announcement blog + contest id 2251 already visible), so the Codeforces regime (per-test limits, sum-of-case scores, post-contest hidden system test) is the most probable shape; the aggregate-120 s regime is the main alternative to rule out on Aug 14.

## 5. ICPC 2025 Huawei challenge findings (contest 2177 — the newest precedent)

- **[Documented]** Contest page: Problem A, 5 s / 1024 MB, standard input/output. Blog announcement: ICPC 2025 Online Winter Challenge powered by Huawei. [S13][S14]
- **[Documented]** Public vs hidden data: contest materials zip contained **tests 01–05**; the problem had 23 test cases in total (participants reverse-derived "23 sets of parameters (N, S, L, M, K, P, Q1..5)"). Announcement: "N, S, L, M, K, P are parameters, they will be the same" in final tests; "N is even" added mid-contest. [S14]
- **[Documented]** Final-judging protocol (announcement, verbatim): *"After the contest ends, we will run an additional system testing on a similar but different test set. This test set is generated by the same tools with the same parameters as the contest tests, but with a different random seed. For each participant, we will take their latest submission with a non-zero score (as of the contest end) and re-test it on this hidden test set. The results on this hidden test set will be used to produce the final scoreboard."* [S14]
- **[Documented]** Per-case scoring report format observed in the 2023 threads (same machinery): "System test: #46: Accepted [421 ms, 8 MB, 714.999 points] with total score: 16446.661" — each case reports runtime/memory/points; the total is the sum. [S17]
- **[Documented]** The hidden system test is where scores actually settle: a 2025 participant fell from rank 15 to rank 52 due to two TLEs on the system test although pretest runtimes were under 3 s (worst ~4 s). [S14]
- **[Documented]** Robustness note for the 2025 edition: the platform re-checks submissions post-contest on a fresh hidden set. [S14][S17]

## 6. Aggregate-vs-per-case budget evidence

Two distinct regimes, both real, both with at least one measured instance:

| Regime                                                                                         | Evidence                                                                                                                                                                                                                                       | Example                                                  |
| ---------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------- |
| **Per-test-case time limit** (sum scores over cases; each case timed independently)            | 2022: "if you put yourself tight on the 5 seconds time limit on pretests…" [S12] · 2023 Spring Buffer: "time limit 15 s per test" [S16] · 2025 Winter: "5 s, 1024 MB" [S13] · DOMjudge: per-problem limits, first error stops judging [S2][S3] | All Codeforces-hosted ICPC Huawei challenges (2017–2026) |
| **Aggregate suite budget** (one wall/CPU budget across ALL cases; per-case score still summed) | 2026 Huawei Cloud challenge README: "所有测试用例合计 120 秒，内存 4 GB"; postmortem: "The submission UI specified 120 seconds for all test cases"; V146 retired with "-1 / Run timeout" for exceeding it [S18]                                            | Universal-Cup-hosted Huawei Cloud challenges             |

- **[Documented]** The aggregate regime is unforgiving: the V146 postmortem's root cause was that *strategy-portfolio work summed over all cases* exceeded the 120 s suite budget — a suite that passed every individual-case look blew the global wall; the fix was a deterministic work ledger (counted units per phase), `clock()` demoted to an emergency brake only, and always being able to emit a valid answer immediately. [S18]
- **[Documented]** Under per-test limits (Codeforces), the same hazard manifests as TLE-on-hidden-case: pretest runtime ~3–4 s vs 5 s limit still produced two hidden TLEs (rk 15→52). [S14]
- **[Inference]** Our harness already covers both: `fast_judge.py` (per-case run with its own timeout) and `suite_gate.py --total-timeout 120` (aggregate). The 2026 ICPC challenge being Codeforces-hosted favors the per-case regime, but the aggregate gate should stay in the toolchain until the spec says otherwise.

## 7. War stories & failure modes

- **[Documented]** Wall-vs-CPU self-measurement lottery (2023, blog 120925 comment): *"we mostly can measure the wall time, while the system measures the CPU time, which are roughly the same in most cases, but in rare cases the two can differ to an extreme rate. Unfortunately, I ran into such a problem during the system test. System test: #46: Accepted [421 ms, 8 MB, 714.999 points] with total score: 16446.661. Practice submission with exactly same code: #46: Accepted [1684 ms, 8 MB, 1192.999 points] with total score: 16918.661. The early return was worth 400+ points and a prize category for me."* — the author's improving-loop early-return was wall-clock-driven; on the (faster) system-test machine it fired after 421 ms wall, truncating the loop and the score. **Lesson: never let wall-clock drive value decisions; budget on counted work.** [S17]
- **[Documented]** Hidden-set TLE (2025 Winter): 3–4 s pretest runtimes, two hidden TLEs, rank 15 → 52. Pretest margin is not system-test margin; the hidden set is re-generated with the same parameters but a different seed (so tuning against public tests can overfit the seed). [S14]
- **[Documented]** Aggregate-suite timeout (2026 Huawei Cloud, V146): "-1 / Run timeout"; portfolio work summed across all cases exceeded the 120 s total; single-case checks had all passed. [S18]
- **[Documented]** ILE = interactive deadlock: not flushing (output sits in the stdio buffer while both sides wait) or reading ahead (blocking on a pipe the judge hasn't written to yet) → ILE; including the classic "read a test-case count when there's only one case" bug. [S10][S11][S7]
- **[Documented]** DOMjudge idle-hogging: wall-clock secondary limit exists to kill programs that wait without consuming CPU; overshoot ('1s|10%') only reports near-misses, it does not accept them. [S2][S3]
- **[Documented]** Post-contest re-judging: final standings from a re-run on a fresh hidden set [S14]; "latest submission with a non-zero score" means a broken final submission can overwrite a good earlier one.
- **[Documented]** First-error semantics everywhere: one TLE/WA/RTE anywhere → that verdict is final for the whole run; partial scores from earlier cases are not retained (except the Codeforces special-judge scoring contests, where per-case points are summed — but a TLE case yields 0 for that case). [S2][S6][S7]

## 8. Sources

1. DOMjudge manual — Overview (official ICPC judging system, judgehost ratios): https://www.domjudge.org/docs/manual/main/overview.html
2. DOMjudge manual — Judging (per-problem time limits, CPU-time primary + lax wall-clock secondary, verdicts, lazy judging): https://www.domjudge.org/docs/manual/main/judging.html
3. DOMjudge manual — Configuration reference (`timelimit_overshoot` '1s|10%', `memory_limit` 2 GB, `output_limit` 8 MB, `output_storage_limit` 50 KB, `results_prio`): https://www.domjudge.org/docs/manual/main/configuration-reference.html
4. DOMjudge wiki — How to add a special judge (validator invocation; interactive = piped program): https://github.com/DOMjudge/domjudge/wiki/How-to-add-a-special-judge
5. ICPC Problem Package Format — legacy-icpc spec (limits table, sample/secret groups, validators exit 42/43, `custom interactive`, `.interaction` files, score.txt): https://icpc.io/problem-package-format/spec/legacy-icpc
6. ICPC World Finals 2023 — Judging Notes (stdin-only, multiple input files, no PE, RE includes OOM, tolerance floats): https://image.icpc.global/nac2023/NAC2023JudgingNotes.pdf
7. ICPC World Finals 2023 — Addendum: Interactive Problems (judge time not counted, adversarial judge, WA-before-TLE, flush after each write, local testing tool): https://docs.icpc.global/wp-content/uploads/2023/09/22-23Sharm.JudgingNotes.Addendum.pdf
8. ICPC Japan 2023 — Judging Details (2 GB total memory, single core, 64 processes, 256 KB source, 30 s compile, g++ 11.4 -O2 -std=gnu++20 -static, c2-standard-4): https://storage.googleapis.com/wp-content.icpc.jp/sites/11/2023/11/judging-details.pdf
9. Kattis — List of possible judgements (total-CPU-time measurement, RTE causes, first-error stop): https://support.kattis.com/support/solutions/articles/79000141261-list-of-possible-judgements
10. Codeforces blog — "Idleness limit exceeded" (exact interactive flush boilerplate quoted): https://codeforces.com/blog/entry/58122
11. Codeforces blog — "Interactive Problem Idleness Limit Exceeded!" (read-ahead/one-test-case ILE cause): https://codeforces.com/blog/entry/111074
12. Codeforces blog — ICPC 2022 Online Challenge powered by HUAWEI ("5 seconds time limit on pretests" quote, contests 1723/1724): https://codeforces.com/blog/entry/105097
13. ICPC 2025 Online Winter Challenge contest page (5 s / 1024 MB, problem-materials zip, announcements incl. post-contest re-test protocol): https://codeforces.com/contest/2177
14. Codeforces blog — ICPC 2025 Online Winter Challenge powered by Huawei (23 tests, parameter lists, TLE rank-drop war story, cheating clampdown): https://codeforces.com/blog/entry/148541
15. Codeforces contest — ICPC 2023 Online Challenge (XR scheduling; "Your program must read all test data" statement excerpt): https://codeforces.com/contest/1885
16. Codeforces contest — ICPC 2023 Online Challenge powered by Huawei (Spring Buffer, interactive per-tick, 15 s per test): https://codeforces.com/contest/1813
17. Codeforces blog — ICPC 2023 Online Challenge powered by Huawei (system-test report format "Accepted [421 ms, 8 MB, 714.999 points] with total score: 16446.661", wall-vs-CPU quote, plagiarism detection): https://codeforces.com/blog/entry/120925
18. frankthinker/huawei-llm-scheduling-challenge (README: 120 s aggregate + 4 GB; docs/postmortem.md: V146 "-1 / Run timeout", work-ledger fix; tools/fast_judge.py per-tick interactive judge; tools/suite_gate.py aggregate gate): https://github.com/frankthinker/huawei-llm-scheduling-challenge
19. Codeforces blog — ICPC Challenge powered by Huawei (2026 edition announcement; contest 2251; spec unpublished as of Aug 6 2026): https://codeforces.com/blog/entry/155646
20. Companion generic-contract note (I/O, flushing, signals, UB, determinism, open questions): `research/day7/oj-execution-contract.md`

---

## Bottom line for Aug 14

- Most probable contract (Codeforces-hosted, per §4–§6): **one binary, standard input/output, per-test-case time limit (~5 s) and memory (~1024 MB), custom special judge scoring, score = sum over cases, public pretests (01–05) + hidden re-generated final set, final standings from re-testing the latest non-zero submission**. Keep `suite_gate --total-timeout 120` around anyway: the sister platform proved the aggregate-120 s regime exists, and the V146 postmortem is the canonical failure mode.
- Never gate on wall clock; budget counted work; be able to emit a valid answer immediately (per V146 and the 2023 wall-vs-CPU loss).
- Read exactly what the judge sends, flush every response, exit rc=0 on EOF (per §3 and the ILE/deadlock war stories).
