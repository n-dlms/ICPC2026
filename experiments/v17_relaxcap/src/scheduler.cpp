// Layer 4: decision engine — work-conserving greedy, one task per free resource.
#include "scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <iomanip>
#include <iostream>

namespace icpc2026 {

namespace {

constexpr double XREMOTE_LAT_THRESH = 50.0;  // PART 7: above this latency, bound cross-remote D PRE to policies_.xremote_cap
#ifndef PSGO_STATIC
constexpr long long PSGO_F = 1000;  // PSGO Batch 3 cadence (dev build only)
#endif



}

// PSGO Batch 3: Gradient Tuning Engine. Reads the Live Score Oracle's SLO
// excess vector, computes the penalty scalar P = sqrt(e_tdr^2 + e_tpot^2)
// (identical to the score's dist), and adapts the dynamic max batch size:
//   P > 0  -> dmb = max(1, (int)(psgo_max_m * alpha * (1 - P)))   (shrink: protect SLOs)
//   P == 0 -> dmb = min(psgo_max_m, dmb + 1)                      (grow: throughput is free)
// The growth branch is gated on the oracle having at least one TDR sample so
// the blind startup phase (no completions yet) cannot drift to the ceiling.
#ifndef PSGO_STATIC
void Scheduler::psgo_adjust(State& state) {
    const double e_tdr = state.get_excess_tdr();
    const double e_tpot = state.get_excess_tpot();
    const double P = std::sqrt(e_tdr * e_tdr + e_tpot * e_tpot);
    const double max_m = static_cast<double>(policies_.psgo_max_m);
    int dmb = state.dynamic_max_batch();
    if (P > 0.0) {
        dmb = static_cast<int>(max_m * policies_.alpha * (1.0 - P));
        if (dmb < 1) dmb = 1;
    } else if (state.oracle().count_tdr > 0) {
        // Batch-4 Hard Rule tuning (F update frequency): the +1 growth walk
        // fires only every 5th adjustment. Growing the instant the SLO is met
        // walked dmb past the compliance edge and self-inflicted the violation
        // (Teeter/Ambush gauntlet losses); shrinking stays reactive at F=1000.
        if (frames_ % (PSGO_F * 5) == 0) {
            if (dmb < 1) dmb = 1;  // -1 sentinel (engine wired after init)
            if (dmb < policies_.psgo_max_m) dmb += 1;
        }
    }
    state.set_dynamic_max_batch(dmb);
    std::cerr << std::setprecision(6)
              << "[PSGO frame=" << frames_ << " e_tdr=" << e_tdr
              << " e_tpot=" << e_tpot << " P=" << P
              << " dmb=" << dmb << "/" << policies_.psgo_max_m << "]\n";
}
#endif

 std::vector<Assignment> Scheduler::decide(State& state, double t) {
     std::vector<Assignment> out;
    out.reserve(static_cast<size_t>(state.num_clouds()) + 1);
    // PSGO Batch 3 engine (dev build only; PSGO_STATIC removes it from the
    // submission artifact — D-049 option (a), D-061 source-removal).
#ifndef PSGO_STATIC
    if (++frames_ % PSGO_F == 0) psgo_adjust(state);
#endif

    TaskSpec local_task = pick_local(state, t);
    if (local_task.ids.size() > 0) {
        state.issue(local_task, t);
        out.push_back(Assignment{std::move(local_task)});
    }

    for (int c = 0; c < state.num_clouds(); ++c) {
        TaskSpec cloud_task = pick_cloud(state, c, t);
        if (cloud_task.ids.size() > 0) {
            state.issue(cloud_task, t);
            out.push_back(Assignment{std::move(cloud_task)});
        }
    }
    return out;
}

 TaskSpec Scheduler::pick_local(State& state, double t) {
     (void)t;
     TaskSpec spec;
     if (state.local().busy) return spec;

      int id = -1;
#ifndef PPRE_MAX_INFLIGHT
#define PPRE_MAX_INFLIGHT 2
#endif
       // PART 5.4 / V11_PPRECAP: bound the shared UP-FIFO depth so a saturated
       // link cannot accumulate unbounded in-flight UPs. up_inflight_proto() is a
       // DECISION-DRIVEN projection: +1 per P PRE and +distinct-clouds per D PRE
       // issued (State::issue), minus XDN-UP completions (State::apply_event).
       // It equals the real interactor FIFO backlog (waiting + in flight). Both
       // P PRE and D PRE are gated by it; when saturated the local falls through
       // to D POST / P POST (DOWN link) and otherwise idles, letting the FIFO drain.
       bool up_full = false; (void)up_full;
#ifdef V11_PPRECAP
       up_full = state.up_inflight_proto() >= static_cast<size_t>(PPRE_MAX_INFLIGHT);
#endif
        {
           // Zero-Allocation Hot Path: pop the largest-Lin prefill candidate
           // from the Lin-bucket array (ppre_pop) in amortized O(PPRE_MAXLIN).
           // Replaced the O(log R) heap (V15) and the O(READY_P_PRE) linear scan
           // (FIFO) with a single head-of-largest-bucket pop.
           id = state.ppre_pop();
       }
       if (id >= 0) {
#ifdef V11_PPRECAP
            if (up_full) {
                state.ppre_push(id);  // retry next frame (UP link saturated)
                // fall through: try other local work
            } else {
                const Request& r = state.request(id);
                spec.step = StepKind::P_PRE;
                spec.cloud = policies_.cloud->choose(state, r, t);
                spec.ids = {id};
                return spec;
            }
#else
            const Request& r = state.request(id);
            spec.step = StepKind::P_PRE;
            spec.cloud = policies_.cloud->choose(state, r, t);
            spec.ids = {id};
            return spec;
#endif
       }
       id = state.rl_pop(Phase::READY_P_POST, -1);
    if (id >= 0) {
        const Request& r = state.request(id);
        spec.step = StepKind::P_POST;
        spec.cloud = r.cloud;
        spec.ids = {id};
        return spec;
    }
    // D PRE batches only members of one cloud (D-009): one UP, one D PROC,
    // one DOWN, one D_POST per group; avoids the cross-cloud DOWN ambiguity.
    bool hold = policies_.delay != nullptr && policies_.delay->hold_dpre(state, t);
    if (!hold) {
        // PART 7 adaptive switch: D PRE combine is runtime-controlled via
        // policies_.combine_dpre / policies_.xremote_cap (no V8/V9 compile flags).
        // V11_PPRECAP: a D PRE enqueues one UP per distinct cloud it spans. Bound
        // the projected UP-in-flight to PPRE_MAX_INFLIGHT; defer (and keep the
        // head at the front) when even the head's single UP would overflow.
        // O(1)-amortized: READY_D_PRE lives in per-cloud buckets
        // (State::ready_dpre). No per-frame vector-of-vectors allocation and no
        // global-list scan: the head cloud is the largest bucket (ties to lowest
        // index); its front request is the head, always included (no starvation).
        {
         int hc = -1;
         size_t bestsz = 0;
         for (int c = 0; c < state.num_clouds(); ++c) {
             const int sz = state.rl_size(Phase::READY_D_PRE, c);
             if (static_cast<size_t>(sz) > bestsz) { bestsz = static_cast<size_t>(sz); hc = c; }
         }
          int hid = -1;
          if (hc >= 0) hid = state.rl_head(Phase::READY_D_PRE, hc);
           bool dpre_acc_skip = false;
#ifdef DPRE_ACCUMULATE
           // OD-9-gated D_PRE accumulation (D3, Holy Grail): defer D PRE while D
           // POST work exists AND the READY_D_PRE pool is below `target`, so the
           // local keeps emitting tokens (D POST) while the pool grows. Liveness:
           // skip is gated on dpost_n>0, so when D POST drains the gate opens.
           // target = full max batch IFF (w_c>0 AND flat decode table); else 0
           // (gate off) — isolates the OD-9 win without touching default tables.
           if (hid >= 0) {
               const int dpost_n = state.rl_size(Phase::READY_D_POST, -1);
               const int dpre_n = state.count_phase(Phase::READY_D_PRE);
               // D3 base gate: hold D PRE while D POST backlog exists AND
               // READY_D_PRE pool is below `target`. target = MarginalBatch's
               // asymptotic cap on D PRE.  The flat_decode_table() restriction
               // was D-066's insurance against regressions on default tables;
               // these scratch variants relax or refine that restriction.
               //   (default) flat-only D3 — D-074 baked.
               //   D3_V_LINK   fire when worst_up_ms < 0.1 (cheap-link) AND any;
               //                falls back to flat-only otherwise.
               //   D3_V_LINK10 fire when worst_up_ms < 10.0 AND any; else flat-only.
               //   D3_V_RBOUNDD3 fire on any table, target bounded to min(cap, R/4) —
               //                 prevents "hold forever" on cases where cap=1<<20.
               //   D3_V_PREFILL fire on any table while the prefill pipe is active
               //                (P_PROC_RUNNING or WAIT_INPUT_DOWN) AND flat tables always.
               //   D3_V_IDLEOK  fire on any table when prefill pipe active OR marginal
               //                link slack (worst_up_ms < MarginalBatch.cap tokens of D_PRE
               //                local wave) — keep local busy.
               const int target = [&] {
                   if (!(cfg_.w_c > 0.0)) return 0;
                   const int mc = policies_.batch->group_size(StepKind::D_PRE, 1 << 20, state, t);
#if defined(D3_V_LINK)
                   const double worst_up = transfer_time(cfg_, static_cast<double>(state.num_requests() + 1));
                   return (worst_up < 0.1) ? mc : (state.flat_decode_table() ? mc : 0);
#elif defined(D3_V_LINK10)
                   const double worst_up = transfer_time(cfg_, static_cast<double>(state.num_requests() + 1));
                   return (worst_up < 10.0) ? mc : (state.flat_decode_table() ? mc : 0);
#elif defined(D3_V_RBOUNDD3)
                   const int R = state.num_requests();
                   return std::min(mc, std::max(1, R / 4));
#elif defined(D3_V_PREFILL)
                   const bool prefill_active = state.count_phase(Phase::P_PROC_RUNNING) + state.count_phase(Phase::WAIT_INPUT_DOWN) > 0;
                   return (state.flat_decode_table() || prefill_active) ? mc : 0;
#elif defined(D3_V_LINK200)
                   // Hypothesis: fire D3-any only when the UP link can absorb the
                   // biggest batch without becoming binding (worst_up < 200ms) AND
                   // the local is not the binding resource (small S). t6/t16
                   // (151ms) fire; t5 (301ms) and t22 (S=20) don't.
                   const double worst_up = transfer_time(cfg_, static_cast<double>(state.num_requests() + 1));
                   if (cfg_.S >= 5.0) return state.flat_decode_table() ? mc : 0;
                   return (worst_up < 200.0) ? mc : (state.flat_decode_table() ? mc : 0);
#elif defined(D3_V_REL)
                   // Relative trigger: fire D3-any when the UP transfer for the
                   // target batch is cheaper than the local D_PRE compute for it
                   // (link is NOT the constraint for that batch size).
                   const double up_mc = transfer_time(cfg_, static_cast<double>(mc));
                   const double wave_mc = cfg_.S + state.table().get(step_of(StepKind::D_PRE), 1);
                   if (up_mc < wave_mc) return mc;
                   return state.flat_decode_table() ? mc : 0;
#elif defined(D3_V_IDLEOK)
                   const bool prefill_active = state.count_phase(Phase::P_PROC_RUNNING) + state.count_phase(Phase::WAIT_INPUT_DOWN) > 0;
                   if (prefill_active) return mc;
                   const double worst_up = transfer_time(cfg_, static_cast<double>(state.num_requests() + 1));
                   const double dpre_wave = cfg_.S + state.table().get(step_of(StepKind::D_PRE), 1);
                   return (worst_up < dpre_wave) ? mc : (state.flat_decode_table() ? mc : 0);
#else
                   return state.flat_decode_table() ? mc : 0;
#endif
               }();
               if (target > 0 && dpost_n > 0 && dpre_n < target) dpre_acc_skip = true;
           }
#endif
          if (hid >= 0 && !dpre_acc_skip) {
#ifdef V11_PPRECAP
            if (state.up_inflight_proto() < static_cast<size_t>(PPRE_MAX_INFLIGHT))
#endif
            {
                state.rl_pop(Phase::READY_D_PRE, hc);  // actually remove the head
                spec.step = StepKind::D_PRE;
                spec.cloud = -1;
                spec.ids = {hid};
                // total D PRE-ready = remaining bucket sizes + the head we just popped
                int total = 1;
                for (int c = 0; c < state.num_clouds(); ++c)
                    total += state.rl_size(Phase::READY_D_PRE, c);
                const int m = policies_.batch->group_size(StepKind::D_PRE, total, state, t);
                if (m > 1 && policies_.combine_dpre) {
                    // 3.4: combine across clouds, bounded by distinct-cloud cap (V9):
                    // high-index clouds pay serial UP, so bound the cap when latency
                    // is high; the head's cloud is always included (no starvation).
                    const int cap = (cfg_.latency_ms < XREMOTE_LAT_THRESH)
                                        ? state.num_clouds()
                                        : policies_.xremote_cap;
                    // V11_PPRECAP: cap distinct clouds so the head's UP plus the
                    // carried clouds stays within the remaining UP link budget.
                    int eff_cap = cap;
#ifdef V11_PPRECAP
                    // LEVEL-2 X9 / D-057: the UP gate bounds DISTINCT clouds by
                    // in-flight COUNT, but on cheap-link tests (tr(n_ready) is
                    // microseconds) the FIFO drains instantly and the count cap
                    // only starves giant groups (dur is a function of TOTAL m:
                    // dur(2000)=105ms vs dur(500)=177ms on non-monotone tables
                    // — a 7x per-token penalty). When the worst-case UP time is
                    // negligible the abyss protection is not needed; allow the
                    // full combine. Abyss-scale links keep the strict cap.
                    const double worst_up = transfer_time(
                        cfg_, static_cast<double>(state.num_requests() + 1));
                    const bool cheap_link = worst_up < 0.1;
                    if (!cheap_link) {
                        const int max_clouds =
                            static_cast<int>(PPRE_MAX_INFLIGHT) -
                            static_cast<int>(state.up_inflight_proto());
                        if (max_clouds < eff_cap) eff_cap = max_clouds;
                    }
#endif
                    int clouds_used = 1;  // head's cloud always included
                    for (int c = 0; c < state.num_clouds(); ++c) {
                        if (c == hc) continue;
                        if (state.rl_empty(Phase::READY_D_PRE, c)) continue;
                        if (clouds_used < eff_cap &&
                            static_cast<int>(spec.ids.size()) < m) {
                            int got = 0;
                            while (got < m - static_cast<int>(spec.ids.size()) &&
                                   !state.rl_empty(Phase::READY_D_PRE, c)) {
                                spec.ids.push_back(state.rl_pop(Phase::READY_D_PRE, c));
                                ++got;
                            }
                            ++clouds_used;
                        }
                        // else: requests stay in their bucket (deferred)
                    }
                } else if (m > 1) {
                    // Same-cloud-only batching (no cross-remote combine): exactly 1 UP.
                    int want = m - 1;
                    int got = 0;
                    while (got < want && !state.rl_empty(Phase::READY_D_PRE, hc)) {
                        spec.ids.push_back(state.rl_pop(Phase::READY_D_PRE, hc));
                        ++got;
                    }
                }
                return spec;
            }
#ifdef V11_PPRECAP
            // UP link saturated: defer the head (leave it at the front of hc's bucket).
#endif
         }
        }
    }
    {
         const int n = state.rl_size(Phase::READY_D_POST, -1);
         if (n > 0) {
            const int m = policies_.batch->group_size(StepKind::D_POST, n, state, t);
            spec.step = StepKind::D_POST;
            spec.cloud = -1;
            spec.ids.reserve(static_cast<size_t>(m));
            for (int i = 0; i < m && i < n; ++i)
                spec.ids.push_back(state.rl_pop(Phase::READY_D_POST, -1));
            return spec;
        }
    }
    return spec;
}

TaskSpec Scheduler::pick_cloud(State& state, int c, double t) {
    (void)t;
    TaskSpec spec;
    if (state.cloud(c).busy) return spec;

    int id = state.rl_pop(Phase::READY_P_PROC, c);
    if (id >= 0) {
        const Request& r = state.request(id);
        auto [ls, le] = policies_.chunk->next_piece(state, r, t);
        spec.step = StepKind::P_PROC;
        spec.ls = ls;
        spec.le = le;
        spec.cloud = c;
        spec.ids = {id};
        return spec;
    }
    {
        const int n = state.rl_size(Phase::READY_D_PROC, c);
        if (n > 0) {
            const int m = policies_.batch->group_size(StepKind::D_PROC, n, state, t);
            spec.step = StepKind::D_PROC;
            spec.cloud = c;
            spec.ids.reserve(static_cast<size_t>(m));
            for (int i = 0; i < m && i < n; ++i)
                spec.ids.push_back(state.rl_pop(Phase::READY_D_PROC, c));
            return spec;
        }
    }
    return spec;
}

}