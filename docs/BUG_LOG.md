# BUG_LOG.md — every bug: symptom, root cause, fix, verification (contest archive that led to #56)

Author: Ntokozo Dlamini (AI sped up checking/summaries). First entries were completed after Phase 4 implementation and contributed to #56.

## 2026-08-14 — BUG-001: Debug transfer oracle predicted FIFO order backwards (P1, debug-only)

**Symptom**: `TransferQueue::confirm` assert (src/state.cpp:36) fired on zero_S and dribble; predicted XDN times differed from actual by up to 0.303s, and one case by exactly one latency.

**Root cause**: Predictions were pushed at TDN-apply time. Real FIFO enqueues at task-issue time (enter = predicted completion).

Issue order ≠ completion order (P PROC 8 issued before D PROC 0's iteration but completing after it) ⇒ prediction queue inverted the real order.

Release scoring unaffected (phase machine is event-driven), but the oracle silently diverged.

**Fix**: Move all four push sites into `State::issue()` (D-004).

**Verification**: Debug zoo green (all asserts live), crash_sweep 800/800 debug, Example 1 500.000 exact.

**Lesson**: Queue models must mirror enqueue time, not event-apply time. All later transfer-aware policies that led to #56 built on this fix.

## 2026-08-14 — BUG-002: Incremental make produced heap corruption (P0, build system)

**Symptom**: `double free or corruption (!prev)` / SIGABRT (exit 134) on max-scale replays, release AND debug, after the perf refactor.

ASan: WRITE of size 48 at `State::init`, overflowing `state` (main frame, 456 B object) by 24 B. frozen_v1 clean; single-TU builds (merge_submission) clean.

**Root cause**: Makefile had NO header dependency tracking (-MMD).

Editing `state.hpp` rebuilt only `state.o`; `solution.o` still embedded the OLD `State` layout (no `finish_pending_`, −24 B).

New `state.o`'s `State::init` wrote `stats_`/`finish_pending_` at +24 offsets → overran the stale-sized object. Classic ODR layout mismatch between TUs.

(First diagnosis pointed at `vector::erase(begin())` — WRONG: gprof showed `end_frame()`'s O(R) scan was 84% of user time; pops were 1.8%.)

**Fix**: Makefile CXXFLAGS/DEBUGFLAGS += -MMD -MP; -include per-variant .d files; clean removes *.d. Verified: touch state.hpp → Solution.o/state.o/scheduler.o/policies.o all rebuild.

**Verification**: make clean && rebuild all 4; max replay + R500 replay exit 0; zoo 12/12; crash 3000/800/800/400 ok; pretest 500.000 exact; rebuild identity byte-identical.

**Lesson**: Lesson learned for #56: any header change had to be followed by `make clean` (or -MMD). This bug silently poisoned every binary linked between the first state.hpp edit and the clean rebuild.
