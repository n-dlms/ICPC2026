> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 306 — Speed Is as Powerful as Clairvoyance (Kalyanasundaram & Pruhs)

**Paper:** Bala Kalyanasundaram, Kirk Pruhs, *Speed Is as Powerful as Clairvoyance*, J. ACM 47(4), 2000, pp. 617-643. **NOT FETCHED (paywalled)**; note based on second-hand summaries (secondary sources marked; no primary text verified — see Weaknesses). Status: unavailable.

## Claim (second-hand)

The paper's central slogan is in its title: for scheduling jobs on a single machine, an **online algorithm with a (1+ε) speed advantage** (i.e., resource augmentation) achieves bounds comparable to an **omniscient offline algorithm**. Two headline results, as widely cited:

- **Shortest-Elapsed-Time-First (SETF)** with (1+ε) extra speed is **O(1)-competitive** (constant depending on 1/ε) for mean/weighted flow time and throughput objectives;
- With the **Latency = Length** idea (a job's latency is charged proportional to its processing length), the algorithm is **constant-competitive without any extra speed**.

The literature usually cites this as the canonical **resource-augmentation** result: speed can *simulate* clairvoyance.

## Relevance

The relevance here is **architectural, not algorithmic**. It explains why `-DLOOKAHEAD`'s full-horizon simulation is *asymptotically* the wrong resource to spend: the theory says that if we had a modest speed margin we would not need lookahead at all, and conversely (from 302/305) lookahead's value is capped at small horizons. In our protocol the "speed" is our **admission aggressiveness relative to slack**: each tick of unused decode capacity is wasted slack (a bubble), and granting earlier uses the same resource more effectively — the equivalent of running "faster". Two takeaways:

1. **Prefer using capacity now over predicting later.** The SETF intuition (serve the job with the least elapsed service) maps to *never let the decode pool idle while a candidate exists*; any lookahead tier must not make the resource idle while waiting for a "better" job — exactly the risk of over-gating (303's ρ>1 accepting small losses is the antidote).
2. **Bound the lookahead budget by the speed margin.** Since our skeleton gives no true clairvoyance channel (302/305 caveat), the practical translation is: the worth of each extra horizon tick ≈ the marginal capacity it *could* have used — which, after κ=1-2, is dominated by the resource's own utilization. Hence the report's recommendation to shrink `HORIZON` (305) rather than extend it.

## Algorithm sketch (second-hand)

```
SETF: at each step, run the job with the least amount of processing
completed so far (i.e., shortest elapsed time). With a (1+ε)-speed
machine, SETF is O(1)-competitive (constant ~ 1/ε or its log, per
the paper's proofs as summarized in the literature).
Latency = Length: bound the delay of each job by a multiple of its
own length, not by the total workload; this gives constant
competitiveness without speed (as summarized).

```

## Port plan

- **No code port.** The actionable output is a constraint on the tranche stack: the lookahead/gate variants (302/303/305) must never leave the decode pool idle when an admissible candidate exists; i.e., `select_actions` fallback to the greedy-EDD tier whenever the gated/simulated tier declines to act (guaranteed by the existing `run_tick_policy` fallback — verify in the audit that it fires in the no-op case).
- **Audit metric**: count per-tick idle decode capacity in the suite vs. the gated variants; if gating raises idle time >2%, the ρ/horizon knobs are trading away the speed advantage and the gate is over-tuned (a measurable, non-code check).

## Weaknesses

- **Not verified from primary text** (paywalled; the JACM version was not fetchable, and no reliable free copy was located). All numbers/statements above are second-hand as summarized in surveys (see Sources); exact constants (the O(1) exponents, the 1/ε dependence) are *not* trustworthy — treat the paper as a *pointer*, not a source of quotable numbers.
- The model is **preemptive single-machine flow-time/throughput**, no deadlines and no two-resource coupling; the resource-augmentation equivalence does not transfer to *coupling slack* (our edge/cloud handoff) — it is about raw processing speed, not structure.
- SETF itself (shortest elapsed first) is **antithetical** to our EDD/weight-based policy on its face; the transfer is the *principle* (utilize now), not the policy.

## Sources

- Kalyanasundaram & Pruhs, *Speed Is as Powerful as Clairvoyance*, J. ACM 47(4):617-643, 2000 (paywalled; no primary copy in research/papers/).
- Second-hand confirmations: citations in Bansal (survey on online scheduling, 2010, slides §resource augmentation), and in the KP-lineage packet-scheduling literature (cf. notes 302/303's cited refs); numbers above are only the qualitative statements those sources agree on.
- `docs/04-scheduling-theory-toolkit.md` (existing theory base; EDD/Johnson context).
