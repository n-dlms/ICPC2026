> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 507 — Open Shop Scheduling with Delays (Rayward-Smith, 1992)

- Status: **unavailable** — 1992 paper, Springer/Elsevier (Discrete Applied Mathematics), not on arXiv (arXiv started 1991 and this is not in the catalog). Distilled from `src/opencode.md` lines 85-89. Record DOI when an open copy is located; mark `unavailable` in `research/papers/manifest.json`.

## Claim

Open shop scheduling with uniform delays is **NP-complete even with just 2 machines and uniform delays** (Rayward-Smith 1992).

## Relevance (theoretical foundation — justifies the portfolio)

This is the **NP-completeness proof that justifies the entire portfolio architecture** (see src/ARCHITECTURE.md). The ICPC edge-cloud U-shape with bidirectional edge↔cloud communication is a 2-machine open shop (machine E, machine C) with communication delays. With delays, it is NP-complete — so any polynomial-optimal claim by a competitor is false.
Our heuristic portfolio (Engine 1 greedy / Engine 2 LNS / Engine 3 flow / Engine 4 frozen) is the theoretically correct response; exact optimal is not on the table except on tiny instances (`tools/opt_gap.py` branch-and-bound).

The deeper trap (opencode Trap C line 11-16): if the spec uses **asymmetric** edge→cloud vs cloud→edge delays, Johnson's rule and simple 2-machine flow-shop heuristics break. The frozen solver must remain robust to asymmetric delays — verify with `tools/engine0_probe.py` when the spec is reconciled.

## Algorithm sketch

This is a *theorem* paper, not an algorithm paper. The proof structure is a reduction from PARTITION (or similar NP-hard problem) to 2-machine open-shop-with-delays. The takeaway for us:

- Any "optimal polynomial algorithm" claim for our problem is a hallucination (a model that does not understand the problem — the archived working discipline, see README.md).
- Tiny instances (N ≤ ~15) can be solved exactly by `tools/opt_gap.py` branch-and-bound; large instances need heuristics.
- 2-machine open shop with delays has a known high-quality approximation (Gonzalez-Sahni for zero delays, extended heuristics for uniform delays) that can serve as a sanity baseline for `tools/opt_gap.py` — if our frozen solver beats it consistently, the portfolio is sound.

## Port plan

No code. Use as the **archived citation (see src/ARCHITECTURE.md)** (sharpen the archived "Portfolio Architecture" sentence to cite Rayward-Smith 1992 for why heuristics are unavoidable). Add a one-line check in `tools/reconcile_spec.py` that flags if the spec declares symmetric delays (then the simpler Gonzalez-Sahni 2-machine open-shop algorithm is a candidate Engine 3) vs asymmetric (then only heuristic portfolios apply).

## Weaknesses

- The proof is for a *static, offline* open shop (all jobs present at t=0). Our problem is online (requests arrive over time). Online NP-hardness is at least as hard but the specific 1992 proof does not directly address arrivals.
- 2 machines is the easy case; the spec may have multiple cloud machines (e.g. one prefill cloud, one decode cloud) which is m-machine open-shop and at least as hard.
- The specific delay model (uniform, integer) may or may not match the spec — `protocol/protocol.yaml` A3 (communication model: latency vs capacity) is UNKNOWN until Aug 14. NEVER assume uniform delays before reconciliation.

## Sources

- `src/opencode.md` lines 85-89, Trap C lines 11-16.
- Rayward-Smith, "Open Shop Scheduling with Delays", Discrete Applied Mathematics 1992 (venue unverified).
