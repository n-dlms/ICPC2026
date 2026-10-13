> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 304 — Two-Machine Flow Shop with Exact Delays Taking Two Values (Inapproximability)

**Paper:** Alexander Ageev, *2-Machine Flow Shop with Exact Delays Taking Two Values*, arXiv:1711.00081 (2017). Fetched OK (research/papers/1711.00081.pdf, 2.1 KB text).

## Claim

Considers F2|exact l_j ∈ {0, L}|C_max (two-machine flow shop where job j has an *exact* delay l_j between the end of its operation on machine 1 and its start on machine 2, taking only two values 0 or L). Main results: **no (1.25 − ε)-approximation unless P = NP** (Thm 1, p.6), by reduction from 3-SAT/independent-set instances; a **2-approximation** (Thm 4, p.7-8) via partition into 0-delay and L-delay classes and concatenation; and the **equal-delay case** (all l_j = L) is solvable in **O(n log n)** (p.4) by transforming to the no-wait/zero-lag problem (reducible Johnson).

## Relevance

Our prefill/decode flow is exactly a 2-machine flow-shop-with-lags in miniature: "machine 1" = the prefill admission slot, "machine 2" = the decode slot, and the *edge+cloud handoff lag* is a per-request delay between the end of prefill and the start of decode. The two-value special case (0 or L) is precisely the two patterns of our requests (those that can couple a prefill with an immediately following decode window vs those whose decode starts after a longer latency budget). Three deliverables:

1. **(1.25−ε)-inapproximability is a warning about plan-quality.** The offline problem of *maximizing admitted weighted jobs under coupled 2-machine-with-lag constraints* contains this hard core; therefore the "full-information re-optimization" tier of the architecture (docs/05, strategy playbook step 4) must be a **budgeted local search over a greedy feasible incumbent**, not an exact solver. The theoretical gap justifies our never-force philosophy: no polynomial plan can certify optimality, so a feasible schedule is worth more than a "better" but infeasible one (mirrors the probe experiment, note 303).
2. **The 2-approx partition trick is a scheduling heuristic with a proof.** Concatenate 0-delay jobs then L-delay jobs (or vice versa), each internally scheduled by Johnson's rule; the proof (Thm 4) bounds the gap by the structure of the partition. Port: **batch prefill admission by (delay-class, EDD) order** — admit prefills grouped by their decode-lag class, inner order by deadline — which is a cheap O(n log n) priority that the current cmp_key approximates with a single key; a two-level key (lag-class then deadline) is a drop-in `-DCMP2` variant.
3. **The equal-delay O(n log n) case** gives a benchmark: when all requests share a homogeneous lag structure, Johnson's exact algorithm *is* optimal for makespan — our greedy's closeness to Johnson ordering on homogeneous cases is measurable (an audit metric, not a code change).

## Algorithm sketch

```
2-approx (Thm 4): split jobs into J0 (l_j=0) and JL (l_j=L);
schedule J0 first by Johnson on (a_j, b_j), then JL by Johnson;
C_max ≤ 2·OPT (analysis via partition exchange, p.7-8).
Equal-delay: transform to no-wait flow shop (job2-start = job1-end + L),
solve by reduced Johnson in O(n log n) (p.4).

```

## Port plan

- Variant `-DCMP2`: change the prefill candidate ordering in `select_actions` to `(decode_lag_class, edd)` instead of a single composite key; measure under the tranche budget against `-DLOOKAHEAD` and the ρ-gate variants (302/303). Zero interaction with the admission gate; only ordering changes.
- Use the equal-delay Johnson benchmark in the *audit* (not in code): on synthetic homogeneous-lag cases, report `|Cmax(greedy) − Cmax(Johnson)|/Cmax(Johnson)` as a score-leverage diagnostic. If the gap is large on homogeneous cases, the two-level key earns a tranche.

## Weaknesses

- **Objective mismatch**: the paper minimizes makespan (C_max); our objective is dominated by weighted on-time admission, with latency terms as 1e-6 tiebreakers. Flow-shop makespan theory does not transfer quantitatively to admission-maximization; only the structural caution (inapproximability) and the ordering heuristic transfer.
- **Exact delays vs our latencies**: our decode lag is a *lower bound on time* (wait ≥ L), not an exact delay; the inapproximability proof uses exact-delay gadgets (independent-set encoding, p.6) that do not directly apply to ≥-constraints — the lower bound is thus a warning, not a theorem about our model. And our comm lag is fixed per case (single network), i.e. l_j ≈ constant: closer to the *equal-delay* polynomial case than to the hard two-value case (p.4).
- Two machines vs two *resource pools*: our decode side is a batch of concurrent windows, not a single second machine; coupling is 1:1 but the pools are multi-slot. Johnson's exchange arguments (a_j vs b_j ordering) assume a single machine each.

## Sources

- arXiv:1711.00081 (PDF + text in research/papers/). Thm 1 p.6 (reduction statement), Thm 4 p.7-8 (2-approx), equal-delay/Johnson p.4, no-wait reduction p.2-3.
- Background (no fetch): Johnson's rule — `docs/04-scheduling-theory-toolkit.md`; F2|l_j exact delays named after Potts; related exact-delay literature cited p.1-2 (see 301 for the lookahead analog).
