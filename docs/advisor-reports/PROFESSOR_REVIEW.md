# Structural review of the scheduler

> **Historical review (post-contest).** I received this external analysis during the contest and used it as hypothesis input on the way to **#56 worldwide (16,147.866/22,000) on Aug 28, 2026**. I (Ntokozo Dlamini) made all adoption decisions; AI assisted with summarizing and checking under my direction.
> This document summarizes an external analysis of an
> earlier solver revision. It records proposed mechanisms and code changes; it
> does not establish that those mechanisms were correct, shipped, or improved
> the final score. The solution ideas and decisions are mine (the repository
> author). The full review transcript is not included in this submission.

## Assessment

The review argued that a fixed priority order can miss useful work when prefill,
decode, and transfer queues compete. Its central proposal was to compare the
score cost of delaying each ready task, then use that estimate to arbitrate
local and cloud work. Six related areas were identified: prefill chunking, cloud
task priority, token-ready local priority, upload admission, cross-cloud
transfer pricing, and active-population batch sizing.

These were hypotheses about the revision under review. Each requires
confirmation against the relevant source version and an A/B experiment before it
can be treated as a solver improvement.

## Findings in the reviewed revision

| Area                 | Observation in the review                                                                                                                                | Proposed check or change                                                                                                  |
| -------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| Prefill chunking     | The guard `S < 1.0` could not fire under the stated `S ≥ 1` constraint; the piece-selection branch also favored full pieces when competing work existed. | Verify the guard in the reviewed build; test chunking only where saved decode wait exceeds extra setup and prefill delay. |
| Cloud priority       | `P_PROC` was selected before ready `D_PROC` work.                                                                                                        | Compare the score cost of delaying prefill with the cost of delaying current decode tokens.                               |
| Local priority       | A ready `D_POST` could wait behind `D_PRE`.                                                                                                              | Measure whether completing current tokens first reduces TPOT and the final completion time.                               |
| Upload admission     | A prefill-only in-flight counter also gated a decode path.                                                                                               | Separate prefill pressure from total upload exposure; A/B any cap change on link-saturated cases.                         |
| Prefill admission    | Shortest-`Lin` ordering could postpone older requests.                                                                                                   | Test age or deadline signals when TDR liability is high.                                                                  |
| Cross-cloud grouping | A one-transfer approximation could omit latency for additional cloud transfers.                                                                          | Price each FIFO transfer and compare predicted with observed completion times.                                            |
| Batch sizing         | Per-member task cost did not account for the number of active decode requests.                                                                           | Compare task efficiency with the full active-population round period.                                                     |
| Score oracle         | Clamp derivatives can be zero outside a scoring interval even when a finite improvement could reach it.                                                  | Use finite changes or a justified lookahead near score thresholds.                                                        |

The full transcript contains file-level sketches for each finding. Those paths
and line references describe the historical code snapshot and may differ from
the current tree.

## Scheduling models proposed by the review

### Token-ready work

The review estimated that delaying a ready `D_POST` group of size `m` by time
`τ` adds approximately `mτ` to the sum of token gaps. With `G` total gaps and a
local TPOT score value `V_TPOT`, it priced that delay as

```text
estimated TPOT loss ≈ V_TPOT × m × τ / G
```

A `D_PRE` action may benefit future tokens, but only after its upload, cloud
compute, download, and post-processing chain. This comparison motivates a
token-ready priority experiment. It is not a universal ordering rule: batch
formation, transfer contention, and score clamps can change the result.

### Cloud arbitration

When a cloud has both a prefill piece and a decode group ready, the review
compared approximate delay costs:

```text
D_PROC delay cost ≈ V_TPOT × m × τ / G
P_PROC delay cost ≈ V_TDR × τ / R_est + downstream effects
```

Here `R_est` is the estimated final request count. The omitted downstream
effects matter: a prefill piece may release later work or become part of the
critical path. A candidate arbiter must account for those effects and be tested
across both waiting-heavy and throughput-heavy cases.

### Cross-cloud transfers

For a batch of `m` requests spanning `r` clouds, with transfer latency `ℓ` and
per-token transfer coefficient `β`, the review used

```text
one transfer:  ℓ + βm
r transfers:  rℓ + βm
extra latency: (r − 1)ℓ per direction
```

The number and order of transfers must follow the actual judge protocol. The
useful test is an oracle comparison between the solver's predicted FIFO times
and the judge's observed events, followed by score A/B for any scheduling
change.

### Active-population batch sizing

The review contrasted a local per-member task cost with an approximate round
period for `A` active decoders:

```text
per-member task cost = [S + duration(m)] / m
population round period ≈ ceil(A / m) × wave_time(m)
```

The second expression captures repeated waves and may be more informative for
TPOT. It remains a model: cloud partitions, changing membership, transfer
queues, and non-monotone duration tables must be included before using it as a
decision rule.

### Chunking and prefill admission

Chunking can expose a cloud to ready decode work earlier, but every extra piece
pays another setup cost and may delay the prefill request. The review proposed
chunking only when the estimated score saved by earlier decode exceeds the extra
setup, prefill delay, and critical-path cost.

Shortest-input-first admission can also delay an older request under sustained
arrivals. The proposed alternative was to include age or deadline pressure when
TDR is valuable. Neither change is safe solely because its local delay estimate
is favorable; both need whole-case scoring evidence.

## Proposed code changes

The original review listed twelve implementation points. This table retains
their intent without treating them as approved changes.

| Item | Proposed change                                                                      | Main verification risk                                                   |
| ---- | ------------------------------------------------------------------------------------ | ------------------------------------------------------------------------ |
| 3.1  | Recompute a throughput-versus-waiting control weight from remaining score liability. | A zero derivative or unreliable projection can cause unstable switching. |
| 3.2  | Track total upload exposure in addition to prefill uploads.                          | Predicted and actual FIFO counts must stay synchronized.                 |
| 3.3  | Make the prefill in-flight cap depend on the observed regime.                        | A deeper FIFO can harm blackhole and tight-TDR cases.                    |
| 3.4  | Test `D_POST` ahead of `D_PRE` when tokens are ready.                                | A smaller post batch may erase the immediate gain.                       |
| 3.5  | Arbitrate cloud `D_PROC` and `P_PROC` by estimated delay cost.                       | Downstream effects and score clamps complicate the estimate.             |
| 3.6  | Add effective score-value helpers for finite threshold crossings.                    | Estimates must agree with the judge's score formula.                     |
| 3.7  | Count all cloud transfers in cross-cloud batch cost.                                 | Transfer order and overlap must match the protocol.                      |
| 3.8  | Correct the chunking guard and test a liability-aware piece rule.                    | Extra setup cost and source-size budget.                                 |
| 3.9  | Add age or deadline information to prefill admission.                                | Throughput loss from abandoning shortest-first ordering.                 |
| 3.10 | Use token age and expected completion in `D_PRE` selection.                          | May reduce batching or starve newer requests.                            |
| 3.11 | Reduce decode holding during drain or low arrival rate.                              | End-of-test arrival inference is uncertain.                              |
| 3.12 | Estimate remaining decode work per cloud from observed output progress.              | Output-length estimates can be biased by unfinished requests.            |

## Placement and batch models

The review proposed a placement estimate that combines cloud availability,
queued prefill work, upload and download tails, the incoming request's input
length, and estimated decode pressure on the cloud. Its purpose was to compare
candidate clouds at `P_PRE`; the expression was not presented as an exact
simulator.

For batch choice, it separated resource capacity from member latency. A
candidate batch of size `m` spanning `r` clouds was priced by local work, cloud
work, and both FIFO transfer directions. The review then suggested choosing `m`
based on the binding resource and an active-population latency estimate. These
formulas are useful as testable models, subject to the exact task-time tables
and transfer ordering.

## Reviewer's responses to six concerns

1. **Busy time:** Measure whether occupied resources advance tokens or reduce
   TDR; utilization alone is not the objective.
2. **Chunking:** The reviewed guard appeared to disable it, and piece selection
   required a separate score test.
3. **TPOT distribution:** A pooled mean can still be driven by a small number of
   long token gaps; inspect those gaps directly.
4. **Wave tests:** Batch size is one axis among admission, task priority, cloud
   arbitration, and drain behavior.
5. **Transfer model:** Missing per-cloud latency may matter even when a single
   constructed case shows no score change.
6. **Hidden tests:** Use observed state and verified score projections rather
   than assuming a hidden-test profile.

## Suggested validation order

1. **Confirm the snapshot.** Check each alleged code path against the exact
   reviewed revision and the current solver. Discard findings already fixed or
   based on an incorrect protocol assumption.
2. **Test isolated scheduling changes.** A/B token-ready priority, cloud
   arbitration, and transfer accounting independently. Keep a snapshotted binary
   for each comparison.
3. **Evaluate dynamic control.** Verify score projections against the judge,
   then test admission caps and finite-threshold logic across adversarial cases.
4. **Evaluate larger policy changes.** Test prefill admission, chunking, and
   output-length-aware placement only after the simpler mechanisms are
   understood.
5. **Apply the repository gates.** Record any adopted algorithmic change in
   `docs/DECISIONS.md`, regenerate `src/submission.cpp` from the layered
   sources, and run the safety battery and official-feedback attribution
   required by the project.

The review's implementation order was advice for an earlier stage of the
project. The [decision log](../DECISIONS.md) and final contest record establish
what I actually tested and shipped on the way to #56.
