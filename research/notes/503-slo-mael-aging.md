> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 503 — SLO-MAEL / PSLO-MAEL: SLO-Aware Inference Scheduling on Heterogeneous Processors (KAIST)

- Status: **unavailable** — paper not on arXiv under that title. Original is a KAIST SoC group paper (IEEE/ACM), no open preprint located. Distilled from the inline description in `src/opencode.md` (lines 60-64, 109-121). Do NOT use pirate mirrors. Reopen this note when a verified open PDF is located (record DOI).
- Source description: KAIST, "SLO-Aware Inference Scheduler for Heterogeneous Processors"; key algorithms MAEL, SLO-MAEL, PSLO-MAEL.

## Claim

Introduces a family of priority policies for inference serving on heterogeneous processors: MAEL (Minimum Available Execution Latency), SLO-MAEL (MAEL keyed on SLO tightness), PSLO-MAEL (preemptive SLO-MAEL with aging). Reported: PSLO-MAEL achieves ~2.84× speedup over affinity-oriented scheduling while suppressing SLO violations.

## Relevance (to the ICPC 2026 Huawei challenge)

Three direct transfers:

1. **SLO tightness as the priority key.** `SLO_i − expected_latency_i` is exactly the SLAI (Last-Schedulable-Time / criticality) key the frozen solver already uses; SLO-MAEL provides the original theoretical justification (see `research/notes/403-slai-last-schedulable-time.md`). This paper is the *origin* citation, not a competing algorithm.

2. **The aging mechanism prevents starvation.** Long prefill/decode jobs with low SLO tightness can beblocked indefinitely by a stream of short tight-SLO arrivals. Aging accumulates a violation_degree on each bypassed request andAdds age_weight·age to its priority — pushing starved jobs back into contention in O(1) per request. This is a closure default the frozen greedy should grow when the spec's decode-saturated adversarial cells (`tools/case_gen.py --force-adversarial decode_saturated`) appear.

3. **PSLO-MAEL → preemptive edge prefill.** The spec is **assumed** (UNKNOWN — see `protocol/protocol.yaml`) to allow or disallow preemption of in-flight prefill. If preemption is permitted (preemption across the cloud→edge decode boundary is unlikely; across edge-prefill slots it may be), PSLO-MAEL is the policy to port.

## Algorithm sketch

SLO-MAEL priority (per opencode lines 109-121):

```
struct Request {
    double base_priority;        // = 1.0 / max(1, SLO_i - expected_latency_i)
    double age;                  // +=1 each tick the request is not dispatched
    double violation_degree;     // how-much-over-SLO if dispatched right now
};
total_priority = base_priority + age_weight * age + violation_degree
schedule request with MAX total_priority (max-tightness, max-aging)
fallback: when no SLO violations are projected among ready candidates, fall back to plain MAEL (min available exec latency)

```

PSLO-MAEL adds: on each new admit decision, if the running request has lower total_priority than a ready one, preempt (suspend + re-queue) the running one. Aging is what stops the preempted request from being preempted indefinitely — each preemption ticks its age.

## Port plan

Engine 1 (Greedy) variant in `src/Solution.cpp`. Add a per-request `age` field on `Req` (int8, ticks since arrival); in the existing `wait_sorted` admit comparator (`cmp_admit_key`), add `age_weight * age` to the existing key when `IMBAL_GATE` permits the SLAI/SJF keys. The weight is a single compile-time `-DAGE_W=<float>`. A/B test against the frozen winner with `tools/ab_test.py` on:

- `cases/adversarial/decode_saturated/*.` — should gain (the whole point of aging is starvation prevention on decode-saturated graphs).
- `cases/dev/burst_*` — neutral or micro-gain (no prolonged starvation ⇒ aging never binds).
- `cases/zoo/identical_*` — neutral (all equal ⇒ aging is a tiebreak that mirrors existing stable-sort order).
- `cases/adversarial/gap_*` — MUST NOT regress (this is the class that broke the previous SLAI/SJF key when the gate was missing). Aging must be IMBAL-gated identically to the other keys.

Acceptance requires: no regression on `cases/*` 20-case freeze gate + no class regression on the adversarial gap set. Open `rejected-ideas.md` and confirm "aging" is not already in the reject list before implementing (the reject-list grep is the gate per docs/DECISIONS.md).

## Weaknesses

- Paper is for inference on heterogeneous *processors inside one machine* — the scheduler chooses which processor type runs each request. Our U-shape has fixed edge/cloud assignment per stage; there is no processor-type choice, only stage-ordering and admit decisions. The aging mechanism transfers; the affinity/speedup numbers do not.
- Aging is a **preference**, not a feasibility check — aged requests still need to pass `impossible_slo` / `doomed` admission control. Aging alone does not make an infeasible request feasible. The frozen solver's `doom` formula (the EXP-42 Finding-B naive `durations()` sum — see `experiments-log.md`) is the actual feasibility gate; aging is layered on top, not a replacement.
- The reported 2.84× is for preemptive scheduling; non-preemptive aging alone is much weaker (paper reports it as modest), and preemption is gated on an UNKNOWN spec feature.
- Float arithmetic in the priority sum risks determinism violations across build flags — use integer ratio keys like the existing `usa1_key`/`sjf_key` parse-time-computed keys (see `src/Solution_asort_frozen.cpp` for the static-key pattern).

## Sources

- `src/opencode.md` lines 60-64 (algorithm description), 109-121 (C++ sketch), 153-161 (vibe-coder kill-list mapping for SLO-MAEL).
- NOTE placeholder — verify the original venue and DOI on the KAIST SoC publications page when accessed; record `unavailable` in `research/papers/manifest.json` so `tools/fetch_papers.py` does not retry.
