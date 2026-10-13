> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 206 — Preemptive multiprocessor scheduling with rejection (Seiden 2001) — [SECOND-HAND]

> Unavailable note: Elsevier (Theoretical Computer Science), paywalled; no legitimate free copy located. Summary is second-hand, from zbMATH/MaRDI review data and the summary in Németh & Imreh (Acta Cybernetica 2009, open access). Claim-level details marked §S.

**Claim.** Re-analyzes online multiprocessor scheduling with rejection (Bartal et al., note 201) with **preemption allowed** for accepted jobs. Result: a generalized Reject-Total-Penalty algorithm is (4+√10)/3 ≈ **2.387-competitive** for all m, with lower bounds: ≥ 2.12457 for any online algorithm, and ≥ 2.33246 for *oblivious* algorithms (those whose accepted jobs are scheduled without knowledge of rejection penalties) §S.
The acceptance/rejection logic of RTP is preserved; the scheduling subroutine is a new *optimal* online preemptive scheduling algorithm without rejection, which never exceeds the makespan of Chen–van Vliet–Woeginger's previous best and beats it on some inputs §S.

**Relevance.** Two transferable messages:

1. *Preemption buys little in the worst case.* Moving from non-preemptive (1+φ ≈ 2.618, note 201) to preemptive only shaves the competitive ratio to ≈ 2.387, and the *rejection logic is unchanged* (same threshold structure). So if the real spec allows token-level preemption of decode (very plausible — we already grant tokens one at a time), the admission/rejection policy should stay the same; don't expect preemption to rescue a bad admission decision. This supports our "never force / admission control beats forcing" doctrine quantitatively §S.
2. *Oblivious vs informed scheduling matters:* separating "who gets admitted" from "how admitted work is scheduled" loses a factor (2.33 vs 2.12 bound). I.e., admission and dispatch should share information — in our code, both `grant_decodes` and `admit_prefills` share the same occupancy arrays and deadline keys, which is the right structure (admission *can* know the current schedule state).

**Algorithm sketch (§S).** Identical to RTP (note 201) at the rejection layer: reject j ∈ B (penalty ≤ load); otherwise reject iff W + w_j ≤ α·p_j with tuned α; accepted jobs scheduled preemptively by the new optimal online algorithm (a preemptive extension of list scheduling with an improved load bound).

**Port plan.** No new code; evidence for two existing design choices: (a) keep `doomed`-style admission thresholds rather than adding a preemptive repair layer; (b) if a later spec version grants multi-token batches, re-derive the rejection threshold on the batch-granularity work, not per-token. If preemption is NOT in the final spec, note 201's constants are the right ones (1+φ/φ for m=2).

**Weaknesses (transfer gaps).**

- §S model has no release times, deadlines, or per-request SLOs (makespan + penalties only); competitive ratios are worst-case adversarial and say nothing about our random/bursty suite's realized scores.
- Preemption here is *free and full*; our decode grants are per-token but each grant books fixed windows (PEDGE/PCLOUD + COMM) — a granted token cannot be withdrawn, so our "preemption" is really "skip the next token", weaker than the model's.
- Two-site structure (edge+cloud alternating) again absent; m=2 results are for identical interchangeable machines.

**Sources.** S. S. Seiden, *Theoretical Computer Science* 262(1–2):437–458, 2001, DOI 10.1016/S0304-3975(00)00288-7 — NOT fetched (paywalled). Secondary: MaRDI/zbMATH record (portal.mardi4nfdi.de, Item Q5958131) for exact constants; T. Németh & C. Imreh, "Parameter learning online algorithm for multiprocessor scheduling with rejection", *Acta Cybernetica* 19(1) 2009, DOI 10.14232/actacyb.19.1.2009.8 (open access, restates Seiden's results).
