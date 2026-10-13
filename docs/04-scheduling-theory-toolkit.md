# 04 — Scheduling Theory Toolkit

> **Historical note (post-contest).** I compiled this toolkit before the contest to guide my design. The competition ended Aug 28, 2026 — I finished **#56 worldwide with 16,147.866/22,000**. The theory below informed the solver that reached #56; I (Ntokozo Dlamini) originated the selection and application of these ideas, and used AI to speed up research summaries and checks.

Classical scheduling results I used for the challenge: two heterogeneous compute sites (edge, cloud) with communication delays between alternating stages, release times, latency SLOs (deadlines), throughput maximization. URLs from web research I ran.

## Framing

Each request is a chain of stages alternating between two machines (edge `E`, cloud `C`) with a communication delay on every handoff → a **flow shop with release times, due dates, and exact inter-machine delays** (`F2|r_j, d_j, l_j|...`) mixed with a throughput objective.

Each request is a *chain* (not a general DAG) → much easier than general job-shop.

General two-machine job shop is NP-hard; open shop with release dates + Lmax is NP-hard (Kellerer–Tautenhahn–Woeginger).

Reference textbook: Lenstra & Shmoys, *Elements of Scheduling* (arXiv:2001.06005).

## 1. Two-machine flow shop (Johnson's rule) — core skeleton

- **Johnson (1954), `F2||Cmax` solved in O(n log n):** split jobs into A = {p₁ ≤ p₂} ascending p₁, then B = {p₁ > p₂} descending p₂. An optimal *permutation* schedule exists; problem is reversible.
- **Why it matters:** for a batch of requests with one machine per site, Johnson's sequence minimizes makespan and, more usefully, minimizes **idle time on the downstream machine** — the same object as pipeline bubbles. Modern linear-time variants exist (Della Croce & Schau 2025, arXiv:2512.06119).
- Extensions: with unavailability periods → 2-approx heuristic (modified Johnson optimal under conditions); with no-wait/no-idle constraints → Gilmore–Gomory; with **exact delays** between coupled operations ("compute then wait-exactly-L then compute") → constant-factor approximations via dual approximation (Dell'Olmo, FOCS 1985). Communication-delay generalization with precedence → Chudak–Shmoys LP technique (2-approx-style results).

## 2. Single-machine due-date models — the per-site workhorse

- **EDD (Jackson 1955)** minimizes Lmax on one machine (`1||Lmax` optimal; proof by inversion/exchange).
- **1|prec|Lmax:** if deadlines consistent with precedence (dⱼ ≤ dₖ when j≺k), EDD still optimal; else **Lawler's algorithm** (1973): backward pass choosing maximal-completion job with smallest cost, O(n²). Generalized to `1|prec, r_j, pmtn|fmax` by Baker–Lawler–Lenstra–Rinnooy Kan (1983), O(n²).
- **Moore–Hodgson (1968)** minimizes the *number of late jobs* (`1||ΣU_j`): scan EDD order, when a prefix goes late remove the longest job (max-heap), O(n log n). **This is the admission-control algorithm.** Modern O(n log n) deadline-tree implementations exist (arXiv:2607.16347).
- **E/T scheduling** (earliness/tardiness): mental model for "tight vs loose SLO windows".

## 3. Online scheduling with release times

- **Graham's list scheduling (1966):** assign each arriving job to least-loaded machine → competitive ratio `2 − 1/m` for `P||Cmax`; tight for m=2; with precedence constraints it's a 2-approx (believed optimal for the class). Wins on random workloads; fancier algorithms win adversarial.
- **Noga & Seiden (TCS 2001):** optimal online algorithm for exactly **2 machines with release times** — directly relevant (edge, cloud).
- **Smith's rule / SPT:** `1||Σw_jC_j` minimized by non-increasing w_j/p_j (WSPT); unweighted = SPT, minimizes mean flow time → maximizes completions-per-time. Parallel version: Kawaguchi & Kyan tight ratio `(1+√2)/2 ≈ 1.207` for `P||Σw_jC_j`.
- **EADD** (Moore–Hodgson-derived, Lodree et al. 2004): for *maximizing on-time jobs in a dynamic flow shop*, SPT is the strongest simple rule; EADD beats SPT by 16–20% — closest published analog to "max throughput under deadlines in a 2-machine flow shop".

## 4. SLO handling: feasibility, deadlines, admission

- **Feasibility:** non-preemptive, no releases → EDD feasible iff no lateness in EDD order. Preemptive with releases → **Horn's algorithm** decides `1|pmtn,r_j|Lmax` in O(n log n) (preemptive EDF forward run with deadline back-check). `1|r_j|Lmax` non-preemptive is strongly NP-hard (LRK 1977); best approx 4/3 (Potts); PTASes exist (Kacem–Kellerer 2024). **I treated feasibility-with-releases as easy only where preemption was allowed** — I kept preemption in the model where the spec allowed it.
- **Real-time theory:** on a preemptive uniprocessor **EDF is optimal** (Dertouzos 1974); periodic task set schedulable iff `U = Σ C_i/T_i ≤ 1` (Liu & Layland 1973); QPA test for arbitrary deadlines (Zhang & Burns 2009). For the two sites: utilization accounting `U_E + U_C ≤ 2` is the first-order throughput ceiling.
- **Converting SLOs into deadlines:** `d_j = arrival + SLO`. Stage sub-deadlines proportional to remaining work, or **operational due dates** (Lodree's EADD): stage deadline = arrival + SLO − (remaining stages' worst-case service + remaining comm) → local EDD dispatch was globally SLO-aware in my design. I kept headroom: scheduled to `SLO − ε`.
- **Throughput ceiling:** max sustainable arrival rate = `1/max(stage-time per site + comm share)`; Little's law `L = λ·W`; sanity bound I used: if `λ·(edge + cloud service + comm) > 1`, no schedule met all SLOs — my admission control shed load.

## 5. Contest/CP-style techniques

- **Greedy + priority queues:** per-site ready queue keyed by adjusted deadline (EDD/operational due date); tiebreak SPT for throughput. All implementable in contest time: EDD O(n log n), Lawler O(n²), Moore–Hodgson O(n log n), Horn O(n log n).
- **Rolling / receding horizon:** fix a window of next W arrivals, solve small offline problem (Johnson ordering + EDD) within the window, commit first slot, slide. Freezing started stages cuts re-solve cost with little quality loss. Event-driven + cycle-driven re-trigger is the standard pattern.
- **Bottleneck analysis + lower bounds:** any feasible schedule has (1) per-site load bound `Cmax ≥ max(ΣE-work, ΣC-work + initial comm)`, (2) critical-chain bound = longest edge↔cloud alternating path, (3) Graham-type bound `Cmax ≤ 2·max(max p_j, Σp_j/m)`. Binary-search the max admissible arrival rate (Hochbaum–Shmoys dual approximation: guess T, check feasibility, adjust).
- **LP relaxation + rounding:** `R||Cmax` LP (Lenstra–Shmoys–Tardos) gives 2-approx; only worth it as a lower bound/tiebreaker in practice.
- **Constraint generation / local search:** simulate greedy → find SLO-violating request → bump its priority / block conflicting pair → re-simulate. Cheap and effective in simulator-based judging.

## 6. Bubble-minimizing pipeline designs

- **Steady-state pipeline rate:** with stages alternating edge/cloud and per-hop comm `c`, throughput ceiling = `1/max(t_E, t_C, t_comm-share)` — compute-bound when `max(t_E,t_C) > t_comm`, communication-bound otherwise. Pipelining removes bubbles only up to this bound; fill/drain analysis: first request pays full fill time, steady state pays max-stage-time per request (1F1B analysis).
- **Bubble ratio:** for N stages with m microbatches, `(N−1)/(m+N−1)` — the "idle bubble fraction" per site.
- **Interleaved schedules:** with 2 machines, keep both busy by having **≥2 in-flight requests** ("two-wave"); optimal steady-state rate achieved by a *periodic* schedule of period = max stage/comm time.
- **Communication–computation overlap (double buffering):** transmit chunk k+1 while computing chunk k; benefit proportional to ρ = T_comm/T_total, worth it when ρ ≳ 20%. I modeled the link as a third concurrent "machine" and never let it sit idle while either site had work.
- **Throughput-optimality (SLAI-style):** any *work-conserving* algorithm achieves max throughput iff load condition holds (`λ(m_p + m_d) < t_b/b`) — **I never idled a machine with ready work**.
- **SLO-driven rate allocation:** prioritize by utility, then modulate batch sizes to meet per-request TPOT deadlines (SLICE, arXiv:2510.18544 — up to 35× SLO attainment under overload; closest published analogue to this challenge's edge setting).

## Implementable algorithm list (with complexities)

| Algorithm | Use | Complexity |
| --- | --- | --- |
| Johnson (F2Cmax) | Batch ordering, minimize downstream idle | O(n log n) |
| EDD | Minimize Lmax on one site | O(n log n) |
| Lawler | 1\|prec\|Lmax with precedence | O(n²) |
| Moore–Hodgson | Admission: minimize late jobs | O(n log n) |
| Horn / preemptive-EDF | Feasibility with release times (if preemption allowed) | O(n log n) |
| Graham list scheduling | Online least-loaded assignment | O(n log n) |
| Rolling horizon | Windowed re-optimization | per-window solve |
| Hochbaum–Shmoys | Binary search max admissible rate | O(log T · check) |
| Constraint generation | Iterative greedy + repair | heuristic |

## Key sources

- Johnson rule: arXiv:2512.06119
- Lawler's algorithm (Wikipedia; J. Scheduling 2015)
- Horn: ACM 10.1145/322326.322337
- Moore–Hodgson: arXiv:2104.06210, arXiv:2607.16347
- Kacem & Kellerer (Lmax + rejection PTAS): Springer 10.1007/s10878-024-01205-y
- Liu–Layland: ACM 10.1145/321879.321884
- QPA: York QPA.pdf
- Graham list scheduling: Springer encyclopedia entry
- Noga & Seiden: Springer 10.1007/978-0-387-30162-4_205
- Kawaguchi & Kyan: UT-wente ORL 2013
- Lodree EADD: EJOR S0377221703004041
- pipeline bubble bounds: inria.hal.science/hal-05353232v2
- Sarathi-Serve: usenix.org/system/files/osdi24-agrawal.pdf
- DistServe: usenix.org/conference/osdi24/presentation/zhong-yinmin
- FastServe: usenix.org/system/files/nsdi26-wu-bingyang.pdf
- SLICE: arXiv:2510.18544
- work-conserving throughput condition: arXiv:2504.07347
- coupled tasks with delays: ACM 10.1109/SFCS.1985.63
- Hochbaum–Shmoys: ACM 10.1145/7531.7535
- rolling horizon: EJOR S0305054822002167
- Elements of Scheduling: arXiv:2001.06005
