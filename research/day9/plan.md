> **Post-contest archive note (Oct 2026):** Pre-contest preparation completed. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). This diagnostic day was completed 2026-08-06 (COMPLETE, EXP-34) and is preserved as a historical record. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# Day 9 — Flip-Rule Mining (Diagnostic Day)

Mandate: no score-chasing. One hypothesis per experiment. Understand why a locally better admission (chain-aware est) can globally hurt.

## Status: COMPLETE (2026-08-06, EXP-34)

## What was mined (evidence)

1. **Class × flip table** (case-level net on_time, estfix vs frozen):
   tight +289 (31/1), deadline +146 (31/5), mixed +88 (27/0), comm +47 (21/0),
   imbalance +17 (12/6), bigpool +756 (33/8), burst −11 (12/11/8).

2. **Flip-level facts**:
   - slack×Lout 2D table kills per-flip rules (bigpool 83-94% completion in every bucket; burst 22-75% in the same buckets).
   - Admitted burst flips complete 75% (49/65) — the damage is admit-queue displacement (P@ delays 9-28 ticks on burst_134), not starvation.
   - Bigpool losses are genuine starvation (38% completion), separated by in-flight-at-admit (16-28 vs 4-9) — but the threshold must scale with system size (burst completed sits at 14-16 too).
   - Future-arrivals-in-window separates best (4.06 vs 6.22) but is unobservable under the streaming protocol.

3. **Gates built and rejected**:
   - outrank-8 (slack0 feature mis-specified): burst −476.
   - in-flight-14 (flip-only): burst −325 (kills completed-flip value).
   - Both permanently doom at first tick; flip-completion value dominates on burst.

## Verdict

- No admission-time rule separates harmful from helpful flips (tested: slack, Lout, 2D,
  in-flight count, live-key outrank, contention; future-arrivals separates but is unobservable).

- estfix stays a high-value non-freezeable candidate; frozen winner untouched.
- Reopen conditions (per selection discipline): a rule that switches on system scale /
  arrival pattern (T, n, E·T vs demand) — never a constant; new case evidence; battery pass.

## Artifacts

- `tools/flip_lab.py` — per-flip dossier lab (reusable for any future admission-policy change)
- `/tmp/flip_{burst,tight,deadline,mixed,comm,imb,big}.json` — flip dossiers
- `build/solution_evt_cap4_asort_estfix_decode_only` — admit-old/decode-new isolation binary
- `build/solution_evt_cap4_asort_estfix_if14` — rejected in-flight gate (A/B ablation only)
- `src/Solution.cpp` — gate code behind `#ifdef ADMIT_INFLIGHT_GATE` / `ADMIT_OUTRANK_GATE`
  (default build == estfix, byte-identical verified)
