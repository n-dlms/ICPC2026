> **Post-contest archive note (Oct 2026):** Pre-contest preparation completed. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). Checklists, TODOs, and "to verify / will / pending" items below are preserved as historical pre-contest working notes. Anything still open is a Post-contest follow-up (not part of the #56 run). Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 399 — Lookahead Research Report (ICPC2026 scheduling)

Status: research phase completed pre-spec. Six papers surveyed; five notes + this report in `research/notes/`. No code was written in this phase; all ports below were recommendations evaluated after the real spec arrived (outcomes in `research/notes/experiments-log.md` and `docs/DECISIONS.md`).

## Papers

| #   | ID / ref                           | Title                                                                                                                                              | Status                                   | Note                                                       |            |     |
| --- | ---------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------- | ---------------------------------------------------------- | ---------- | --- |
| 1   | arXiv:2306.06003                   | Semi-online Scheduling with Lookahead (2LA1; 2-mach LB 4/3, matching 1-lookahead alg; threshold `l1+pi ≤ (2/3)(l1+l2+pi+p_{i+1})`)                 | fetched ok                               | 301                                                        |            |     |
| 2   | arXiv:1606.09395                   | Online Packet Scheduling with Bounded Delay and Lookahead (BDPS; ToggleH φ; CompareWithBias 1-lookahead R≈1.303; LB≈1.281)                         | fetched ok                               | 302                                                        |            |     |
| 3   | arXiv:2305.07164                   | Learning-Augmented Online Packet Scheduling with Deadlines (LAP; ρ-gated prediction following; 1-consistent ⇒ ≥2-robust)                           | fetched ok                               | 303                                                        |            |     |
| 4   | arXiv:1711.00081                   | 2-Machine Flow Shop with Exact Delays Taking Two Values (F2                                                                                        | exact l_j∈{0,L}                          | Cmax: (1.25−ε)-inapprox; 2-approx; equal-delay O(n log n)) | fetched ok | 304 |
| 5   | Goldwasser, SODA 1999              | Patience Is a Virtue: Greedy Admission Control (greedy-EDD (2+1/p)-comp; **1/κ lookahead tradeoff**; earliest-available optimal for equal lengths) | fetched (curator copy)                   | 305                                                        |            |     |
| 6   | Kalyanasundaram & Pruhs, JACM 2000 | Speed Is as Powerful as Clairvoyance (SETF; (1+ε)-speed ⇒ O(1)-comp)                                                                               | **unavailable** (paywalled; second-hand) | 306                                                        |            |     |

All fetches verified: `research/papers/*.pdf` + `.txt` present for 1-5, manifest `ok`; 6 recorded as unavailable, no primary text.

## Top-3 findings by score leverage

1. **Lookahead value is front-loaded: 1 tick of lookahead closes most of the worst-case gap** (notes 301/302/305: matching 1-lookahead algs at LB 4/3; BDPS ratio 1.618→1.303 with 1 lookahead; Goldwasser's 1/κ tradeoff). → **Shrink the `-DLOOKAHEAD` horizon from T to 1-2 ticks**: same score tier at ~T× less per-tick simulation cost, freeing the global time budget (the #1 suite-gate risk per docs/DECISIONS.md).
2. **The acceptance gate is the load-bearing structure, and it must be transactional + ratio-based** (notes 302/303: CompareWithBias's min-threshold plan test; LAP's local test `W(Opt)/W(achieved) ≤ ρ`; the probe-experiment regression proved non-transactional acceptance loses ~20 score). → **Upgrade the accept rule to a ρ-scaled ratio gate on `base_score`** (ρ=1 = current strict not-worse; ρ>1 = small-loss tolerance); keep exact `apply_action_set` legality. Zero horizon cost.
3. **Ordering heuristics have provable floors: greedy-EDD is 2-competitive, and equal-length cases are exactly optimal under earliest-available** (notes 304/305: Johnson partition trick; Goldwasser Thm 1 + p.13 optimality). → **Keep greedy-EDD as the instant-fallback tier and add a two-level (lag-class, EDD) ordering variant** (`-DCMP2`); use the equal-length audit ratio as a canary for ordering bugs.

## Recommended port (ONE)

**Goldwasser-style depth-1 lookahead margin gate** replacing the full-T simulation in `select_actions`:

- **Idea**: evaluate each candidate by simulating **only 1-2 ticks ahead** (not to T), and accept iff `sim_score(candidate) ≥ base_score / ρ` with ρ ∈ {1.0, 1.02, 1.05, 1.1} (knob; default 1.02). This is the 302/303 ratio-gate shape with the 305 horizon bound; the existing est-filtered candidate list, K top candidates, and transactional `apply_action_set` stay untouched; the greedy-EDD `run_tick_policy` remains the unconditional fallback (306: never idle the decode pool).
- **Complexity**: candidate selection O(n log n) (existing); per-tick simulation cost drops from O(K·T) to **O(K·H), H ≤ 2** (vs. current full-T); gate is O(1) per candidate. Worst case per tick: O(n log n + K·H) — strictly bounded, no clock(), safe answer always available (fallback tier).
- **Expected effect**: same score tier as full-horizon lookahead on most cases (value front-loaded per finding 1) at a fraction of the time cost; the ρ>1 gate buys small-loss admissions on tight/burst cases (finding 2) that the ρ=1 gate rejects.
- **Suggested tranche budget**: `-DCMP2` and `-DGATE ρ∈{1,1.02,1.05,1.1}` × `-DLOOKAHEAD H∈{1,2}` under the existing suite gate; freeze a safe submission first.

## Risks / mis-transfers

- **No true lookahead channel in the skeleton** (arrivals revealed at t): the 1/κ and 1.303 numbers are *parameters of the algorithm*, not achieved in-protocol ratios — do not claim them as guarantees; the port relies on the gate/horizon shape, not on the constants.
- **Model gaps**: all papers are single-machine/unit-job/deadline-free or 2-machine-without-batches; our two-resource coupled tokens with per-request lengths break exchange arguments — treat every bound as a *floor/ceiling warning*, never a tight bound. In particular the (1.25−ε) inapproximability of 304 does **not** directly cover our ≥-constraint model (delays are lower bounds, fixed per case ⇒ closer to the polynomial equal-delay case).
- **Over-gating risk** (303/306): ratio gates can idle the decode pool; mitigate with the fallback tier and the per-tick idle audit metric (≤2% budget).
- **LAP's test uses an exact prefix optimum** (unit-job matching); our prefix problem is NP-hard, so our ρ-test runs against a *heuristic* plan — the ρ+γ_on+1 robustness bound does not hold for us; ρ is an empirical knob, not a theorem parameter.
- **Unverified secondary source**: KP 2000 (306) is second-hand; no quotable constants.

## Files

- Notes: `research/notes/301-`…`306-`, this report. Sources: `research/papers/*.pdf|.txt` + `manifest.json`.
- Pending: day-1 checklist in `docs/05-strategy-playbook.md` once the spec drops; then implement port + tranches per docs/DECISIONS.md.
