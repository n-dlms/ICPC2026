> **Post-contest archive note (Oct 2026):** Contest audit completed 2026-08-26, before the Aug 28 2026 contest close (final rank #56 worldwide, top 1.3%). Preserved as a historical working record. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# GAP AUDIT PACK — primary-source hunt for missing points (2026-08-26)

Mission: exhaustive edge-case / clause-exploit audit against the OFFICIAL statement,
banned-evidence mode (no local judge scores anywhere in this directory).
Authoritative inputs actually used:

- `problem.md` (full text read this session; scrape timestamp inside file: server time
  Aug/22/2026 14:18:29 UTC — every line reference below is problem.md:Lnnn).

- Live statement https://codeforces.com/contest/2251/problem/A fetched 2026-08-26
  ~18:53–18:55 CF server time (4 HTML fetches + 2 r.jina.ai mirrors).

- Layered sources src/*.cpp|hpp (NOT submission.cpp — generated), greps cited file:line.
- research/scoring/{SYNTHESIS.md,SA09_ceiling_analysis.md} — CHALLENGED, not trusted.

## Files

| File                      | Content                                                              | Status                           |
| ------------------------- | -------------------------------------------------------------------- | -------------------------------- |
| 01_statement_live_diff.md | banner forensics, verified-identical ledger, amendment-risk register | COMPLETE                         |
| 02_floor_challenge.md     | re-derivation of SA09 bounds; surviving/open flanks                  | COMPLETE                         |
| 03_protocol_gap_matrix.md | clause-vs-code matrix; primitives; zero-rule traps                   | COMPLETE (some rows [TO-VERIFY]) |
| 04_candidates.md          | ranked one-mechanism-per-submission list, byte math, kill criteria   | COMPLETE                         |

## One-page verdict

NEW EXPLOITABLE STRUCTURE FOUND (statement-proven):

1. **Piece-TDN alarm clocks** — no self-wake exists (problem.md:364), but every extra
   input-stage piece generates a TDN event ⇒ a legal, priceable way to manufacture
   decision points at chosen times, enabling *time*-triggered policies (endgame/lull
   trigger, wave stagger) that the doctrine previously had to reject as wake-proof-less.
   Evidence chain: problem.md:364 (no timer), :136/:110 (pieces alternate; splitting
   allowed), :147 (piece waits only on prev piece TDN), :376–378 (legality only about
   ranges). Cost model: extra cloud task = S+dur piece; duration tunable down to
   pproc(Lin)/num_layers ≥ 0.001 ms; sleeps use ONE cloud + zero link bytes.

2. **Assignment-line tie-break controls the transfer FIFO** — problem.md:206: when
   several tasks complete together and enqueue transfers "in interactor event order;
   tasks started together and finishing together use assignment-line order." We emit
   assignment lines in arbitrary order today ⇒ free SJF-shortest-transfer-first control
   of link FIFO ordering at every tie-break; reduces head-of-line blocking cost
   δ = (long_len−short_len) headers-beyond. Pure win candidate, cheap bytes.

CONFIRMED HARD CELLS (no reopening):

- Batch transfer volume invariant (member-count bytes regardless of grouping);
  decode volume = ΣLout·bpt per direction — SA09 §1.1 re-derived correct.

- Latency-header count lower bound survives every attack tried (per-request prefill
  legs individually queued; per-round ≥1 header/direction; no piggyback primitive exists).

- tp-side ceilings stand where LINK/CLOUD floors bind; pipelining shifts work, not bytes.

BIGGEST MISSED-OBJECTIVE CLASS IDENTIFIED ([TO-VERIFY] in code next session):

- Objective collapses computable from the scalar line at t=0: dist_base ≥ achievable
  dist ⇒ wait ≡ 1 (pure-tp even with w_c > 0); SLO1/SLO2 = 1e9 ⇒ dist ≡ 0 ⇒ same;
  tp_UB(tp_base+1e-9..) ramp collapse ⇒ pure makespan minimization. Matrix rows 07–09.

---

# PART 2 ADDENDUM (2026-08-26 audit close-out)

Resolution status of every [TO-VERIFY] (dossiers with code pins: 03 Part II):

- Row 03 EOF ⇒ exit 0: chain fully pinned protocol.cpp:20-28/:43-45/:126-128 →
  Solution.cpp main-loop exit; VERDICT OK.

- Rows 07/08 objective collapses: D9 scalar flip RETIRED; SB08 slack controller
  (fwsim.cpp cliff_refresh, guard `w_c>0 ∧ dist_base==0 ∧ SLO1>0`, fwsim.cpp:130-133)
  now computes per frame with doom valve. In SLO=1e9 / huge-dist_base regimes the
  controller latches SAFE permanently ⇒ behavior already pure-makespan ⇒ these cells
  RESOLVED as distortion-free. One residual pin recorded (tdr_urgency_refresh vs
  excess≡0). C3 downgraded accordingly (EV = byte savings + tail-risk removal).

- Row 14 degeneracies: mega_regime() DELETED from tree (state.hpp:117 comment);
  x9 pack carriers must be re-identified via git diff before any CF probe.

- Row 16 cross-D PRE freedom: combine path EXISTS (combine_dpre/xremote_cap,
  Solution.cpp:176-179); full P3 booking absent, kept parked pending waves.

NEW AUDIT FINDINGS:

- AF-1 REGISTRY ALERT: AGENTS/DECISIONS narrative describes mechanisms no longer on
  the tree (mega_regime, D8 flip retired; P0–P7/SB08 series present); Tier-0 probe
  attribution MUST be re-baselined against actual compiled content. Human decision.

- AF-2 Artifact robustness: protocol.cpp asserts LIVE (no -DNDEBUG in Makefile flags)
  - unconditional stderr "[DEBUG] Unknown event kind" write — pipe-fill TLE vector;
  hardening candidates C6a/C6b (04_candidates addendum).

- NEW PRIMITIVE P1 → candidate C1: piece-TDN alarm clocks. FULL DESIGN SPEC:
  research/gaps/05_alarm_clock_spec.md (legality proof :136-152/:147/:110/:297-321,
  feasibility limits F1–F3 incl. num_layers==1 unavailability, wake-proof WP1–WP5,
  cost/budget η≈0.15 self-disabling predicate, trigger policy, pseudocode, failure
  census FM1–FM6, byte budget ~370 B fundable, one-submission probe protocol).

Verified-unchanged confirmations closing old suspicions:

- FIN-in-final-TDN-frame engineered state.cpp:203-212 ✓ (row 10).
- FIN latency semantics P7-exact fwsim.cpp:263-284 ✓; Example 1 byte-identical ✓.
- Byte invariant & header floor re-certified with exact arithmetic 02 §II.1–II.5;
  honest contested pool UNCHANGED (50–150 rigorous / ≤580 optimistic), top remaining
  lever confirmed = realized-vs-B* gap ⇒ waves/P1 class.
