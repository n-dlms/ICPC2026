> **Post-contest archive note (Oct 2026):** Contest audit completed 2026-08-26, before the Aug 28 2026 contest close (final rank #56 worldwide, top 1.3%). SPEC below is preserved as a historical working record; any unimplemented mechanism is a Post-contest follow-up (not part of the #56 run). Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 05 — DESIGN SPEC: Piece-TDN Alarm Clocks (candidate C1 / primitive P1)

Status at contest close: SPEC ONLY (preserved; any implementation is a Post-contest follow-up, not part of the #56 run). Implementation would require a DECISIONS entry and ONE dedicated CF
submission. This document holds the full legality proof, feasibility analysis,
cost model, wake-proof, policy design, failure-mode census, and byte budget so an
implementing session never re-derives anything.

## 0. One-paragraph idea

The protocol gives NO self-wake timer (problem.md:364): decisions happen only when
a frame arrives. But every extra input-stage piece a request runs EMITS a TDN event,
i.e. manufactures a future decision point at a time WE choose (≈ now + S + dur).
Tiny pieces therefore act as programmable alarm clocks: legal, cheap, link-free ways
to wake the policy during lulls/endgame windows where today's reactive engine is
silent and cannot start e.g. a drain-tail merge or refresh an endgame posterior.

## 1. Legality proof (statement clauses cited)

L1 Pieces exist: input stage splittable into pieces `[ls,le)` nonempty ints,
   ascending gap-free, first ls=0, last le=num_layers; duration
   `(le−ls)/num_layers·pproc(Lin)`; only LAST piece queues DOWN (problem.md:136-152).
   Piece count is bounded only by distinct split points ≤ num_layers−1 extras.
L2 When issuable: a piece assignment is an ordinary response line assigning the next
   un-run interval of the request's input stage to its cloud (:297-321); predecessor
   rule :147 says a piece waits ONLY on its previous piece's TDN — any not-yet-run
   interval of an in-flight input stage may be scheduled whenever its cloud is free.
L3 Cost: each piece occupies ITS cloud S+dur (S paid per task incl. per piece,
   :110/:123). No transfer until the last piece completes (:151).
L4 Response legality: n ∈ [0,K+1] lines, one task per resource per frame
   (:297,:371) — a clock consumes exactly one line on one free cloud, ZERO link bytes.
L5 Simulate-ahead sanctioned (:290,:362) — computing piece boundaries to time wakes
   is explicitly allowed.

Conclusion: arming an alarm clock = issuing the host's next legal piece with length
chosen so its TDN lands where wanted; wake time T_wake ∈ [t+S+pproc(Lin)/num_layers,
t+S+pproc(Lin)] continuously tunable by interpolating le−ls.

## 2. Feasibility precondition (the honest limit)

A clock needs a HOST: ≥1 request whose input stage is IN FLIGHT — past UP (XDN
delivered), pieces not all issued/completed: H(t) = {i : XDN_i ∧ pieces_left_i > 0}.

F1 Early/mid test: usually many hosts — clocks available exactly when arrivals may
   still be pending, which is the useful window (no end-of-arrivals signal :400-401).
F2 True endgame (all requests past prefill): H(t)=∅ ⇒ no clocks; behavior degrades
   gracefully to today's event-driven engine. Endgame claims must scope to
   "endgame-with-prefill-stragglers" or pair with decode-side holds (existing
   wake-proven pattern) and the token-cap ladder estimator (C4).
F3 K==1 ∧ num_layers==1 corner: no spare split possible (last piece forced single);
   guard triggers on num_layers>1 ∧ (K − busy_clouds) ≥ 1.

Mitigation for F2: pair with D_PRE accumulate hold (members' leg frames arrive and
release it) — clocks EXTEND coverage, they need not cover 100% of silent windows.

## 3. Wake-proof construction (answers the D-140-P0 obligation)

WP1 Clocks never gate real work: arming holds no resource beyond its own cloud slot
   for S+dur; productive assignments take line priority when n ≤ K+1 binds.
WP2 Each armed clock produces EXACTLY ONE guaranteed future frame (its TDN) at
   T_wake ≤ t+S+pproc(Lin) — bounded latency, deterministic (non-adaptive judge).
WP3 Chains terminate: each host has ≤ num_layers−1 extra pieces; global chain count
   finite; pool empties ⇒ falls back to base behavior. No infinite poll loop.
WP4 Re-arm rule is memoryless w.r.t. correctness (only finite counters persist).
WP5 FIFO interplay: non-last pieces enqueue NOTHING on UP/DOWN ⇒ link orderings
   (row 06 tie-breaks) untouched; zero external side effects.

This satisfies "every new hold needs an event-backed wake" with events we
MANUFACTURE; previously rejected staggered-wave designs become provable because
wave-boundary checks can be scheduled AT chosen times instead of passively awaited.

## 4. Cost model & pricing

Direct cost per clock piece: Δ = S + dur added work delaying the HOST's critical
path (DOWN→P POST→tokens shift right by Δ). Pricing with pooled-mean dilution (L5):

    loss_wait ≈ w_c · (∂Score/∂tdr) · R⁻¹ · Δ
    loss_tp   ≈ w_tp · (∂Score/∂E)  · Δ       (only if host ON the makespan path)

Arm predicate (SB04 value calculus): expected_gain(wake) > loss_wait + loss_tp,
host := argmin loss = prefer large-slack hosts, small pproc(Lin)/num_layers floors,
positive tdr slack. Budget: hard global cap B_clock = max(1, ⌊η·Σ_avail_pieces⌋),
η ≈ 0.15; worst-case spend B_clock·(S+dur_max); predicate is FALSE on knife-edge/
pure-wait configs (loss_wait enormous) ⇒ self-disabling exactly where aggressiveness
is disqualifying (adv_living_cliff-class, c_w=1 discriminator).

## 5. Trigger policy (when to arm)

Arm at frame f iff ALL of:
T1 H(f) ≠ ∅ ∧ ≥1 free cloud (piece's own legality);
T2 silence heuristic: t − t_last_nonclock_event ≥ θ_lull (θ ≈ p95 of measured
   inter-event cadence; conservative start) OR SB08 endgame posterior crosses;
T3 budget: clocks_used < B_clock;
T4 §4 value predicate passes (cached marginals, O(1)/frame);
T5 safety interlock: NOT while cliff-latch SAFE contract forbids holds / drains
   mid-flight — respects every existing gate.

At the WAKE frame: refresh the arrivals-ended posterior (a chained sequence of wakes
with zero non-clock events is geometric evidence that R ≤ observed — no
end-of-arrivals signal exists to do this for us); then re-arm within budget, or
execute the deferred action: drain-tail merge admission, wave-boundary evaluation,
park-depth adjustment.

## 6. Pseudocode (integration point: after the work-conserving pass leaves spare lines)

```
on_frame(t):
    assignments = schedule_work_conserving(t)      // unchanged, absolute priority
    spare = (K+1) - len(assignments)
    if spare > 0 and clock_budget_left():
        for host in argmin_loss_hosts(H(t)):        // §4 ordering
            if triggers_T2..T5_pass(t, host):
                (ls, le) = pick_piece_length(target_t = t + θ_check)
                emit("C%d P PROC %d %d" % (host.cloud, ls, le))
                --budget; --spare
                if spare == 0: break
    flush(assignments)

```

The emitted piece must be the host's NEXT legal interval and is routed THROUGH the
ChunkPolicy interface so planner state stays consistent (clock routing never
bypasses the chunk planner).

## 7. Failure-mode census (each with its guard)

FM1 Herd inflation: too many clock pieces slow prefill tail → η-budget + T4
    predicate (self-disabling on tight-critical-path tests).
FM2 Wake storm: chains re-arm every frame in pseudo-lulls → minimum spacing between
    consecutive armed clocks ≥ observed S_min; count kill-switch at B_clock.
FM3 Chunk-policy collision → route through ChunkPolicy interface only (FM-avoiding).
FM4 Stderr leakage: none — feature adds no output paths.
FM5 Zero-duration piece: floor dur = pproc(Lin)/num_layers ≥ table-min > 0 (:235);
    a zero/negative-length line is never constructible from an empty interval rule.
FM6 Host completes mid-plan: piece request for a finished request = protocol error
    → eligibility check H(t) recomputed EVERY frame immediately before emit.

## 8. Byte budget (free: 65,535 − 65,203 = 332)

Trigger predicates ~180 B minified; host argmin ~90 B; piece emit via existing
ChunkPolicy call sites ~60 B; budget counters ~40 B ⇒ ≈370 B raw ⇒ needs ~40–150 B
funding via remaining shorten_names qN tokens (inventory kept in
tools/merge_submission.py). Verdict: FUNDABLE, contingent on Tier-0 attribution.

## 9. Probe protocol (one mechanism, attributable)

Submit ALONE after the standing Tier-0 probe. Prior expectations: gains on
lull-heavy / starve-analog tests through better drain timing and endgame decisions;
no effect on knife-edge configs. Kill criteria: (1) any stuck/zero verdict anywhere;
(2) negative net per-test prelim sum; (3) marginal tp regression on pure-tp analogs.
Rollback: revert the marked trigger block + planner shim + rename entries.
