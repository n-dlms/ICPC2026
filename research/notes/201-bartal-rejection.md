> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 201 — Multiprocessor scheduling with rejection (Bartal et al. 2000)

**Claim.** Online scheduling on m identical machines where each job may be *rejected* for a penalty; objective = makespan(accepted) + Σ(penalties of rejected). A threshold rule that rejects a job when its penalty is small relative to its processing time is provably near-optimal: (1+φ)≈2.618-competitive for all m (matching lower bound), and **φ≈1.618-competitive for m=2, which is best possible**. Offline: FPTAS for fixed m, PTAS for arbitrary m, and an O(n log n) 2−1/m approximation.

**Relevance.** This is the canonical theoretical model of *admission control*: under overload you must reject some requests, and the question is which ones, with what threshold, with no knowledge of the future. Our interactive judge is exactly the "jobs arrive one by one, decide immediately, cannot revoke" setting (minus the penalty term — our penalty for rejecting a request is the implicit opportunity cost of never completing it, and the real score only rewards *completed on-time* requests). m=2 case maps to our two sites (edge, cloud).

**Algorithm sketch** (from the paper, p. 67):

- `B = {j : w_j ≤ p_j/m}` (penalty ≤ per-machine load). Rule: reject j ∈ B outright.
- RTP(α): maintain W = total penalty of rejected jobs; for j ∉ B, reject iff W + w_j ≤ α·p_j; else accept and list-schedule on least-loaded machine.
- For m=2: RP(α) — reject iff w_j ≤ α·p_j; with α = φ−1 ≈ 0.618 it is φ-competitive; RTP with α=√2/2 ≈ 0.707 is (1+√2/2)≈1.707-competitive.
- Lower bound ρ for m machines solves ρ^(m−1)+…+1 = ρ^m; m=2 gives ρ=φ.

**Port plan.** Maps to a *dooming threshold* in `admit_prefills` (src/Solution.cpp:123). Today a WAIT request is doomed only when `est > min(D,T)` (serial lower bound, line 136). The theory says: under overload, also doom feasible-but-"expensive" requests once the accumulated rejected work W exceeds a threshold fraction of their processing time — i.e., replace the binary est-check with `W + work(r) ≤ α·work(r) ⟺ W ≤ (α−1)·work(r)` (reject when W is small relative to r's work).
In our objective every on-time completion is worth 1, so "penalty" is uniform → the rule degenerates to: reject the longest-work requests first, which is precisely the Moore–Hodgson/SPT intuition (note 202/205). The m=2 φ-optimality is a sanity bound on how much any online admission policy can gain over clairvoyant — it justifies keeping admission *simple* and investing instead in ordering.

**Weaknesses (transfer gaps).**

- No release times, no deadlines, no SLOs in the model: the "deadline" is folded into a makespan term. Our D is a hard per-request deadline, which changes the reject-or-accept calculus (deadline + work jointly determine feasibility).
- No two-site pipeline: "machine" here is an abstract identical parallel machine; our request occupies edge *and* cloud in alternating windows. The rejection rule has no notion of the bubble/staging structure.
- Competitive analysis is worst-case adversarial; the local suite is random/bursty. The constants (φ, 1.618) tell us the *shape* of the best policy (threshold on work), not that the shape helps on our distribution — A/B required.

**Sources.** arXiv: n/a (journal only). Fetched copy: TU/e pure repository (Version of Record), `research/papers/bartal2000msr.pdf`; original: Y. Bartal, S. Leonardi, A. Marchetti-Spaccamela, J. Sgall, L. Stougie, *SIAM J. Discrete Math* 13(1):64–78, 2000, DOI 10.1137/S0895480196300522. Algorithm RTP(α) p. 67; Theorems 3.1–3.3 pp. 67–71; m=2 lower bound §4.1 p. 73.
