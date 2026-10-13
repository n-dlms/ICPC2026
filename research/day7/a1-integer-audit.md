> **Post-contest archive note (Oct 2026):** Pre-contest preparation completed. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). Spec-bound "to verify on Aug 14" items below were reconciled against the official statement at contest start and are preserved as historical working notes. Anything still open is a Post-contest follow-up (not part of the #56 run). Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# A1 — Integer max-value audit (2026-08-06)

Per Day-7 philosophy: integer safety is a first-class correctness property. This audit enumerates every arithmetic site in `src/Solution_asort_frozen.cpp` where overflow / truncation / UB could occur under adversarial spec bounds, and conditioned each finding on the spec bounds confirmed at contest start (Aug 14). The `limits.yaml` placeholders at the time (N≤2000, T≤1e6) kept the solver safe; the audit covered *the untested part of the contract space*.

## Field-type inventory

- `struct Req` (lines 9-19): `int A, Lin, Lout, D, phase, remain, tokens_left, start, completion; bool doomed; double usa1_key; long long sjf_key`.
- Top-level params (line 21): `int T, E, C, COMM, B, PEDGE, PCLOUD, PSCALE; double ALPHA; int Ecap, Ccap`.
- `vector<int> edge_occ, cloud_occ` (line 25) — sized `T + 5000` (line 1166-1167) — `int` size argument converted to `size_t`.

## Findings (spec-bound-conditioned)

### F1. `prio_afterlife` (line 178) — LLONG_MAX risk at extreme Lout

```cpp
return (2LL * r.Lin + r.Lout + 1) * (long long)r.Lout;

```

The LHS `2LL * r.Lin + r.Lout + 1` is `long long` (good), but the final product `(LL) * (long long)r.Lout` ≈ `(2Lout + Lout + 1) * Lout ≈ 2Lout²` plus `Lout²` → roughly `3Lout²`. At `Lout = 2.1e9` (≈INT_MAX) → `3 * (2.1e9)² ≈ 1.3e19 > LLONG_MAX (9.22e18)` → **signed overflow → UB**.

- **Safe iff** `Lout ≤ ~1.75e9` (solving 3x² ≤ 9.22e18). SPEC-DEPENDENT.
- **Fix on Aug 14 if spec allows Lout > 1.75e9**: cap `prio_afterlife` to a saturated value, or use `__int128` for the multiply.

### F2. `int` field types (lines 10, 21, 1186) — read UB at INT_MAX

`cin >> r.A >> r.Lin >> r.Lout >> r.D` and `cin >> T >> E >> C >> COMM >> ...` — all `int`. If the spec gives any of these > 2.147e9 (INT_MAX), `cin >> (int&)` extraction fails silently (sets failbit) → uninitialized field or skipped parse → garbage scheduling. The input-loop fix (EXP-29) catches total EOF but NOT per-field overflow into a too-narrow type.

- **Safe iff** spec declares every `int` parameter ≤ 2.147e9. SPEC-DEPENDENT.
- **Fix on Aug 14 if any bound > INT_MAX**: widen the affected field(s) to `long long`. Cheap, localized.

### F3. `int` inner-sum truncations before `long long` cast

Multiple sites compute `(int + int + int + int)` then cast to `long long` for downstream use. The inner `int` sum overflows UB if the pairwise sum > INT_MAX, even though the result *would* fit in `long long`.

| Line               | Expression                                           | Pattern                             | Overflow condition |
| ------------------ | ---------------------------------------------------- | ----------------------------------- | ------------------ |
| 276, 378, 764, 831 | `per = (long long)(PEDGE + COMM + PCLOUD + COMM)`    | `int + int + int + int` before cast | sum > 2.147e9      |
| 509                | `(long long)r.Lout * (PEDGE + PCLOUD)`               | int inner sum                       | sum > 2.147e9      |
| 1188               | `(double)(r.Lin + r.Lout)` inside `usa1_key`         | int inner sum before double cast    | sum > 2.147e9      |
| 1190               | `(long long)r.Lout * (PEDGE + COMM + PCLOUD + COMM)` | int inner sum on RHS                | sum > 2.147e9      |

Compile-time realistic case: `PEDGE, PCLOUD, COMM` are tick counts (likely ≤ a few thousand); `Lin, Lout` are request sizes (likely ≤ a few thousand). Sums are tiny, all safe. SPEC-DEPENDENT.

- **Fix on Aug 14 if any spec bound crosses 1e9**: lift the inner sums earlier into `long long`, e.g. `(long long)PEDGE + COMM + PCLOUD + COMM`.

### F4. `(int)llround(ALPHA * (double)Wp)` truncation (line 79)

```cpp
int pe = (int)llround(ALPHA * (double)Wp);
if (pe < 0) pe = 0;

```

`Wp = (long long)r.Lin * PSCALE` (correct LL chain). `ALPHA * Wp` is `double`. `llround` returns `long long`; cast to `int`. If `ALPHA * Wp > 2.147e9`, the `(int)` cast is implementation-defined (effectively truncation per C++ standard → silent corruption, not UB but unhelpful). Same for `pc = (int)max(0LL, Wp - pe)` at line 81.

- **Safe iff** `ALPHA * Lin * PSCALE ≤ 2.147e9`. With ALPHA ≤ 1, PSCALE ≤ 1e3, safe up to Lin ≈ 2.1e6. SPEC-DEPENDENT.
- **Fix on Aug 14 if breached**: change `prefill_durations` to return `pair<long long, long long>` and ripple through callers (TICK-bound downstream uses are all `long long`-cast already).

### F5. `edge_occ.assign(T + 5000, 0)` — int sum + size_t conversion (line 1166)

`(T + 5000)`: `int + int = int`. If T near INT_MAX, sum overflows; the resulting `int` (negatives wrap to UB-on-take-address style) is then converted to `size_t` (a large positive garbage number) → `vector.assign(size_t)` tries to allocate garbage bytes → `std::bad_alloc` → RE/MLE.

- **Safe iff** `T ≤ INT_MAX - 5000` (~2.147e9). SPEC-DEPENDENT; almost certainly true.
- **Fix on Aug 14 if T bound is suspicious**: `edge_occ.assign((long long)T + 5000, 0)` — though `T` itself `int` so the cast just shifts the boundary.

### F6. `score_state` vsum (line 628) — long long accumulator, int operands

`vsum += lat; lat = completion - A; max_lat = max(max_lat, lat); max_c = max(max_c, r.completion); if (n > 0) vsum += max_c + max_lat`. Left operand `vsum` is `long long`; `lat, max_c, max_lat, max_c + max_latency` are all `int` but promoted to `long long` when added. **Bound**: `vsum ≈ N × T + 2T`; at N=2000, T=1e6 → ~2e9; at N=2e5, T=1e9 → ~2e14 — well within `long long`. The `lat` `int` itself is bounded by T; `int lat` is fine as long as T ≤ 2.147e9. SPEC-DEPENDENT only on T's ceiling.

No WAL-level findings (the `long long` math chains are wide enough). Only `int` local expressions are exposed.

### F7. `for (int t = 1; t <= T; ++t)` (line 1180) — int loop bound

If `T == INT_MAX`, the loop iteration `t = INT_MAX` succeeds (matches `t <= T`), but `++t` then overflows `int` → UB. Practically only a hazard if T is exactly INT_MAX.

- **Safe iff** `T < INT_MAX`. SPEC-DEPENDENT.
- **Fix on Aug 14 if T = INT_MAX exactly**: use `for (long long t = 1; ...)` and propagate.

### F8. `t_completion` assignment (`r.completion = t` and similar)

`r.completion` is `int`. If `T > INT_MAX`, assigning `t` (= T) overflows. SPEC-DEPENDENT; same fix as F7.

## Summary verdict

**No findings are real with the realistic spec regime** (small params, T ≤ 1e6, Lin/Lout ≤ 1e3). The audit found **8 latent UB paths awaiting a spec with extreme parameter bounds**:

| Field/Expression                                      | Widen only if spec bound exceeds |
| ----------------------------------------------------- | -------------------------------- |
| `int Lin/Lout/D` (F2)                                 | 2.147e9 (INT_MAX)                |
| `int T/E/C/COMM/B/PEDGE/PCLOUD/PSCALE` (F2)           | 2.147e9                          |
| `prio_afterlife` multiply (F1)                        | Lout > 1.75e9                    |
| Inner int sums `per`, `PEDGE+PCLOUD`, `Lin+Lout` (F3) | pairwise sum > 2.147e9           |
| `pe = (int)llround(Wp * ALPHA)` (F4)                  | Lin × PSCALE × ALPHA > 2.147e9   |
| `edge_occ.assign(T + 5000, ...)` (F5)                 | T > 2.146e9                      |
| `int t` for-loop (F7)                                 | T == INT_MAX (boundary)          |

**Aug 14 spec-parse action item** (linked from D1): record the spec's parameter bounds in a small table; for any bound that crosses the threshold above, apply the localized fix (e.g., widen `Lin/Lout` to `long long`, lift `per` inner sum to `long long`, saturate `prio_afterlife`). This file is the punch list.

## What the audit does NOT cover

- UB outside the integer domain: aliasing, alignment, OOB array access — covered by ASan + UBsan + ASSERT_STATE (running 300/300 clean on the suite).
- Floating-point reproducibility (`pow` at line 1188) — the spec's scoring uses judge-computed float score; cross-platform differences in `pow` are presumably not compared back to the solver's emitted key (the key affects *ordering* only). Flagged for spec-day verification: does the judge use OUR usa1_key value, or only OUR action sequence?

Footer: do NOT change solver types until the spec confirms any bound actually breaches the threshold above. Fix-free = behavior-identical.

## STATUS (2026-08-13, EXP-42): punch list now has a TESTED ARTIFACT

`src/Solution_frozen_wide.cpp` (→ `build/solution_frozen_wide`, source sha `b73fd2ec…`, binary sha `17e7beea2ecc…`) is a byte-derived widening of the certified frozen source implementing F1-F8 + an Engine-0 parse guard (SPEC-BOUNDS marker, param caps 1e7 → graceful admit-zero rc=0 instead of bad_alloc on billion-scale T; verified T=1e9 → rc=0 in 0 bytes, no allocation).
Identity-proven vs the certified binary on every local pool (suite 20, dev 201, adversarial 41, zoo 15, matrix 280 — all delta +0.000000), crash_sweep 2000/2000, ASan/UBSan+ASSERT 500/500, determinism 3/3, 15/15 flag combos `-Werror` clean. Build: `make wide` / `make wide_san`.

**Promotion rule (Aug 14):** read the real spec's bound table; for ANY bound crossing an audit threshold → submit `build/solution_frozen_wide` (first re-verify the SPEC-BOUNDS guard ≥ every spec max, raising the 1e7 cap if the spec exceeds it). Otherwise the certified frozen binary stands. No deadline surgery required in either case — if boundaries are crossed, the substitution is a compiled, identity-proven artifact, not a rushed edit.
