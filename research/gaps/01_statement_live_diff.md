> **Post-contest archive note (Oct 2026):** Contest audit completed 2026-08-26, before the Aug 28 2026 contest close (final rank #56 worldwide, top 1.3%). Preserved as a historical working record. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 01 — Statement live-diff forensics (2026-08-26)

## The banner

Live page (fetched 2026-08-26, CF server time Aug/26/2026 18:53–18:55) still renders:
> "The problem statement has recently been changed. View the changes."

The `View the changes` target is behind the authenticated session (anonymous HTML
carries only the banner; the diff dialog body is not in the anonymous page — confirmed
on 3 fetches + 2 r.jina.ai mirrors). SA02 reached the same wall on Aug/22 and inferred
the edit predates Aug/22. **New fact this session: the banner is still present FOUR
DAYS LATER** while our every-visible-section comparison matches the Aug/22 scrape.
Working hypotheses:
(H1) The changed text predates Aug/22 (SA02's reading) and CF simply never clears the
banner for an interactive contest problem; scoreboard behavior since then shows no
mass re-judging.
(H2) A silent wording amendment occurred ≤Aug/26 with no rejudge trigger. Under H2,
areas most likely to be amended (largest ambiguity density) are listed below.

## Verified-identical ledger (live vs problem.md scrape)

Verified byte-equivalent modulo LaTeX rendering, on ≥4 independent fetches:
intro paras, glossary table, "What You Control", System Model bullets (incl. R not
announced, Lout hidden until FIN), Challenge Format ("Test 1 is the public worked
Example 1, tests 2–22 are hidden preliminary tests", "finals use a separate frozen
set", "no hacks are used"), both Examples incl. all timestamps, tail notes ("output
task's time depends only on its group size…", server copyright block).
NOT LIVE-RE-VERIFIED (tool truncation): Input Stage splitting rules, Output Steps
grouping paragraph, Communication FIFO-ordering sentence set, Event Frames details,
Scoring formulas, Constraints block. Mitigation: these were re-read locally from
problem.md this session (cited throughout gap pack); the local judge implementation
matches them on Example-1 replay (SB03 byte-identical replay, recorded previously).

## Amendment-risk register (strategy-changing clauses, ranked)

For each clause: what a plausible amendment would do to us.
R1. Scoring line parameter ORDER (`SLO1 SLO2 tp_UB tp_base dist_base w_tp w_c`,
    problem.md:189) — a reorder silently flips our regime detection from frame one;
    every test zeroes ONLY via wrong-output? No—parse still succeeds, but policy runs
    on swapped semantics ⇒ massive score loss, not zeros. Defense: none possible if
    silently reordered; watch per-test rows for a uniform inexplicable collapse = first
    sign. LOW likelihood (would break everyone), HIGH damage.
R2. TDR endpoint definition ("P POST completion", problem.md:137/330/443) — any change
    to "first output step can begin" phrasing moves ~all waiting math. Guarded by
    example transcript (TDR=30 shown for Example 1 ⇒ self-checks on every submit via
    test #1 visibility… actually test 1 score visible: sanity anchor stays).
R3. Group-size caps ("no maximum exists or is announced", :173/:235) — if a cap were
    introduced, mega-round strategies break legality ⇒ protocol zero per-test. Our
    responses would be REJECTED → zeros. Watch first test after any drift... tests are

## amendment-risk appendix (2026-08-26, part 2)

### Fetch-tooling limit, recorded verbatim

Live page fetch path (fetch_web_content + r.jina.ai mirror) reliably reproduces the
page head (through the Challenge Format intro) and the page tail (Examples' last
timestamps onward) but mechanically elides middle sections on every attempt made
today (6 attempts, 3 URL shapes). Therefore the ledger above marks those sections
NOT-LIVE-VERIFIED rather than claiming identity. No drift was observed anywhere the
tool did render. Re-verification recipe for the next session: fetch with
`https://r.jina.ai/https://codeforces.com/contest/2251/problem/A` AND the plain URL,
diff the rendered head/tail against the anchor strings below; treat ANY anchor change
as a stop-the-world panic (Statement Changed Protocol):

1. Anchor head: "One local computer and K remote computers".
2. Anchor mid-1: "up to 10 milliseconds".
3. Anchor mid-2: "assignment line has one of the following formats".
4. Anchor mid-3: "You should simulate ahead".
5. Anchor tail: "The contest is over…"` / copyright block.
On any mismatch: snapshot, diff against problem.md, freeze submissions until the
change triage against this register is done.

### Register amplifications

**R1 (parameter order) — detection playbook.** The scoring line arrives once at
startup (problem.md:189). If its ORDER were amended, values would swap silently and
every downstream regime decision (alpha mapping, cliff guard, urgency gates) would
act on wrong semantics. Since test #1 is visible per-prelim-round, a wrong-order
world shows as a broad multi-test collapse unrelated to any submitted diff. Response
plan if ever detected: submit a probe whose alpha mapping is invariant to plausible
reorderings (e.g., classify by SET of weights/SLOs rather than position) — cheap
hardening candidate C5 in 04_candidates.

**R2 (TDR endpoint) — why R2 is well-anchored.** Example 1 transcript pins the TDR
semantics numerically (tdr = 30 in the official trace, reproduced exactly by
statement-following simulators). Any endpoint redefinition would contradict the
published example unless the example was rewritten too — the example transcripts'
timestamps are among the verified-identical regions. Residual risk therefore LOW;
damage HIGH if changed (all waiting-side math).

**R3 (group-size caps) — damage model refined.** Our solver emits unbounded decode
group sizes whenever caps allow. Under a hypothetical cap C introduced silently,
responses with m>C would presumably be rejected → stuck-zero cascade. Detection is
fast (zeros on tests previously scoring high), rollback trivial (cap emitter),
so exposure is bounded to at most one prelim cycle. Note asymmetry: introducing a cap
would ALSO break the reference pace for everyone, mitigating rank damage.

**R4 (piece rules) — coupled to two mechanisms at once.** The piece-TDN alarm clock
(P1/C1) and standard adaptive chunking BOTH stand on :136-152 (alternation, ascending
gap-free [ls,le), only-last-piece-DOWN). An amendment tightening piece counts or
removing mid-request pieces would disable chunk-based pacing strategies broadly.
Monitor via behavior of test cases with S≈0 (chunk-heavy regimes show first).

**R5 (tie-break sentence) — the exploitable clause.** "tasks started together and
finishing together use assignment-line order" (:206) is the single most unusual
sentence in the statement — most problems never specify tie-breaks. Two readings we
owe vigilance to: (a) it applies ONLY to transfers ENQUEUED at that completion burst;
(b) possibly also to other FIFO insertions. Current implementation exploits only (a)
via G2/C2 ordering. A loosened reading (b) would ADD exploit surface; a removed/
clarified-away reading kills C2. The sentence's persistence across the verified
regions is confirmed; mark MEDIUM conservation confidence.

**R7 (bounds asymmetry) — legal-region inventory.** tp_base ∈ [0,1e9] UNBOUNDED ABOVE
tp_UB is prevented only by ":282 tp_UB>tp_base"; dist_base ∈ [0,1e9]; SLOs positive.
Feasible degeneracies our classifier must tolerate: tp_UB = tp_base + 1e-9 (row 09,
ramp collapse), dist_base = 1e9 with dist ≪ 1e9 (wait ≡ 1 — row 07), SLO = 1e9 with
dist_base = 0 (wait ≡ 1 by binary rule — row 08 family), w_tp = 0 xor w_c = 0. All
are classified in matrix rows 07–09 with their resolver status.

### Standing verification cadence (adopted)

Every session that touches anything submission-related: rerun the two-block fetch +
anchor check above (< 2 minutes), eyeball R1–R7 deltas, then proceed. Record
"statement anchors verified YYYY-MM-DD HH:MM CF-server-time" in the session's
DECISIONS entry.
    scored continuously during prelims so any CF-side change would show immediately
    as fresh zeros — none observed through today's scoreboard reading of record.
R4. Piece rules (:138–152) — amendment to "only the last piece queues DOWN" or piece
    count limits breaks chunking legality AND the alarm-clock primitive (§03 P1).
R5. Transfer ordering rule (:206 assignment-line tie-break) — micro-exploit G2 rests
    here; also underpins D_PRE multi-cloud UP determinism.
R6. Frame/coalescing + "9 decimal places" formats — parser-stability items.
R7. Constraint bounds (Constraints, :264–295). Note the odd asymmetry kept as-is:
    `0≤tp_base,dist_base≤1e9` but `1e-9≤tp_UB≤1e9` and `tp_UB>tp_base` (:282) ⇒
    near-degenerate ramps legal (matrix row 09).
Every future session should re-run the two-block fetch and eyeball this register.
