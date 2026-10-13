# Codeforces feedback: test-class analysis

> **Historical review (post-contest).** I wrote this analysis against preliminary feedback
> and the solver state available at the time; it informed my late-contest decisions on the way to **#56 worldwide (16,147.866/22,000) on Aug 28, 2026**. I (Ntokozo Dlamini) originated the decisions; AI assisted with drafting and checks under my direction.
> Its test classifications, point
> estimates, and proposed mechanisms were hypotheses, not confirmed properties of
> the hidden tests or the final submission. The solution ideas and decisions
> are mine (the repository author). The full review transcript is not
> included in this submission.

## Summary

The review grouped the preliminary tests by the metric that appeared to
constrain their scores. It proposed five experiments: a link-saturation probe
(A), output-length-aware cloud placement (B), a cap on cross-cloud local batches
(C), a conditional upload-admission cap (D), and prefill-aware cloud placement
(E). Each proposal required a local discriminator and the repository's normal
A/B and safety gates before adoption.

| Class                                                    | Tests discussed          | Observed pattern                                                       | Main question                                                                 |
| -------------------------------------------------------- | ------------------------ | ---------------------------------------------------------------------- | ----------------------------------------------------------------------------- |
| I-a: throughput at the reference floor                   | 1, 2, 11                 | `norm_tp = 0`                                                          | Is the floor structural, or is the solver serializing available work?         |
| I-b: high absolute throughput, low normalized throughput | 5, 6, 13, 14, 19, 22     | Throughput sits near each test's `tp_base`                             | Is the remaining gap caused by the decode tail or by the scoring bracket?     |
| I-c: local batch growth                                  | 22 and parts of 5, 6, 19 | A combined local task may become expensive on growing task-time tables | Does the merge exceed the local table's useful batch size?                    |
| II: waiting-component collapse                           | 9, 10, 15, 17, 18        | Large mean TDR; some tests have `mean_tpot = 0`                        | Is input-stage delay caused by the link, cloud capacity, or admission policy? |
| III: moderate or mixed gaps                              | 4, 7, 8, 12, 13, 16, 21  | Smaller, mixed losses                                                  | Which changes survive without harming sensitive SLO cases?                    |

These groups overlap because a test can expose more than one mechanism. The
point totals in the original review were estimates, not recoverable-score
guarantees.

## 1. Throughput near the reference floor

The normalized throughput component is zero when achieved throughput fails to
exceed `tp_base`, the reference schedule's throughput. A low value does not, by
itself, identify a scheduler defect. Tests 1 and 2 also had low mean TDR and
zero mean TPOT; the review treated limited available concurrency as a plausible
explanation. Test 11 differed: its reported mean TDR was about `6.6e7 ms`, mean
TPOT about `16,199 ms`, and throughput about `7e-6` tokens/ms, while the waiting
component was already saturated.

The useful discriminator is to compare achieved throughput with a resource
bound. For a transfer link with bandwidth `bw` and `bytes_per_token = bpt`, the
byte-rate ceiling is

```text
link token rate ≤ bw × 10^6 / (8 × bpt) tokens/ms
```

This ceiling omits per-transfer latency and scheduling overhead, so it is an
upper bound rather than an attainable target. A wide gap between the bound and
achieved throughput would justify a closer look at the upload FIFO gate and
local decode scheduling. A narrow gap would indicate that the link itself limits
improvement.

**Option A — Link-saturation probe.** Build a local case with test 11's reported
signature. Measure upload occupancy, cloud idle time, local scheduling delay,
and achieved throughput. Only if admission throttling is demonstrated should a
regime-specific relaxation of the V11 upload-depth cap be tested.

## 2. High throughput with a long decode tail

Tests 5, 6, and 22 had different absolute throughputs but similarly low
normalized throughput. The review proposed that a long-lived request could
determine the final completion time after most batch members finish. Its
simplified model was

```text
makespan ≈ prefill prefix + longest output length × decode-wave period
throughput = total output tokens / makespan
```

This model is a diagnostic approximation. Actual waves can change size, clouds
can run concurrently, and transfer queues can become the binding resource.

**Option B — Output-length-aware placement.** At prefill assignment, estimate
each cloud's remaining decode work using finished-request output lengths and the
progress of active requests. Compare this with the existing `Lin`-based
placement rule on cases where input length is a poor proxy for output length.
Because cloud assignment is permanent, the experiment must measure both average
balance and worst-case tail completion.

The review suggested a survival-conditioned estimate of remaining output length.
That estimator is a research candidate; it should be evaluated for bias when
active requests have not yet finished.

## 3. Local batch size on growing tables

A cross-cloud `D_PRE` or `D_POST` group can save local setup work, but a growing
task-time table may make a large combined task slower per member. The review
used

```text
per-member cost f(m) = [S + D(m)] / m
add member m+1 while D(m+1) − D(m) ≤ f(m)
```

Here `D(m)` must come from the local task's table column. The condition is a
local marginal test; non-monotone tables require checking the relevant
breakpoints rather than assuming a single stopping point.

**Option C — Local combine cap.** Apply the local `D_PRE` or `D_POST` duration
table when evaluating a merged local group. Compare the resulting transcript and
score with a snapshotted solver. The original review linked this candidate to
OD-9 and D-046; its proposed gain was not a measured result.

## 4. Waiting-component collapse

The review identified large mean TDR on tests 9, 10, 15, 17, and 18. In tests
with `mean_tpot = 0`, the TPOT excess is zero, so any local improvement to the
waiting component must come through TDR while the score remains in its
responsive region.

For positive `dist_base`, the review modeled the waiting component as

```text
C_w = max(0, 1 − dist / dist_base)
dist = sqrt(e_tdr² + e_tpot²)
e_tdr = max(0, (mean_tdr − SLO1) / SLO1)
e_tpot = max(0, (mean_tpot − SLO2) / SLO2)
```

This gradient identifies which latency metric has local value. It does not prove
which resource caused the delay. The candidate causes were long FIFO transfers,
cloud-side prefill congestion, and admission throttling. A lower-bound
comparison is needed before treating the gap as recoverable.

**Option D — Conditional upload admission.** If prefill transfers are being
throttled while the link and clouds could make progress, test a higher in-flight
cap in that measured regime. Preserve the shallow cap on blackhole cases where
deeper FIFO queues increase waiting.

**Option E — Prefill-aware cloud placement.** Add pending `P_PROC` work and
cloud availability to the placement estimate. Test whether this reduces prefill
queue imbalance without creating a decode straggler.

## 5. Mixed tests and scoring risk

The review placed tests 4, 7, 8, 12, 13, 16, and 21 in a mixed group. Test 7 was
described as a pure-waiting case near an SLO boundary; test 20 was already near
a full score. These are useful regression sentinels. A candidate that helps a
large-gap class but crosses an SLO boundary elsewhere may lose more than it
gains.

## Experiment order

| Order | Experiment                                                                                                 | Decision it informs                                                       |
| ----- | ---------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| 1     | Reproduce the high-TDR signature of tests 15 and 18; compare achieved TDR with link and cloud lower bounds | Whether class II has scheduler-recoverable delay                          |
| 2     | Probe test 11's link occupancy and throughput ceiling                                                      | Whether option A targets a real admission bottleneck                      |
| 3     | A/B the local-table combine cap                                                                            | Whether option C improves growing-table cases without harming linear ones |
| 4     | Evaluate output-length estimation and cloud-tail balance                                                   | Whether option B improves permanent placement                             |
| 5     | Test conditional admission and prefill-aware placement independently                                       | Whether options D and E help the measured bottleneck                      |

Every algorithmic change I adopted still required a decision entry, a regenerated
submission artifact, the safety battery, and A/B comparison with a snapshotted
binary. Official judge feedback remained the scoring arbiter. The original
review's point ranges and recommended order were historical estimates only.
