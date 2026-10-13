> **Post-contest archive note (Oct 2026):** Contest audit completed 2026-08-26, before the Aug 28 2026 contest close (final rank #56 worldwide, top 1.3%). Preserved as a historical working record. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 02 — Challenging the SA09 floors (re-derived from primary sources)

Method: each bound re-proven or attacked using ONLY problem.md clauses; no local
scores anywhere. Verdicts: HOLDS / LOOSE-BUT-SOUND / OPENS.

## B1 LINK bound (SA09 §1.2a) — HOLDS, one cosmetic looseness

Per-direction serial occupancy inside [first ARR, last token]:
UP bytes = Σ Lin_i·bpt (each request: exactly ONE UP leg queued at P PRE completion,
len=Lin — problem.md:133,163; pieces add NO transfers, :151) + decode rounds.
Decode UP: "one transfer per distinct remote in the group (len = that cloud's member
count)" (:164) ⇒ Σ members over clouds in round k = m_k ⇒ Σ_k m_k = ΣLout tokens'
bpt total across the test regardless of partitioning. DOWN symmetric per :166.
Header latency: ≥1 header per direction per round (a queue may batch, time doesn't);
rounds ≥ n_rounds = max(maxLout, ceil(ΣLout/R)) because member ids are distinct
(:172) and each request needs Lout_i distinct groups ⇒ per-request chains don't
merge. Piggyback primitives: NONE (transfers auto-queued only by task completion,
:204). Prefill legs can't share headers (one per request, individually enqueued).
⇒ E ≥ max(UP_serial, DOWN_serial) stands. Looseness (known, §5.1 SA09): ignores
trigger-latency dead time — works FOR us when attacking futility claims.

## B2 CLOUD bound — HOLDS in partition form

Atomicity: D PROC is one task per cloud per round (:165, :371 busy-computer rule),
a request's cloud fixed at P PRE (:109). Pigeonhole argument certified in SA09 §1.2b;
nothing in the statement lets a request's decode work leave its assigned cloud, and
num pieces apply ONLY to prefill (:160 "Output-step tasks cannot be split").
ATTACK FAILED: could chunks be used to shift DECODE load cross-cloud? No mechanism —
assignment permanent at P PRE (:109, :312 "also fixes the remote computer").

## B3 LOCAL bound — HOLDS

All six steps' PRE/POST tasks run exclusively on the single local computer (:125);
serial. Preallocating the exact count is computable per test. No attack vector found;
note the artifact ALREADY prices local-bound schedules (METER e_min term,
src/Solution.cpp:325-331 region).

## B4 Dependency-chain floor per straggler — HOLDS with honest caveat

Chain ARR_i → … → token k uses ≥ fixed minimum path independent of other requests;
SA09 §flagged contention-blindness (teeter family). Keep treating as loose lower
bound; never cite it against upside pools.

## Where the CHALLENGE genuinely lands (reopened flanks)

F1. The SA09→score conversion assumed *independent* achievability of the tdr/tpot/E
    floors (their own caveat #3). That cuts BOTH ways: for w_tp≈1 starve-class tests
    the binding quantity is E alone; the ~95%-structural verdict for #5/#6 is sound
    ONLY IF the LINK floor is tight there — i.e., if the schedule can truly saturate
    UP/DOWN end-to-end. The m17-family measurement showed UP-bound loose by 6.5×
    on one shape. So for ANY hidden test whose link looks saturated in judgement
    dumps, the true floor = max(B1, B2, B3) NOT B1 alone; where B2/B3 < B1, link
    saturation requires transferring DURING all compute — which pipelining delivers.
    NET: pipelining recovers toward B1 (the certified bottom), not below it — pools
    remain capped by B1, but current-artifact-vs-B1 gaps stay legitimately open
    wherever B1 ≪ achieved-E (m19b: 2.28×). This confirms SB07/waves as the lever;
    no futility reversal.
F2. Lout-hidden exploitation of ΣLout≤2e5 (constraints, :270): per-test remaining-token
    upper bound = 2e5 − observed_tokens − Σ observed-fin Louts; plus maxLout ≤512 cap
    (:269) gives a HARD ladder for survival-conditioned estimates (SB05 synergy).
    Cheap, gated, new information the estimator layer can consume.
F3. Alarm-clock primitives (see 03/P1 + doc 05) change wake-proof assumptions used
    to reject earlier staggered-wave designs (D-140-P0 / Rybko–Stolyar caution
    remains, but the proof obligation is now satisfiable IN PROTOCOL).

---

## PART II — Formal re-derivations (math written out in full)

Notation: `lat` = latency_ms, `β` = 8·bytes_per_token/(bw·10⁶) ms/byte so
transfer(len) = lat + β·len; `lat > 0` strictly (problem.md:213-214); bpt = bytes_per_token.

### II.1 B1 LINK bound — exact byte and header ledger

Byte invariance, decode legs. Round r places member counts (m_{r,1},…,m_{r,K})
with Σ_c m_{r,c} = |group| = m_r (every member sits on some cloud; member ids distinct,
problem.md:172). UP queueing rule (:164-165): ONE transfer per DISTINCT remote in the
group, len = that cloud's member count. Hence

    UP_decode_bytes = Σ_r Σ_c m_{r,c}·bpt = Σ_r m_r·bpt = Σ_i Lout_i·bpt,

i.e. the TOTAL decode UP bytes is a partition-independent constant of the TEST, not
of our batching. Same argument down: DOWN is queued per GROUP after its D PROCs
(:166) with len = m_r ⇒ DOWN_decode_bytes = ΣLout_i·bpt. Prefill adds the fixed
Σ_i Lin_i·bpt in each direction (one UP len=Lin at P PRE completion :163; one DOWN
len=Lin from the LAST piece only :151). Therefore TOTAL directional bytes:

    UP_total  = (Σ Lin_i + Σ Lout_i)·bpt      DOWN_total = same expression.

No grouping choice moves a single byte. Attack tried and failed: piggybacking
several prefills into one transfer — impossible, transfers auto-queue only on task
completion (:204) and a prefill task is single-request (groups are DECODE-only,
:172,:235). Chunking explicitly adds NO transfers between pieces (:151).

Header latency, refined beyond SA09. Every transfer entry pays `lat` inside the
shared FIFO while that direction is busy (serial channel). Entry counts:

- Prefill: exactly R UP entries and R DOWN entries (forced, non-batchable).
- Decode: a request's token k comes from a D POST group containing it; a group has
  ≥1 member ⇒ #groups N_g satisfies N_g ≥ max_i Lout_i =: Λ_max, because each
  request contributes AT MOST one member to any group (distinct ids within a group,
  :172; a request needs Lout_i DISTINCT groups for its Lout_i tokens).
  UP entries ≥ N_g (each member rides its own cloud entry); DOWN entries ≥ N_g
  (one per group :166; adjacent-in-time groups still create separate FIFO entries,
  transfers are auto-queued only by task completion :204).

Directional serial time inside the makespan window E*:

    E* ≥ max( UP_serial , DOWN_serial )
    UP_serial   ≥ (R + N_g)·lat + (ΣLin + ΣLout)·β·bpt
    DOWN_serial ≥ (R + N_g)·lat + (ΣLin + ΣLout)·β·bpt

with the certified minimum at N_g = Λ_max. Verdict: **HOLDS** — this restates
SA09 §1.2a with header counting made exact; only looseness remains trigger dead
time outside transfer occupancy (works FOR us in futility arguments).

### II.2 B2 CLOUD bound — pigeonhole, why every loophole dies

A request's remote computer is FIXED at its P PRE assignment (:109; :312 "also fixes
the remote computer"). Its decode D PROC tasks execute exclusively on that cloud
(:165 same-cloud rule; busy-computer rule :371 forbids overlap). Output-step tasks
CANNOT be split (:160) — chunking is an INPUT-stage privilege only, so decode load
can neither migrate cross-cloud nor be divided.

Per-cloud residency: across all rounds cloud c runs one D PROC per round covering
its members; duration dproc(m) is ≥ m·δ where δ := min_m dproc(m)/m > 0 (clamped
interpolation keeps every legal duration positive, :235). Summing over rounds,
cloud c accumulates ≥ Σ_r m_{r,c}·δ of pure decode processing; since
Σ_c Σ_r m_{r,c} = Σ_i Lout_i, some cloud accumulates ≥ Σ_i Lout_i·δ/K ⇒

    E* ≥ Σ_i Lout_i · δ / K        (pigeonhole under permanent assignment).

Attack log: (a) move decode work via chunks — dead, :160; (b) reassignment — dead,
cloud permanent (:109/:312); (c) mega-group amortization — already counted, it is
exactly what δ captures (the best achievable per-member rate over ALL batch sizes);
(d) idling other clouds while herding — irrelevant to a LOWER bound. **HOLDS.**

### II.3 B3 LOCAL bound — serial six-step tax

Every PRE and POST task runs on THE single local computer (:125; :371 applies to
server "E"). Lower-bound instance count over the test:

- 2R forced: R× P PRE + R× P POST.
- decode: R memberships ⇒ ≥ N_g D PRE tasks locally; ≥ N_g D POST tasks locally.

Each task pays its own setup S (:110/:123 S paid per task, per piece, per group) at
its ACTUAL table duration, all strictly positive (:235). With N_g = Λ_max and the
most generous flat-table assumption beyond m=1, E* ≥ S·(2R+2Λ_max) + reduced task
sums. No attack vector exists: one local machine, non-preemptive, no merging of
PRE/POST across stages. **HOLDS** — and note the coupling: B3 grows with N_g exactly
as B1 does, so the honest resource floor is

    B* = max( B1 , B2 , B3 + chain floors )    evaluated JOINTLY,

never B1 alone; any futility claim must show achieved(p) ≤ B* for every term.

### II.4 B4 straggler dependency chain — statement-exact path length

For request i arriving τ_i, token 1's earliest possible completion, on an otherwise
EMPTY system with best-case batch sizes, is the chain

  ARR →P PRE[+S+p_pre] →UP[+lat+β·Lin·bpt] →P PROC[+S+d_proc] →DOWN[+lat+β·Lin·bpt]
      →P POST[+S+p_post] →D PRE[+S+d_pre(m₁)] →UP[+lat+β·m₁bpt] →D PROC[+S+dproc(m₁)]
      →DOWN[+lat+β·m₁bpt] →D POST[+S+d_post(m₁)] ⇒ token 1;

each further token repeats only the five-step decode cycle. This is contention-blind
(a lower bound only); SA09 flags its teeter-family blindness and we concur — never
cite it against upside pools.

### II.5 Where the challenge lands, quantified

Joint floor B* = max(B1, B2, B3_local, B4_chain). A pool p is closed only when
achieved(p) ≤ B*. Where achieved(p) > B* — e.g. the m19b shape at 2.28× the cloud
floor with 27% local occupancy — the gap realized(p) − B* is legitimately open,
reachable only by schedules saturating MULTIPLE resources concurrently (compute ∥
transfer ∥ local pipeline): precisely wave pipelining (SB07) plus alarm-clock wake
proofs (03/P1, doc 05). Conversely pools already sitting ON B1 (byte-pinned starve
class) stay closed: pipelining shifts work in time, deletes NO bytes. Audit verdict:
contested pool 50–150 rigorous / ≤580 optimistic SURVIVES, now with header-count
arithmetic certified rather than assumed, and the reopened flank is exactly the
realized-vs-B* gap class (confirming SB07/waves as the top remaining lever).
