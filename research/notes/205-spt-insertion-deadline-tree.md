> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 205 — Greedy minimization of the number of late jobs: deadline trees, optimal prefixes, tier matroids (Menezes & Plaxton 2026)

**Claim.** For 1||ΣU_j, Moore's *original* procedure — scan jobs from shortest to longest and accept a job exactly when the accepted set stays feasible (the SPT insertion rule, "never revoke an acceptance") — can be implemented in O(n log n) worst case via an augmented BST ("deadline tree") storing per-subtree total work + least intrinsic slack.
Moreover the rule is *lex-first optimal*: for every work threshold e it maximizes the number of accepted jobs with p ≤ e; optimal prefixes are exactly the minimum-work feasible sets of each cardinality; equal-work tie choices form laminar matroids (so tie-breaking = Rado–Edmonds greedy).

**Relevance.** This is the *only* provably-grounded admission rule that is **online-safe for our protocol**: Moore–Hodgson (note 202) revokes already-accepted jobs, which we cannot do once 'P'/'D' actions are emitted; the SPT insertion rule never revokes, so it maps 1:1 onto "decide admission at the moment of admission, then it's final". Lex-first optimality is exactly the throughput objective: under overload, prefer the many-small-work set over the few-large-work set, while never accepting a job that breaks feasibility of everything already accepted.
Our current admission (src/Solution.cpp:123) considers candidates in EDD order — the same order that is *suboptimal* for ΣU_j (the whole point of MH).

**Algorithm sketch** (from the paper):

- Feasibility of a set X ⇔ every deadline prefix is satisfiable: Proc(X_≤d) ≤ d for all deadlines d (Lemma 2.1). Equivalently the least deadline-prefix gap Slack(X) ≥ 0.
- Insertion criterion (Lemma 2.2): X + α feasible ⇔ `SlackX(α.d) ≥ α.p` where `SlackX(d) = min{d, Slack(X_≥d)} − Proc(X_<d)` — the *least affected gap* in the deadline suffix.
- Algorithm 1 (SPT-Greedy): sort by p asc; X = ∅; for each α: if X+α feasible, accept (never remove). Theorem 3.1: same accepted set as Moore's SPT procedure (hence also optimal in cardinality — via the Moore–Hodgson equivalence — and lex-first optimal).
- Deadline tree: balanced BST over deadlines, each node stores {total work of subtree, Slack of subtree}; Add(d,a)/Slack(d) in O(log(q+2)); total O(n log n).

**Port plan.** Recommended port target (see report 207). In `admit_prefills` (src/Solution.cpp:123):

1. *Comparator change:* order WAIT candidates by serial work `w = pe + COMM + pc + COMM + Lout·per` **asc** (ties: EDD, then USA1 desc, then A) — replacing the EDD-primary order at line 130 for admission only (leave `grant_decodes`'s cmp_key untouched). The per-candidate acceptance test stays exactly as-is (est ≤ min(D,T) at line 136 + free_window checks at 141–142), so legality is unchanged; only *which* requests get capacity when not all fit changes.
2. *Feasibility refinement (phase 2):* track the accepted-set slack `SlackX(d)` over a small deadline tree so that a candidate is admitted only if the whole accepted set + candidate stays feasible under the deadline-prefix criterion with d_j = min(D, T) and p_j = serial work. This is the literal insertion rule; the deadline tree is ~80 lines; or start with the naive O(n²) re-check per tick (n = WAIT pool is small in the skeleton).
A/B: `make` variant `ADMIT_SJF` vs baseline and vs LOOKAHEAD; watch c3_burst / gen_burst_* where admission order matters most.

**Weaknesses (transfer gaps).**

- Single machine, all jobs released at time 0, non-preemptive, work = one scalar. Ours: release times (A), horizon T, two resources, windows, and decode granted per-token (so "work" is not a scalar and the serial lower bound under-estimates real occupancy). The deadline-prefix feasibility oracle is an *approximation* of our true feasibility (it ignores window conflicts); using it to reject is safe (never admits illegally — window checks still gate), using it to *accept* is not a guarantee.
- Optimality is for cardinality only; the lex-first refinement is about min-work, which aligns with our 1e-6·V tiebreak but is not proven to optimize V.
- SJF-flavored rules can starve long/deadline-tight requests; EDD is the right *dispatch* order (decode grants) while SPT is the right *admission* order — mixing them per-policy (as the port does) has no single-machine theorem behind it; must be validated empirically on the suite.

**Sources.** arXiv:2607.16347 (v4, Jul 2026), fetched → `research/papers/2607.16347.pdf` / `.txt`. Key results: Lemma 2.1 (p. 4), SlackX/insertion criterion Lemma 2.2 (p. 4), Algorithm 1 (p. 5), Theorem 3.1 (p. 5), deadline tree Theorems 4.2–4.3 (p. 7), lex-first optimality Theorems 5.4–5.5 (pp. 9–10), tier matroids Theorem 6.1 (p. 11). Predecessor: Lin & Wang, insertion rule O(n log n) (analysis only); Zhao & Yuan, DD-schedule implementation.
