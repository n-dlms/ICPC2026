> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 101 — KVV: RANKING is Optimal for Online Bipartite Matching

**Claim.** For online bipartite matching (girls arrive one by one with their eligible sets; each is matched irrevocably to an eligible boy), the randomized RANKING algorithm — assign boys a random priority once, then match each arriving girl to the highest-priority eligible boy — achieves expected matching size ≥ (1 − 1/e)·OPT. This is optimal: no online algorithm (deterministic or randomized) beats (1 − 1/e + o(1))·OPT in the worst case (Theorem 2, p. 357).
The naive RANDOM variant (pick uniformly at random among eligible boys) is shown to be almost as bad as deterministic greedy (n/2 + o(log n), pp. 352–353), despite looking equivalent at first glance.

**Relevance.** This is the canonical anchor of the "online assignment" line (my area). The interesting part for us is *why* RANDOM fails and RANKING succeeds: a fixed random priority ordering creates a self-correcting mechanism that favors boys who have been eligible least often (pp. 354–355, the "duality principle" of Lemma 1). Practically, it justifies randomized/diversified tie-breaking on the *resource* side — exactly the "deterministic shuffle for diversification" that USA1 (doc 06) used empirically.
It also sets the hardness expectation: **irrevocable** online assignment without revocations is (1−1/e)-hard, i.e., the greedy-EDD floor is not far from the theoretical online limit; the gains must come from *revocation/augmentation* (notes 102, 106), not from better irrevocable matching.

**Algorithm sketch.** `Init: pick a random permutation π of boys (rank r_b = position in π). For each girl g arriving (with eligible set E_g): if ∃ b ∈ E_g unmatched, match g to argmax_{b∈E_g, unmatched} r_b.` Never revoke. Analysis via duality: equivalent to boys arriving in random order, matched to highest-ranked eligible girl.

**Port plan.** (a) A `-DRAND_TIE` variant: replace the `return a.A < b.A` tiebreak in `cmp_key` (Solution.cpp:77–94) with a seeded pseudo-random per-request priority (seed = case hash, not wall clock), keeping deadline/priority primary keys intact. This is the KVV lesson: randomize on the *slot* side, not the request side. (b) More ambitious: KVV-style slot-side priorities inside the LOOKAHEAD candidate ordering (Solution.cpp:276) to diversify the K=8 candidate set across ticks.

**Weaknesses / does NOT transfer.**

- **Irrevocability is the core of the model; we are legally allowed to revoke not-yet-started bookings.** Our planned windows (edge_occ/cloud_occ) are free to re-plan until the stage actually starts; the judge only sees emitted actions. So the 1−1/e barrier does not apply to us — we can exceed it with augmentations (notes 102/106).
- Unit-capacity unit-slots matching vs. our multi-stage chains (PE→comm→PC→comm→DW, then per-token DE→comm→DC→comm) on two resource types with per-request capacities Ecap/Ccap.
- Cardinality objective, perfect-matching assumption, oblivious adversarial arrival order; our judge is a fixed case file (no adversary), and our objective is deadline-count plus 1e-6·latency terms.
- Competitive-ratio guarantees do not bound per-case score; transferring RANKING wholesale would be cargo culting. Only the tie-break diversification lesson is load-bearing.

**Sources.** Karp, Vazirani, Vazirani, "An Optimal Algorithm for On-line Bipartite Matching", STOC 1990, pp. 352–358. DOI 10.1145/100216.100262. Paywalled at ACM; full text fetched from co-author's page (ics.uci.edu/~vazirani/KVV.pdf → `research/papers/kvv1990.txt`). Key results: RANKING definition p. 354; RANDOM's failure pp. 352–353; optimality Theorem 2 p. 357.
