> **Post-contest archive note (Oct 2026):** Contest audit completed 2026-08-26, before the Aug 28 2026 contest close (final rank #56 worldwide, top 1.3%). Candidate rankings below are preserved as a historical working record of the contest-close queue. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 04 — Ranked candidates (one mechanism = one CF submission)

Standing constraints: artifact = 65,203 B of 65,535 ⇒ **332 B headroom**; new code
must be byte-funded via tools/merge_submission.py rename/minify passes. Working tree
carries UNCOMMITTED, CF-unprobed P0–P7 landings + D-161 pack: attribution for those
comes FIRST in any submission queue; nothing below may be stacked with them.
Kill criterion template: revert on any per-test regression or adv_living_cliff <733.884.

## Tier 0 — prerequisite probes (not from this audit)

0a. Submit current tree (D-158/D-159 modes + D-161 x9/TLE pack). Attribution by
    per-test rows. This is already doctrine; listed to keep the queue explicit.

## Tier 1 — this audit's new mechanisms

C1. **Piece-TDN endgame/lull trigger** (matrix rows 04/12 + primitive P1).
    Mechanism: during quiet windows arm micro-piece chains; absence of non-clock
    events across successive polls updates the "arrivals ended" posterior (test-level
    geometric evidence, R unknown & no signal clause :400-401); drive SB05/SB08-style
    drain policy from it. Byte cost: modest (~400-700 B raw; fundable). Risk class:
    hold/deadlock (mitigate: clock pieces never gate real work; every real task still
    starts on its own event). EV rationale: endgame tail is the largest *legal-policy*
    pool (SA06/SYNTHESIS §6.5); previously blocked solely by wake-proof obligation.
    Kill: any stuck state in hostile fuzz; zero transferable gain.
C2. **Assignment-line SJF tie-break** (row 06, P2). Sort response lines so that among
    tasks completing simultaneously earlier-perceived-finishers carry shorter queued
    transfers first. ~150-300 B. Wrong-sign risk minimal (pure ordering inside one
    response; same set of tasks either way). Kill: any A/B regression beyond noise.
C3. **Scalar regime collapse classifier** (rows 07-09): at t=0 classify wait-side as
    DEAD when dist_base ≥ d_max(achievable) or SLOs ≥ ceiling, then disable ALL wait-
    protective machinery (cliff gates, holds, DPOST waits) and run pure-makespan.
    Cheap, gated; adversarial guard = keep classification monotone-safe (never flips
    to pure-wait). Kill: living_cliff drop.
C4. **Token-cap ladder Lout estimator** (row 11/F2): feed ΣLout≤2e5 & Lout≤512 hard
    bounds into survival-conditioned posterior max(μ_bucket, iter+1). Small bytes;
    pairs naturally with C1 but SUBMIT SEPARATELY.

## Tier 2 — previously ranked, unchanged relative standing

D1. Gated herd-sync hold (m5_order-class) — wrong-sign family; ONE probe maximum.
D2. Span-aware dead-zone parking depth. D3. Wave pipelining w/ P1-backed wakes.
Order: after 0a attribution, C-class before D-class because C-items are cheap,
deterministic, and falsify cleanly.

## Explicitly NOT recommended

N1. dist_base=0 knife-edge chasing (float-exactness lottery; burned once, negative-EV).
N2. Any #3 pure-wait razor variant (D-152/D-153 verdict stands).
N3. Re-litigating floors without a NEW loophole class (this audit found none that
    reverses futility claims; see 02/F1).

## Addendum (2026-08-26 audit close-out): new candidates

C6a. **Assert removal in artifact** (hardening): compile-time eliminate protocol.cpp
     live asserts (`e>=0`, `server_s[0]=='C'`, `m>=1`×2, `dur>0`) either by -DNDEBUG
     hardening in merge_submission bake or by replacing asserts with silent no-op
     guards. Frees bytes AND removes whole-test abort risk from hypothetical judge
     formatting quirks. Near-zero EV on conforming streams; pure insurance + bytes.
C6b. **stderr silencing**: guard the unconditional "[DEBUG] Unknown event kind"
     fprintf behind #ifdef LOCAL_DIAG (it currently ships in the artifact; repeated
     hits could fill a 64 KB pipe and block = TLE mechanism already documented for
     LOCAL_DIAG). Zero behavior change on conforming streams; removes a tail-risk
     vector. Both C6a+C6b together are ONE submission (single mechanism: artifact
     robustness; attributable as a pair).
Re-ranking impact: C6a/C6b slot ABOVE C3 (which downgraded this session — see
03 Part II rows 07/08 dossier) by tail-risk logic, below the standing Tier-0
attribution probe. Full order: Tier-0 → C2 (tie-break FIFO control, pure win,
near-free) → C6a/C6b → C1 (alarm clocks, spec 05) → C4 → C3 → D-class.
