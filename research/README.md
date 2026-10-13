# Research archive

This directory records the papers, experiments, and mathematical analyses that
informed the solver.

The solver's ideas, architecture, and decisions are Ntokozo Dlamini's. AI
helped gather and summarize sources, relay instructions, implement selected
changes, and check results. Labels such as "agent analysis" or "subagent" in
historical notes mean assistant drafting under his direction (prepared by
Ntokozo Dlamini with assistant support); they do not transfer ownership of
the solution ideas. Archive note (post-contest): the SA01–SA10 / SB01–SB10
programs were completed on 2026-08-23; the contest ended Aug 28 2026 with
final rank #56 worldwide.

The workflow used during development was (archived; program completed 2026-08-23):

1. **Scope** — define a research question and identify relevant papers (see [past winners](../docs/06-past-winners.md) and the [strategy playbook](../docs/05-strategy-playbook.md)).
2. **Fetch** — `python3 tools/fetch_papers.py <arxiv-id>...` (or `--file ids.txt`) downloads PDFs into `research/papers/` and records status in `research/papers/manifest.json`. Respects arXiv rate limits (`--sleep`).
3. **Extract** — convert each PDF with `pdftotext <paper>.pdf <paper>.txt` (Poppler) and review the text.
4. **Distill** — one Markdown note per paper under `research/notes/`, named `NNN-<short-title>.md`. Required sections:
   - **Claim** (1-3 sentences: what it proves/builds)
   - **Relevance** (why it matters for this challenge)
   - **Algorithm sketch** (pseudocode or precise math)
   - **Port plan** (the proposed solver variant and its A/B test)
   - **Weaknesses** (what does not transfer to our interactive judge / SLO model)
   - **Sources** (arXiv id / URL, page numbers for key results)
5. **Apply** — incorporate useful findings into the [strategy playbook](../docs/05-strategy-playbook.md) and evaluate solver variants with `tools/ab_test.py`. Results are recorded in the [experiment log](notes/experiments-log.md).

## Conventions

- Keep one topic per note. Notes preserve the evidence behind algorithm candidates.
- Never modify a fetched PDF. Never commit anything unless asked.
- If a paper is paywalled (ACM/IEEE), record the DOI in the note and mark `unavailable` — do not use sci-hub or pirate sources. Prefer arXiv/OpenReview/public PDFs.
- Research contributors first document findings; implementation follows an explicit decision.
- Statement citations in the SA/SB docs refer to the official problem statement
  (formerly mirrored locally at `problem.md`; the official statement is linked
  from the root README). Line numbers match the archived scrape.

## Inventory

| Path                    | Contents                                                                                                                                                                                                                                                                                             |
| ----------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `research/papers/`      | 71 archived papers (PDF + extracted text + `manifest.json`) and [USAGE.md](papers/USAGE.md) — the paper → code usage map.                                                                                                                                                                            |
| `research/notes/`       | Distilled reading notes (one per paper), the experiments log, and rejected-ideas registry.                                                                                                                                                                                                           |
| `research/scoring/`     | **SA01–SA10** — scoring-physics program: authoritative formula, official artifacts, throughput intervals, waiting component, aggregation losses, admission value, control restatement, literature, ceiling analysis, structural audit. [SYNTHESIS.md](scoring/SYNTHESIS.md)                          |
| `research/techniques/`  | **SB01–SB10** — computation modules: online scheduling theory, forward-model rollout, exact forward simulator, score calculus, inference layer, partition math, pipelining, endgame/cliff controller, artifact engineering, integration blueprint. [PROGRAM_STATUS.md](techniques/PROGRAM_STATUS.md) |
| `research/day0/…day10/` | Pre-contest preparation: theory notes, judge-system research, catalogs, work plans.                                                                                                                                                                                                                  |
| `research/gaps/`        | Statement live-diff, floor challenge, protocol gap matrix, candidate audits, alarm-clock spec, implementation & probe plan.                                                                                                                                                                          |
| `experiments/`          | Program harnesses and result tables (scorecard, ceilings, traces, fuzz evidence).                                                                                                                                                                                                                    |

## Status

Both programs were completed on 2026-08-23. The contest ended Aug 28 2026
with final rank #56 worldwide (preliminary leaderboard 16,147.866 / 22,000).
The SB program produced two post-contest
integrations on the working tree (P4 exact score calculus, P7 binary-cliff
controller); the sealed leader artifact remains the D-148 tree. See
`research/techniques/PROGRAM_STATUS.md` for per-module status.
