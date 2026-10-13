> **Post-contest archive note (Oct 2026):** Pre-contest preparation completed. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). Checklists, TODOs, and "to verify / will / pending" items below are preserved as historical pre-contest working notes (this runbook was executed on spec-drop day, Aug 14 2026). Anything still open is a Post-contest follow-up (not part of the #56 run). Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# Day 0 Runbook — Spec Reconciliation Day (opencode Upgrade 7)

**Rule applied on spec-drop day (Aug 14 2026): wrote NO solver code.** Only
reconciled. The pipeline below was executed top-to-bottom, in
order, with each step's output feeding the next. Every step is a command, not
a thought.

Timing goal: read the spec end-to-end (30–45 min) → update contract (30 min) →
envelope check (10 min) → engine verdict (15 min) → a written decision (10
min). Total ≈ 1.5–2 h, leaving the rest of Day 0 for rest and Day 1 planning.

---

## Step 0 — Inputs

- [ ] Official problem statement saved to `problem/statement.txt`
- [ ] Sample inputs/outputs saved under `problem/samples/`
- [ ] `limits.yaml` and `protocol/protocol.yaml` open for editing

## Step 0b — Map spec to Paper 1's formulation (opencode decision gate)

Per `src/opencode.md` line 172 and `research/notes/602-prereading-day0-step0.md`. BEFORE reading the spec for parsing (Step 1), read `research/notes/402-work-conserving-queueing-theory.md` (distilled from arXiv 2504.07347 Sections 1-2) and skim the paper itself if needed.

- [ ] Read `research/notes/402-work-conserving-queueing-theory.md` (5 min — has the work-conservation theorem for LLM inference, the prefill/decode interference model, the throughput-under-SLO formulation).
- [ ] After reading the spec end-to-end (Step 1), return here and rate the match:
  - **Match > 80%** (the problem IS throughput-optimal under SLO constraints with explicit prefill/decode phase interference, online arrival model, work-conservation being the theorem): the frozen solver `src/Solution_asort_frozen.cpp` is **valid as-is**; A/B test against spec samples, then proceed directly to Step 1's normal fill-the-contract flow. Do NOT rewrite the frozen solver.
  - **Match < 80%** (the spec shape differs on any major axis — baseline-relative scoring, per-phase time budgets, variable split points, chunked prefill, asymmetric delays, edge memory budgets): **pivot** to the Engine 1 + 2 portfolio (Engine 1 greedy with aging as the safety net, Engine 2 LNS as the refiner, Engine 3 flow conditional on the structural mismatch type — `research/notes/500-portfolio-engine-mapping.md`). Start each UNKNOWN dimension as an isolated A/B experiment against the frozen baseline.
- [ ] The match-test is a **human decision** — The archived AI guardrail (docs/DECISIONS.md): "Never let AI decide the freeze decision." Document the verdict in Step 7's written decision record.

## Step 1 — Read the spec end-to-end

- [ ] Read the statement once, no notes, no code
- [ ] Read the scoring section twice (see Step 5 — this is the #1 trap)
- [ ] Note ANY of these as first-class items:
  - [ ] Time limit model: per-test or aggregate suite
  - [ ] Compile model: judge compiles with what flags? Do WE submit a binary
        or source?

  - [ ] Score formula: absolute, relative-to-baseline, relative-to-best
  - [ ] Protocol: interactive per-tick (stdin/stdout), or batch file in/out
  - [ ] Threading: allowed? CPU-time or wall-time measured?

## Step 2 — Extract the contract

```bash
python3 tools/problem_contract.py --statement problem/statement.txt --out problem/

```

- [ ] Fill every UNKNOWN from the actual spec text
- [ ] Run `python3 tools/problem_contract.py --check problem/contract.yaml --complete`
      — must pass before anything else

- [ ] Update `protocol/protocol.yaml` statuses (CONFIRMED/CHANGED/REMOVED) with
      `reconcile_note` filled

- [ ] `python3 tools/reconcile_spec.py --check` — must be clean (no residual
      ASSUMED that the spec now contradicts)

## Step 3 — Envelope / dimension check (Engine 0)

```bash
python3 tools/engine0_probe.py --case-dir problem/samples --table
python3 tools/engine0_probe.py problem/samples/01.txt --json

```

- [ ] Does T, E, C, COMM, B, PEDGE, PCLOUD, PSCALE, ALPHA, n all fall inside
      the frozen solver's assumed envelope (see `engine0_probe.py` `ASSUMED`)?

- [ ] ANY warning = the frozen solver's counted-work ledger, buffer sizing, and
      doom logic were calibrated elsewhere. **A warning here is a stop-and-think,
      not a continue.**

- [ ] Run the architecture verdict: `python3 tools/arch_mapper.py --contract
      problem/contract.yaml` — do NOT reuse the architecture blindly.

## Step 4 — Protocol shape validation (the harness must match the judge)

```bash
python3 tools/rehearsal.py --solver build/solution_frozen --samples problem/samples
python3 tools/contest_runner.py --solver build/solution_frozen --pool 'problem/samples/*.txt' -j 4

```

- [ ] Interactive per-tick protocol → `fast_judge.py` path (already built)
- [ ] Batch file protocol → add a batch adapter NOW, not later
- [ ] Confirm the frozen binary still produces valid output on the REAL sample
      format (not our synthetic format). If the sample format differs (header
      shape, request line fields), this is the FIRST reconciliation item —
      the whole toolchain downstream assumes the current format.

## Step 5 — Score function shape

```bash
python3 tools/score_probe.py --solver build/solution_frozen --case problem/samples/01.txt

```

- [ ] CONFIRMED absolute `on_time - 1e-6*V`? → strategy unchanged
- [ ] CONFIRMED relative/baseline/best-in-contest? → **STOP**: the objective
      changed from "optimize raw score" to "optimize gap to a reference".
      Every tuning constant, every admission key, every doom threshold must be
      re-derived against the true formula. This is the opencode Trap 4 and it is
      the single highest-impact reconciliation finding.

- [ ] UNKNOWN still? → keep the absolute assumption, flag it in the decision
      record, and design a Day-1 experiment to probe it (run 2 plans, see if
      the judge's reported score moves like absolute or relative).

### Step 5b — Empirical score identification (EXP-45 tool)

When the sample output (or first real-judge verdict) includes an official
score for a KNOWN case, verify the formula class empirically — text scanning
is not enough (score_probe.py --spec reads statements; the oracle reads
numbers):

```bash
python3 tools/score_oracle.py --casegen /tmp/oracle_cases
python3 tools/score_oracle.py --run /tmp/oracle_cases/all_ontime.txt build/solution_frozen
python3 tools/score_oracle.py --identify <normalized_sample_case> build/solution_frozen <OFFICIAL_SCORE>

```

- [ ] Normalize the official sample case to the local format FIRST (batch
      adapter / format reconcile) — the oracle feeds the solver the local
      header + request lines, so an unconverted sample gives garbage.

- [ ] MATCH on exactly one family → strategy implications per family table
      (hard_all_or_none: every refusal is a zero; lateness_*: V-minimization
      dominates; abs_eps_v: status quo).

- [ ] AMBIGUOUS (multiple matches) → families are indistinguishable at the
      sample's printed precision; only the spec text can tie-break. Note:
      abs_eps_v vs throughput vs quad_penalty differ by ≤1e-6·V, so at
      typical 6-decimal output precision they only separate if V ≥ ~1e6.

- [ ] NO match → formula outside the family set; the opencode Trap-4 alert:
      re-read the scoring section before ANY solver work.

- [ ] CRITICAL: fast_judge.py's own score is computed with OUR assumption —
      it is NOT evidence. Only an official judge score is evidence.

## Step 6 — Regression + freeze decision

```bash
python3 tools/freeze_gate.py --candidate build/solution_frozen --baseline build/solution_evt_cap4_asort_frozen
python3 tools/ab_test.py --baseline build/solution_evt_cap4_asort_frozen --candidate build/solution cases/*.txt

```

- [ ] Frozen vs Day-6 frozen: any score change on the 20-case suite?
- [ ] If the sample format changed → regenerate `cases/` from the real spec
      (`tools/case_gen.py --seed <new> --dev 500 --secret 1000`) and re-run
      the freeze gate with `cases/*.txt` REPLACED by the regenerated set

- [ ] Record: is the existing frozen binary still the incumbent, or does the
      spec require a re-freeze?

## Step 7 — Written decision record (required before any Day-1 code)

Write `problem/PROBLEM_DAY.md` with:

1. **Kept** — engines/assumptions confirmed by the spec (with protocol.yaml status)
2. **Killed** — engines/assumptions contradicted by the spec (with reconcile_note)
3. **Pivoted** — engines needing parameter/strategy change (list exactly which)
4. **Day-1 plan** — the first valid baseline to bank, and the score to beat
5. **Open UNKNOWNs** — explicitly deferred, with the experiment that resolves each

---

## Day-1 (next day) — Bank a valid baseline FIRST

Per the archived cadence (docs/DECISIONS.md): greedy feasible incumbent → budgeted local search →
re-optimization. Do NOT start Day 1 with experimentation. Get the day-1
checklist filled (`docs/05-strategy-playbook.md`), freeze a valid submission,
THEN experiment.
