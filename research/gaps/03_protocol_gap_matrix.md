> **Post-contest archive note (Oct 2026):** Contest audit completed 2026-08-26, before the Aug 28 2026 contest close (final rank #56 worldwide, top 1.3%). "[TO-VERIFY]" rows below were pre-contest session markers, preserved as historical working notes. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 03 — Clause-vs-code matrix (edge traps and primitives)

Legend: OPTIMAL = handled + scored optimally per clause; LOSSY = legal but leaves
points; [TO-VERIFY] = code row not yet pinned in that pre-contest session (preserved historical). All problem.md refs are
Lnnn; source refs file:line as of working tree (contains uncommitted P0–P7 landings —
audited against the tree, not the AGENTS header).

| #   | Clause (problem.md)                                                                                                                    | Solver handling                                                           | Verdict          | Notes                                                   |
| --- | -------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------- | ---------------- | ------------------------------------------------------- |
| 01  | FIN beside final D POST TDN (:242,:258); must not appear in that frame's response                                                      | state.cpp:203-212 event-order note; FIN cancels READY_D_PRE via rl_remove | OK               | replay-verified by SB03 Example 1                       |
| 02  | n ∈ [0,K+1]; ≤1 task/resource (:297,:371)                                                                                              | one task per free resource per frame; structural cap K+1                  | OK               | pieces/decodes share the same K+1 resources             |
| 03  | EOF ⇒ exit code 0 immediately (:387)                                                                                                   | FastScanner::eof() protocol.cpp:30; main returns 0 Solution.cpp:347       | [TO-VERIFY deep] | confirm no read path spins on a truncated partial frame |
| 04  | No self-wake timer (:364)                                                                                                              | purely reactive engine                                                    | OK               | but we never EXPLOIT piece-TDNs either — primitive P1   |
| 05  | Pieces alternate with other work (:110,:136)                                                                                           | chunking priced only as prefill-speed lever                               | LOSSY→P1         | decision-point value unpriced                           |
| 06  | FIFO entry: same-D PRE UPs by increasing cloud index; otherwise event order; simultaneous completions use ASSIGNMENT-LINE order (:206) | assignment lines emitted in resource iteration order; no tie-break policy | LOSSY→P2         | free SJF control at every coalesced completion          |

| 07  | dist_base>0 & huge ⇒ wait≡1 ∀ achievable dist (:462-478)          | regime detection keys on weights/SLOs; no direct wait-saturation classification | [TO-VERIFY] Solution.cpp compute_alpha          | classify at t=0 from scalars ⇒ pure-makespan mode (candidate C3)       |
| 08  | SLO1/SLO2→1e9 ⇒ excess≡0, dist≡0 (:459)                           | cliff/V_tpot machinery activity in this regime unknown                          | [TO-VERIFY] dead-weight check on cliff branches | micro unless a flip misfires                                           |
| 09  | tp_UB−tp_base can be 1e-9 (:281-282) ramp collapse                | clamp math exact                                                                | OK                                              | above-base E-reduction worthless; gate future probes on measured width |
| 10  | tp_UB above physical ceiling (starve) ⇒ linear points forever     | hard-driving tp policy                                                          | OK, capped                                      | B1 binds; pipelining closes realized-vs-B1 gap only                    |
| 11  | Lout hidden until FIN (:103); ΣLout≤2e5, Lout≤512 (:269-270)      | EWMA estimators state.hpp:105-112; survival max(μ,iter+1) planned not landed    | LOSSY                                           | FREE hard-bound ladder unused (candidate C4)                           |
| 12  | No end-of-arrivals signal (:400-401)                              | no lull/endgame trigger implemented                                             | LOSSY                                           | P1 alarm clocks make a legal poll-loop possible (C1)                   |
| 13  | R unknown (:97); no group-size limit beyond readiness (:172,:235) | demand-driven sizing                                                            | OK                                              | table clamps handle sizes >R (:228-235)                                |
| 14  | K=1 / num_layers=1 degeneracies disable mechanics (:295)          | single-piece path; std::max(1,cfg.K) guards                                     | [TO-VERIFY]                                     | confirm mega_regime V-gate auto-noops at K=1                           |
| 15  | Group ids distinct; order-insensitive semantics (:172,:321)       | set-based grouping                                                              | OK                                              |                                                                        |
| 16  | D PROC members may come from DIFFERENT D PRE groups (:165)        | cross-bucket combine logic exists                                               | [TO-VERIFY pin]                                 | staged straggler absorption = primitive P3                             |
| 17  | latency>0 strictly (:213-214)                                     | EPS=1e-9 discipline                                                             | OK                                              |                                                                        |
| 18  | Stuck rules: respond-0 safe pre-end; abandonment fatal (:393-401) | reactive loop; holds re-examined per frame                                      | OK                                              | every new hold needs event-backed wake or P1 clock (D-140-P0 lesson)   |
| 19  | Simulate-ahead officially permitted w/ doubles (:290)             | fwsim.* on tree (SB03)                                                          | OK                                              | keep assignments grounded in delivered frame facts (:362)              |

## Primitives this audit adds

**P1 — Piece-TDN alarm clocks.** A request between XDN and its last piece may be
issued additional tiny pieces [ls,ls+δ), ascending gap-free (:138-152), each paying
S+dur ≤ S+pproc(Lin). Each completion emits TDN ⇒ next frame ⇒ decision point at
~t_now+S+dur. Tunable span [pproc/num_layers, pproc]; occupies ONE cloud; ZERO link
bytes; at most num_layers−1 extra pieces per request; ineligible once finished.
Uses: lull/stream-end polling during quiet windows (no-event evidence across chained
clocks), timed admission for wave staggering (makes SB07 wake proofs constructible),
precise park-until-T expiry frames even when nothing else moves.

**P2 — Assignment-line SJF tie-break.** Simultaneously-completing tasks enqueue their
transfers in our line order (:206). Emit shorter queued transfers first → shorter
head-of-line occupancy each tie-break; same tasks, same legalities, deterministic.

**P3 — Cross-D PRE straggler absorption.** A member may join any upcoming PROC of its
cloud regardless of which D PRE fed it (:165). Staged mixes let late stragglers ride
already-planned rounds at zero extra headers beyond their own UP leg.

---

# PART II — Row dossiers (resolution of every [TO-VERIFY], with code pins)

Dossier format: clause → code evidence (file:line, read this session) → verdict →
residual risk. Line refs verified 2026-08-26 against the working tree.

## Row 03 dossier — EOF ⇒ exit 0 (problem.md:387) — VERDICT: OK

Chain of custody: `FastScanner::fill()` returns false on fgets-EOF, sets valid_=0
(protocol.cpp:20-28); `next_token()` returns `{}` when fill fails (protocol.cpp:43-45);
`ProtocolReader::next_frame` returns false when the first token view is empty
(protocol.cpp:126-128); main loop `while (reader.next_frame(...))` exits and falls to
`return 0` (Solution.cpp tail). No token getter spins on EOF — every path terminates
via fill failure. RESIDUAL RISK (recorded, low): a mid-frame disconnect would make
subsequent getters return empty views (next_int→0; parse_double("") unpinned) —
unreachable in practice because a judge death closes our pipe ⇒ clean EOF exit.
No change proposed.

**NEW HARDENING FINDING: asserts are LIVE in the artifact.** Makefile CXXFLAGS carry
no -DNDEBUG (AGENTS §7.1), so protocol.cpp assertions ship in the submission:
`e >= 0`, `server_s[0]=='C'`, `m >= 1` (×2), `ev.dur > 0.0`. None can fire on
statement-conforming streams, but each converts ANY hypothetical judge formatting
quirk into an abort = protocol zero for that test. ALSO: the unknown-event-kind
branch writes "[DEBUG] Unknown event kind" to stderr UNCONDITIONALLY (protocol.cpp,
ARR/TDN/XDN/FIN dispatch else-arm) — not behind #ifdef DEBUG; repeated hits could
fill a 64 KB stderr pipe and BLOCK the solver (the exact TLE mechanism documented
for LOCAL_DIAG leakage). Hardening candidates C6a/C6b registered in 04_candidates.

## Rows 07/08 dossier — objective collapse regimes — VERDICT: RESOLVED

Code pins (Solution.cpp compute_alpha): the old D9 scalar early-out is RETIRED
("P7 … computed per frame by the SB08 slack controller (fwsim.cpp cliff_refresh)
on the realized cohort, with a doom valve"); the D-058 serialized-monster
worst-case check remains documented at the same site.

cliff_refresh guard (fwsim.cpp:130-133): controller engages ONLY under
`w_c>0 ∧ dist_base==0 ∧ SLO1>0`; otherwise inert.

Row 08 (SLO=1e9 family): with dist_base==0 the controller DOES engage, but slack
hC = SLO1 − mean_tdr stays hugely positive ⇒ SAFE latched permanently ⇒ SAFE
contract (prefill priority, no holds) coincides with the pure-makespan optimum;
excess ≡ 0 forever means wait ≡ 1 regardless of schedule — machinery costs only
O(1)/frame and ZERO scoring distortion. RESIDUAL [single open pin]:
tdr_urgency_refresh runs unconditionally near the cliff_refresh call site;
scheduler.cpp:160 shows tdr_urgent requires `mv_tdr>0 && a_i>1e-9`; whether the
oracle marginal can be positive while excess_tdr ≡ 0 was not pinned this session —
if marginals vanish outside excess regions (SB04 says yes), row 08 fully closes.

Row 07 (dist_base ≥ achievable-dist family): handled implicitly — huge dist_base
makes clamp(1−dist/dist_base)≈1 even under distortion; SB08's doom-valve/latch
prices wait-protection at ≈0 automatically. The α mapping still spends w_c-weighted
protection that is dead weight here ⇒ candidate C3 stands as REAL but its EV
downgrades: expected gain is mostly byte savings + tail-risk removal, not score,
because the live machinery already behaves pure-makespan in these regimes.

## Row 14 dossier — K=1 / num_layers=1 degeneracies — VERDICT: MOOT + REGISTRY ALERT

The D-161 gate function `mega_regime()` is GONE from the working tree: state.hpp:117
reads "P3 retired: D-161 mega_regime() duty … subsumed by SB06 exact-split DP", and
grep finds NO occurrence of mega_regime / mega_cache_ / sb06 / exact_split in
src/*.cpp|hpp (only the orphaned minifier rename q7g in tools/merge_submission.py
and historical text). Also scheduler.cpp shows NO accumulate / DPRE_ACCUMULATE hits
— the A1b pure-tp accumulate target described in D-161 is likewise absent/renamed.

(a) K=1-auto-noop concern for mega gating is moot on this tree (no mega path exists).
(b) REGISTRY ALERT AF-1 for the human: docs/DECISIONS.md and DECISIONS D-161
describe state.cpp mega_regime + policies.cpp A1/A3 + scheduler.cpp A1b as the
current tree's x9 pack; the tree has moved past that description — an earlier
"P0–P7 landing series" replaced the D8 traffic-aware flip with the SB08 slack
controller (Solution.cpp comments say so explicitly). Any CF submission queue MUST
re-baseline against what is ACTUALLY compiled today; the Tier-0 probe will attribute
mechanisms no current DECISIONS entry fully describes. Decision ownership: human.
(c) If the x9 win (+335.561 local, ban on citing local scores notwithstanding its
CF relevance) is to remain CF-probed, its CURRENT carrier must be identified from
git diff of the P-series landings, not from memory or the AGENTS header.

## Row 16 dossier — cross-D PRE membership freedom — VERDICT: PARTIAL

Pins: `policies.combine_dpre = (alpha > 0.2)` and
`xremote_cap = 1 + int(alpha*(cfg.K−1))` set per-frame from α (Solution.cpp:176-179)
— the dynamic cross-cloud cap ladder (D-144 lineage) IS the combine path.
Arbitration confirmed at scheduler.cpp:160-178: run_ppost with cloud_starved /
link_busy predicates, and the V11_PPRECAP `!up_full` gate wraps the P_PRE pop.
EXISTS today: mixed-cloud D PRE groups up to xremote_cap, staged across frames.
DOES NOT EXIST: deliberate booking of stragglers into ALREADY-PLANNED upcoming PROC
rounds of their cloud independent of their own D PRE cohort (full primitive P3).
Cost of full P3: moderate (membership bookkeeping + fwsim lookahead); EV small-to-
moderate on ragged-tail shapes; KEEP PARKED unless wave pipelining lands first,
since waves create exactly the round structure P3 would exploit.

## Row 12 supplement — behavior during total silence

During a frame-less interval NOTHING executes (no timer). After the last queued task
completes and its frame is answered "0", the solver sleeps until the next external
event. That silent window is precisely what C1's clocks monetize and why row 12 was
scored LOSSY. All Part I table rows stand as written.
