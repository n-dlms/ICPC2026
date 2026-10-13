> **Post-contest archive note (Oct 2026):** Contest audit completed 2026-08-26, before the Aug 28 2026 contest close (final rank #56 worldwide, top 1.3%). Probe plan below is preserved as a historical working record. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 06 — Implementation record & CF probe plan (2026-08-26)

Three audit mechanisms implemented in the layers (`src/scheduler.cpp`,
`src/policies.cpp`), flags baked via `tools/merge_submission.py FLAGS`, artifact
regenerated and verified. NO commit made (per standing rule).

## M1 — MECH_PRESSURE_PICK (audit §1.2 / matrix primitive P2)

Site: `Scheduler::pick_cloud` (scheduler.cpp, P_PROC SPT block).

Semantics: when cloud c has BOTH a ready prefill request AND ready decode work,
decode wins this frame IFF all three hold:
    get_excess_tdr() <= 0.0   (TDR compliant)
    get_excess_tpot() > 0.0   (TPOT is the binding SLO)
    rl_size(READY_D_PROC, c) > 0
Every other regime keeps the incumbent prefill-first order BY CONSTRUCTION:
TDR-excess ⇒ prefill (tdr_urgent semantics preserved); both-compliant ⇒
incumbent. This makes the mechanism provably inert on knife-edge configs
(adv_living_cliff-class) and on the starve class where TDR already breaches.

## M2 — MECH_DPRE_UNGATE (audit §1.3; confirmed verbatim at scheduler.cpp:207)

The `#ifdef V11_PPRECAP if (state.up_inflight_pre() < max_inflight_eff)` gate
around the D_PRE pop used a PREFILL-UP counter to throttle DECODE group
formation. With the flag ON (default), that condition is compiled out of the
D_PRE path; the identical cap logic REMAINS for its designed purpose — P_PRE
admission depth (up_full at the ppre_pop site). A decode-ready request can no
longer be blocked by new arrivals' prefill UPs.

## M3 — MECH_ROUNDTIME (audit §1.4)

Site: MarginalBatch::group_size, after the existing max_m_slo scan + crossing.

cost(m) is a single-batch makespan and cannot see inter-batch queueing: a pool
of n served in batches of m forces ceil(n/m) repetitions before every member
gains one token. New metric for D_PROC groups only (they never span clouds ⇒
closed form exact: exactly one UP entry + one DOWN entry carry the batch):
    latency(m) = tr(m) + (S + dproc(m)) + tr(m) + (S + dpost(m))
    cycle(m)   = max(2S + dpre(m) + dpost(m), S + dproc(m), 2·tr(m))
    RT(m)      = latency(m) + (ceil(n/m) − 1) · cycle(m)
Fires ONLY when n_ready > cap (genuinely deep backlog), n_ready ≤ 50000, and
(w_c == 0 OR TDR compliant). If the SLO crossing set slo_bound (cap clamped to
max_m_slo because V_tpot made it binding), we never deepen past it — SLO
protection stays authoritative. Otherwise the candidate deepens to the full
pool and is accepted iff it strictly lowers RT. O(1)/frame after table lookups;
no loops added (TLE-guard compatible).

## Verification performed

1. python3 tools/merge_submission.py → src/submission.cpp = 64,718 chars
   (margin 817 vs limit 65,535; generation fails loudly beyond).

2. make build/submission (-Wall -Wextra -Wpedantic -Werror -DNDEBUG): clean.
3. tools/replay_pretest.py build/submission → RESULT: PASS assignments match
   expected; score drift 0.0000 (Example-1 byte-identical — mechanisms
   provably inert there by design).

4. All touched units compile clean in dev (no flags ⇒ disabled paths exercised)
   and ASan/UBSan: protocol, task_table, state, scheduler, policies, Solution.
   NOTE make san/make all still fail inside src/fwsim.* for a PRE-EXISTING
   reason (AF-3 below); fwsim is NOT part of the generated artifact.

## Pre-existing findings handled / recorded during implementation

AF-3: src/fwsim.cpp/fwsim.hpp reference State::predict_lout() and
      State::ewma_lin(), which do not exist in state.hpp — an earlier
      integration (P-series landing) pasted code against unwired members,
      leaving layered dev builds broken while the artifact stayed green (fwsim
      is excluded from UNITS in merge_submission.py). Needs a human decision:
      wire the two helpers into State, or drop fwsim from Makefile SRCS.
      Not fixed here — scoring-simulation internals are out of scope tonight.
AF-4 (FIXED): pick_local's "Theorem B" D_POST block contained
      post-minification identifiers (state.qS/state.qR/spec.q13) making every
      LAYERED build uncompilable since that paste. Canonicalized to
      rl_size/rl_pop/cloud; regenerated artifact verified BYTE-IDENTICAL to
      the pre-fix artifact — zero behavioral risk. Standing lesson: NEVER
      author layer code from minified artifact text; layers carry long names,
      the merge tool applies _RENAME.

## CF probe sequence (one mechanism per submission; each attributable)

Baseline for attribution = current CF best 16,147.866 tree. Submit in order;
compare per-test feedback rows against the immediately preceding submission:

  PROBE-A = MECH_PRESSURE_PICK + MECH_DPRE_UNGATE only
            (edit FLAGS: remove "MECH_ROUNDTIME").
            Expected movement: exactly on tests where TPOT was in excess while
            TDR stayed compliant (decode-priority flips), and where decode
            group formation was throttled by prefill-UP counts (#5/#6/#19-
            class TPOT-side losses; D-062 spread-arrival analogues).
  PROBE-B = A + MECH_ROUNDTIME (full FLAG list as now baked).
            Expected movement: ONLY deep-backlog shapes (sustained n_ready >
            profit-cap); zero effect elsewhere by construction.

Kill criteria per probe: any stuck/zero verdict; negative net mean over the
feedback tests; any regression on adv_living_cliff-analogues in per-test
feedback (structurally inert by gating ⇒ a hit there means an oracle-excess
assumption is wrong → revert and re-audit excess definitions vs sample timing).

Rollback: delete flag name(s) from FLAGS in tools/merge_submission.py and
regenerate (byte-exact return per combination, process verified above).

Byte accounting: net artifact delta −485 chars vs pre-session file
(65,203 → 64,718): M2 removes the compiled-out condition (~70 B raw);
canonicalization round-trips through _RENAME identically. Added machinery
≈ 420 B raw, inside existing headroom — no new rename funding needed.
