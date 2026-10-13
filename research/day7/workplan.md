> **Post-contest archive note (Oct 2026):** Pre-contest preparation completed. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). Checklists, TODOs, and "to verify / will / pending" items below are preserved as historical pre-contest working notes. Anything still open is a Post-contest follow-up (not part of the #56 run). Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# Day 7 — Workplan: Judge Survival System

Status: **completed pre-contest (design locked, executed 2026-08-06; contest ended Aug 28 2026)**. Owner: solver engineering. Inputs: the four research notes in this directory + the frozen winner `build/solution_evt_cap4_asort_frozen` (wall 1.51 s min on n1000_t14000; probable spec ~5 s / 1024 MB per test, CPU-time primary).

## Objective

Survive every plausible execution contract. Score was already frozen and excellent; Day 7 made the *execution contract* a first-class property. Zero tolerance for: TLE/RE/OOM/WA-via-protocol/UB/EOF-hang/deadlock/nondeterminism. The judge is part of the problem.

## Work streams (in execution order)

### A. Audits (code reading + bounded testing, ~1 day)

**A1. Integer max-value audit** — every `int`/`long long` field, every product/sum accumulator. For each: what is the max mathematical value under plausible spec bounds, and what C++ type stores it?

- [x] Field types identified: `Req` uses `int` for `A, Lin, Lout, D, phase, remain, tokens_left, start, completion`; top-level params all `int`.
- [x] `prio_afterlife` (line 178): `(2LL * Lin + Lout + 1) * (long long)Lout` — at Lin≈Lout≈2e9 the product ≈1.2e19 > LLONG_MAX (9.22e18) → **overflow/UB at extreme values**. Realistic spec values (Lin/Lout ≤ ~1e6?) are safe; MUST re-audit against the real spec's bounds on Aug 14.
- [ ] `score_state` vsum accumulation: per-request contributions × N — check magnitude.
- [ ] `est = t + pe + COMM + pc + COMM + Lout*per` chains: per = PEDGE+COMM+PCLOUD+COMM; with Lout=1e6, per~1e3 → 1e9 — fine in LL; verify with spec bounds.
- [ ] `T` as `int` with T ≤ 1e6 — fine; if spec allows T > 2.1e9 → type change needed.
- [ ] UBSan sweep already covers signed overflow on all tested inputs; the audit covers *untested* input shapes.

**A2. Memory-with-time audit** — which structures grow with time, not live state? (Essay VI.)

- [ ] `dw_list`: compacted in `advance_all` real path (Day-3); sim path re-uses `seen_c` dedup. Verify worst-case bound under decode-saturated zoo.
- [ ] `wait_sorted`/`wait_list`: grown at arrival, entries removed only implicitly (filtered at use). **Never shrinks** — worst-case N=2000 → ~2000 ints ≈ 8 KB. Safe.
- [ ] `inflight`: in-place compaction — bounded by live in-flight. Safe.
- [ ] `seen`/`seen_c`/`seen_sel`: sized to N (ints). Safe.
- [ ] `edge_occ`/`cloud_occ`: sized T+spare. **T=1e6 → 2×4 MB = 8 MB. T=5e6 → 40 MB.** Must check spec T bound; at T=1e6 fine under 1024 MB.
- [ ] Journals/rollback: per-candidate `sim_reqs = base_reqs` copy (EXP-28: ~1% — negligible).
- [ ] Run stress_max at N=2000/T=50000 and record peak RSS; verify far below 1024 MB.

**A3. Output-size bound** — DOMjudge default output cap 8 MB; CF unknown. Max output = Σ ticks (1 + actions×~8 bytes). With Ecap/Ccap ≤ 6 and T=50000: ~50k × (2+6×8) ≈ 2.5 MB — safe. Verify with a decode-saturated max-T case measuring actual stdout bytes. Note: judge reads actions per tick; our per-tick flush means the cap is per-tick effectively, not total — but keep total < cap.

**A4. Flush/read-ahead audit** — [x] `cout.flush()` at line 1237 per tick (verified). Read protocol: reads exactly k + k×4 ints per tick — no read-ahead (no `while(cin)` on the request stream). The END/EOF handling: `int k = 0; if(!(cin >> k)) break;` — exits cleanly on EOF. Good.

### B. Tooling hardening (~2-3 hrs)

**B1. CPU-time in contest_runner** — add `ru_utime + ru_stime` (via `resource.getrusage(RUSAGE_CHILDREN)`) to the per-case report alongside wall. The official metric is CPU-primary (DOMjudge/CF research); we currently only report wall, which is pessimistic. Gate warn/hard fractions on CPU as primary, wall as backstop.

**B2. limits.yaml** — keep placeholders; comment the probable spec values (5 s / 1024 MB) so Aug-14 spec-parse is one edit.

### C. Adversarial battery (~1 day)

**C1. Mechanism-driven degenerate zoo** — extend/verify stress_max covers: all-identical requests (tie storm), max decode rounds (Lin=1, Lout=max → longest chain, most DW re-entry), deadline collisions (all same D), arrival burst (all at tick 1), max active (E,C small, B large), reverse-sorted deadlines, duplicate timestamps, N=1, empty.

**C2. Runtime adversary** — re-run `runtime_stress.py` on the frozen binary; record max wall. Target: nothing > ~0.15 s on any adversarial input shape.

**C3. I/O adversarial** — dribble/paused-reader/slow-judge rehearsal (EXP-29 battery exists) — re-run against frozen.

### D. Spec-day readiness (~1-2 hrs, all written)

**D1. Day-1 checklist** — the exact steps for Aug 14: parse spec → fill timing-regime matrix (research/day7/README.md §2) → update limits.yaml → reconfigure runner → re-run release gate → verify protocol shape (per-tick vs batch) against fast_judge → freeze/re-freeze decision.

**D2. Re-verify type bounds against the actual spec** (A1 follow-through): N, T, Lin, Lout, PSCALE, COMM bounds → type audit conclusions.

### E. Release gate formalization (~1 hr)

**E1. Release-gate run on the frozen binary** — one command sequence, all gates, recorded numbers:

1. suite 20 (fast_judge) — validity + score baseline
2. gap 20 — class tracking
3. crash_sweep 3000 (release) + ASan/UBSan 500 (sanitized)
4. suite_gate --total-timeout 120 (aggregate regime hedge)
5. runtime_stress (adversary)
6. stress_max (zoo incl. T=50000)
7. scaling_bench (10..2000)
8. metamorphic (shift + determinism)
9. rehearsal battery (I/O lifecycle)
10. contest_runner full pool with CPU+wall reporting (B1)

**E2. Record** results in research/day7/README.md §4 as the Day-7 baseline table.

### F. Promotion criteria (documented)

Promote to the submission slot ONLY if: all E1 gates pass; wall+CPU ≤ ~0.5× of the spec-confirmed limit on every adversarial case; RSS ≤ ~0.5× of memory limit; no sanitizer findings; score byte-identical to frozen or strictly better with no class regression. Never submit an experimental build ("latest non-zero submission" trap).

## Key research-derived constraints honored (from the four notes)

- Never let wall-clock drive value decisions → work ledger stays (V146 lesson).
- CPU-time primary → B1 measurement is essential, not nice-to-have.
- Per-test process model → per-case statelessness; no cross-case state.
- Sum-of-case scoring → a TLE case = 0 for that case; each case must individually survive.
- Hidden set re-generated → SECRET pools one-look each; no seed-overfitting.
- Interactive per-tick → flush per tick, read exactly what's sent, exit rc=0 on EOF.
- No PE; format errors → WA; first error stops judging.
- Determinism → rejudge-safe: byte-score-identical outputs (our suite proves this daily).
