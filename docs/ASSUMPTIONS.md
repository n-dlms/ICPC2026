# ASSUMPTIONS.md — Spec Facts, Assumptions, Unknowns (contest archive that led to #56)

ICPC 2026 Online Challenge 1 powered by Huawei — Problem A "Edge–Cloud Collaborative Scheduling"

Status: 2026-08-14 — FULL STATEMENT received by me (Ntokozo Dlamini) (verbatim). All prior UNKNOWNs were resolved. Contest ended Aug 28 2026, final rank #56.

Status legend: CONFIRMED (statement text) / ASSUMED (inferred) / UNKNOWN

---

## 0. Contest facts (CONFIRMED — statement + CF API)

- Contest 2251, one problem A. 14 days (Aug 14 11:00 UTC → Aug 28 10:59 UTC). Interactive, deterministic, non-adaptive per test.
- **Aggregation**: 22 preliminary tests (feedback only, no ranking); finals = 20 frozen tests; final score = **arithmetic mean of the 20 final scores** (3-decimal display). No hacks.
- Per-test score in [0, 1000]. Protocol error / malformed output / stuck state / TLE / MLE → 0 on that test only. **No partial credit for an unfinished test.**

## 1. Startup configuration (CONFIRMED)

- Line 1: `K S latency_in_ms bandwidth_gbps bytes_per_token num_layers` — K, `bytes_per_token`, `num_layers` ints; rest reals (ms, tokens/ms).
- Line 2: `SLO1 SLO2 tp_UB tp_base dist_base w_tp w_c` — reals.

```text
w_tp + w_c = 1, both ≥ 0. SLO1 > 0, SLO2 > 0, tp_UB > tp_base ≥ 0, dist_base ≥ 0.
```

## 2. Task-time table (CONFIRMED)

- N rows: `batch_size prefill_pre prefill_proc prefill_post decode_pre decode_proc decode_post`.
- batch_size ∈ [1,4096], distinct; missing = -1; unordered. Every step column has ≥ 1 non-missing.
- get_duration(step, batch_size): sort by batch_size; exact → row; between → linear interp; below min → clamp; above max → clamp; skip -1 rows in that column. Result strictly positive. `2 ≤ N ≤ 4096`.
- Durations exclude S (task busy = S + dur, same as TDN's dur field).

## 3. Architecture (CONFIRMED)

- 1 local computer + K identical clouds (K ≤ 8). Shared link: UP and DOWN are independent one-at-a-time FIFOs, both active simultaneously.
- Transfers automatic; never commanded. **We only predict** (for planning) or observe (XDN).

```text
Transfer time = latency_in_ms + 8·len·bytes_per_token/(bandwidth_gbps·10^6)
```

- Input len = `Lin`; output UP len = per-remote member count; output DOWN len = group size.
- Same D PRE's per-remote UPs enter in **increasing remote index**; otherwise in interactor event order (task completion order; equal completion → assignment-line order).

## 4. Request lifecycle (CONFIRMED)

```
ARR → P PRE (local; fixes cloud) → [input UP] → P PROC piece(s) [0,num_layers)
→ [input DOWN, only after last piece] → P POST (local) → ready (TDR stops)
→ D PRE (local, any cloud mix) → [UP per remote] → D PROC (same cloud)
→ [DOWN per group] → D POST (local) → token → repeat Lout[i] times → FIN (beside final D POST TDN)
```

- Cloud assignment permanent. Prefill never grouped. R unknown; Lout[i] unknown until FIN (we only count iterations).
- **Transfers enter the FIFO at task-issue time with enter = predicted completion (issue + S + dur), not when the TDN arrives.**

  FIFO order therefore follows issue order; two tasks issued A-then-B can complete B-then-A, and their transfers queue in issue order.

  Debug oracle mirrors this (D-004); any transfer-aware policy must too.
- Request states strictly sequential; **iteration index never transmitted** — our bookkeeping is the sole source of truth.

## 5. Chunking rules (CONFIRMED)

- Pieces [ls,le), nonempty ints; first ls=0, later ls=prev le, last le=num_layers.

```text
Duration = (le−ls)/num_layers · prefill_proc(Lin)
```

- First piece waits input UP XDN; later pieces wait previous piece TDN. **Only the last piece queues input DOWN (len=Lin). No inter-piece transfers. Exactly one UP + one DOWN per request total, regardless of chunk count.**
- Empty/out-of-range/gap/out-of-order → violation (0 on test).
- num_layers ≤ 64 → at most 64 pieces.

## 6. Batching rules (CONFIRMED)

- m ≥ 1, distinct ids, every member exactly at this step and not in flight or finished.
- D PRE / D POST: any cloud mix (D PRE queues one UP per distinct remote, increasing index). D PROC: all members same cloud. No max group size (bounded by ready requests; a listed table size > R does not make it schedulable).
- Id order in a group is semantically irrelevant; preserved on echo (TDN/XDN list ids in our order).

## 7. Protocol / I/O (CONFIRMED)

- Frames: `t` (9 decimals) then `e` then e event lines. Read whole frame, respond once, flush. All assignments start at t.

  Printed t nondecreasing; internal times strictly increasing between frames; same-timestamp events coalesced; line order carries no priority.
- Events: `ARR <rid> <Lin>` / `TDN <server> <task_spec> <dur>` (server = E or Ck; spec echoed canonical; dur 9 decimals) / `XDN <UP|DOWN> <remote> <size> <PRE|DEC> <m> <rid...>` / `FIN <rid>`.
- End: `END` line in place of next frame header → exit. EOF → exit(0) immediately, no crash.
- **Assignment format (exact):** response = count n (0 ≤ n ≤ K+1) then n assignments, each `<server> <task_spec>`:

  - `E P PRE <remote> <rid>`
  - `C<k> P PROC <ls> <le> <remote> <rid>`  (ls le BEFORE remote! remote must equal assigned cloud)
  - `E P POST <remote> <rid>`  (remote is a consistency check)
  - `E D PRE -1 <m> <rid...>`
  - `C<k> D PROC <remote> <m> <rid...>`  (all members assigned to that remote)
  - `E D POST -1 <m> <rid...>`
- Arbitrary whitespace accepted between tokens (still print one per line). At most one task per resource per response.
- No self-wake timer; act only on frames. Responding 0 is safe while arrivals remain; stuck state (unfinished + no possible future event) = 0.
- Every frame is a scheduling opportunity; TDN in a frame frees the resource at t, usable in the same response.
- Constraints: 1≤K≤8, 1≤R≤2000, 1≤Lin≤4096, 1≤Lout≤512, ΣLout ≤ 2·10^5, 1≤num_layers≤64, 2≤N≤4096, 1≤S≤10, latency∈[0.001,50], bw∈[0.001,100], bpt∈[1,10^6], table entries∈[0.001,10^4], arrivals∈[0,10^9], completions ≤ 10^12 bound, **≤ 2·10^6 frames per test** (fast I/O mandatory).

## 8. Scoring (CONFIRMED — full formula; verified on Example 1)

```text
tp = ΣLout / (latest final-token completion − earliest ARR)
tp_comp = clamp(tp; tp_base, tp_UB) = max(0, min(1, (tp−tp_base)/(tp_UB−tp_base)))
```

- `tp` — throughput; `tp_comp` — clamped throughput component.

```text
μ_tdr = Σ(ppost_i − arr_i)/R
```

- `tdr` — mean over requests of (input stage final step completion − arrival).

```text
μ_tpot = Σ_i(e_last,i − e_first,i)/(ΣLout − R)
```

- `tpot` — mean of pooled consecutive gaps (0 if no gaps exist).

```text
ε_tdr = max(0, (tdr−SLO1)/SLO1); ε_tpot = max(0, (tpot−SLO2)/SLO2)
dist = √(ε_tdr² + ε_tpot²)
```

- `dist` — distance from SLO targets using `SLO1`, `SLO2`.

```text
wait_comp = clamp(dist; dist_base, 0) = max(0, min(1, 1 − dist/dist_base))
```

- if `dist_base` = 0: 1 iff `dist` = 0 else 0.

```text
Score = 1000 · (w_tp·tp_comp + w_c·wait_comp)
```

- per test.

- Example 1 verification: tp = 1/45 = tp_base → tp_comp 0; tdr=30=SLO1, tpot=0 (Lout=1) → dist=0, dist_base=0 → wait_comp 1; score = 0.5·1000 = 500. ✓ (Hand-traced transcript: 0→4→10→21→27→30→32→35→40→43→45, transfers 6/6/3/3 ms — all match.)
- Every request must be completed even if a weight is 0.

## 9. Corrections to OPTIMIZATION_DOCTRINE.md (flag only; my file — Ntokozo Dlamini)

- §1.3 chunking formula: "c·up_transfer_time" is WRONG. One UP total per request (chunks wait on previous piece TDN; only the last queues DOWN). Chunk cost = (c−1)·S + pipeline-fill latency; benefit = cloud/local overlap. Transfer time is paid once per direction per request.
- §3.3 assignment sketch garbled: real formats are §7 above (P PROC order: ls le remote rid).
- Doctrinal "reject/abandon" ideas (deliberately never finishing a request) are impossible: unfinished test = 0.

## 10. Design constraints (CONFIRMED)

- C++17, -O2, -Wall -Wextra -Wpedantic -Werror, single-threaded, stdlib only. Fast I/O; flush after every response. Exit 0 on EOF. EPS=1e-9 for time comparisons.

  Makefile tracks header deps (-MMD): header edits must not produce stale objects (BUG-002).

- ≤ 2·10^6 frames ⇒ O(events) amortized state updates + O(ready) decisions; no per-frame O(R) scans.

  All hot paths incremental (D-006): ready lists are std::deque with O(1) pops; end_frame() scans only requests finished since the last frame.

  Measured: 1.01M frames = 3.1s CPU + 1.8s protocol-mandated flush floor ≈ 4.9s wall-ish floor at statement max; 4 MB RSS.
