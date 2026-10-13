> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 106 — Any-Order Online Interval Selection (windowed slots with revocation)

**Claim.** Online interval scheduling on a single machine where intervals arrive in *adversarial order* (not sorted by start time) and accepted intervals may be revoked: (i) unweighted, general lengths — **unbounded** (no constant competitive ratio possible, from Garay et al.'s call-control lower bound); (ii) with at most k distinct interval lengths — the simple greedy **"take if conflict-free; else take only if the new interval is properly contained in an existing one (displacing it)"** is **2k-competitive and optimal** among deterministic algorithms (and memoryless randomized);
(iii) connection to call control on the line: unweighted O(log n), proportional weights (2+√5)-competitive (Garay et al.); (iv) under *random* arrival order, only "one-directional" replacement algorithms can benefit over adversarial order. (Borodin–Karavasilis, arXiv 2023.)

**Relevance.** This is the theory of *our* slot structure: the time line with capacity is exactly a line of windows; our planned bookings are intervals; and revocation of not-yet-started bookings is legal in our protocol (note 102's legality argument). The **containment-only displacement rule** is the key transfer: when a new request's window conflicts with booked windows, only displace if the new window is **properly contained** in the booked one (per resource), i.e. the displaced request is *larger* than the displacer. Consequences that matter for us:

- It gives a **safe, bounded-churn displacement policy** with a formal (if weak, 2k) guarantee. k = number of distinct stage lengths in our system is small and bounded (PEDGE, PCLOUD, pe(Lin) ∈ [0, ALPHA·Lin·PSCALE], pc(Lin)) — with Lin bounded, k is bounded.
- It identifies the *nesting depth* as the true difficulty parameter (d ≤ k−1) — nested bookings (large prefill containing small prefill inside) are the only genuinely dangerous conflicts.
- Unboundedness of the general case is a red flag: an unrestricted displace-anything rule (naive b-matching flip of note 102 without discipline) has no worst-case guarantee — containment-only is the provably safe core, arbitrary displacement must stay a budgeted experiment.

**Algorithm sketch.** Algorithm 1 (p. 8): on arrival of interval I, let Is = intervals in the current solution conflicting with I. If Is = ∅, take I. Else take I iff ∃ I′ ∈ Is with I ⊊ I′ (proper containment); remove that I′. Ties/lengths arbitrary. Analysis: containment reduces nesting depth by 1 per accepted replacement; a length-chain argument yields the 2k bound (Theorem 3.2); matching lower bound for any deterministic algorithm (Theorem 3.6, incl. memoryless randomized).

**Port plan.** `-DCONTAIN` in `admit_prefills` (Solution.cpp:123) and `grant_decodes` (Solution.cpp:96): when `free_window` fails for candidate id, gather the conflicting booked owners; attempt displacement **only if the candidate's edge window [t, t+pe) is properly contained in the owner's edge window (and the cloud window likewise, when pc > 0)** and the owner is still WAIT-phase (booking not started) and has a later feasible re-booking (est re-checked, line 135);
then un-book owner, re-book owner at its next feasible start, book candidate — transactionally, exactly one owner displaced per candidate (single-displacement version; multi-hop = note 102's BFS upgrade).

- This is deliberately conservative: proper containment guarantees the displaced owner has strictly more slack span than the candidate, so displacement churn cannot cascade through same-size bookings.
- A/B against baseline and against `-DAUGMENT` (note 102) as the two ends of the displacement spectrum.

**Weaknesses / mis-transfers.**

- **Single machine, capacity 1, fixed start times** — our windows are *choosable* (start time is our decision, capacity Ecap/Ccap > 1, two resource layers at fixed offsets). The containment rule must be applied per-resource; the 2k bound is a worst-case for a strictly harder model and does not certify our two-layer version.
- Unweighted cardinality objective vs. our deadline+latency score; nothing here handles SLOs — displacement must be gated by the displaced request's own deadline check (it is, in the port plan).
- "Take if conflict-free" is naive for us: with capacities > 1 a "conflict" is capacity overflow, and taking everything until overflow is what our greedy already does; the value of the paper is the *replacement discipline*, not the acceptance part.
- Their adversarial arrival order is strictly worse than our A-sorted arrivals; so the guarantee is a lower-bound sanity check, not a prediction of score on our cases. Do not expect 2k-level gains; expect churn discipline.
- Revocation in the paper discards the displaced interval *permanently*; ours re-books it later — more permissive, and the theory does not cover re-booking, so the re-book feasibility gate is on us (it is, transactionally).

**Sources.** Borodin & Karavasilis, "Any-Order Online Interval Selection", arXiv:2303.06127 (`research/papers/2303.06127.txt`; Algorithm 1 p. 8, Theorem 3.2 and 3.6 §3, call-control connections §3.2, random-order results §5). Related paywalled: Garay, Gopal, Kutten, Mansour, Yung (INFOCOM'97, call control on the line); Woeginger (2003, ordered-case weighted interval selection, 4-competitive); Emek–Halldórsson–Rosén (streaming/online interval selection, 1/6-competitive randomized).
