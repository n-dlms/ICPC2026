// =============================================================================
// scheduler.cpp — Layer 4 implementation: pick_local / pick_cloud arbitration.
//
// pick_local (Theorem B, D-140/D-146/D-147): when the local computer is free,
// first arbitrate P_POST vs P_PRE with a TDR-urgency override (finishing an
// input stage both frees the pipeline and stops TDR); then admit a new P_PRE
// through the V11 UP-FIFO gate (K-scaled in-flight depth: protects the shared
// link in blackhole regimes, opens up under SLO headroom); then form a D_PRE
// group — home cloud first, cross-cloud members only while the dynamic-cap
// ladder allows (UP cost priced against decode value, link-saturation
// override), with the DPRE_ACCUMULATE gate keeping rounds unified when a
// larger batch is imminent; finally D_POST groups drain completed tokens.
//
// pick_cloud: SPT-on-Lin prefill first (Smith's rule minimizes mean flow time
// — the TDR engine), falling back to per-cloud D_PROC batches sized by the
// Layer-5 batch policy. Past 1.2 M frames the O(R) SPT scan is replaced by a
// FIFO head: the measured ~10.5 µs/frame worst case would exceed the 15 s
// TLE at the statement's 2·10^6-frame cap, and a frozen zero costs −50 final
// mean points — ordering-level loss always beats a timeout.
// =============================================================================
#include "scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <iomanip>
#include <iostream>

namespace icpc2026 {

 namespace {

#ifndef PSGO_STATIC
constexpr long long PSGO_F = 1000;  // PSGO Batch 3 cadence (dev build only)
#endif

#ifdef LOCAL_DIAG
// Instrument 3 accumulators (scheduler.cpp-internal; dumped at program end).
extern int g_hist_dpre_n, g_hist_dpre_sum;
extern int g_hist_dproc_n, g_hist_dproc_sum;
extern int g_hist_dpost_n, g_hist_dpost_sum;
int g_hist_dpre_n = 0, g_hist_dpre_sum = 0;
int g_hist_dproc_n = 0, g_hist_dproc_sum = 0;
int g_hist_dpost_n = 0, g_hist_dpost_sum = 0;
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
#ifdef LOCAL_DIAG
    // Instruments 1+6: rolling bottleneck detector + projected score gap.
    // Utilization = busy time / elapsed since first arrival; the resource
    // nearest 1.0 in steady state is the throughput ceiling of this test.
    {
        static double last_bn_t = -1e18;
        if (t - last_bn_t > 100.0) {
            last_bn_t = t;
            const double first_arr = state.oracle().earliest_arrival;
            const double elapsed = t - first_arr;
            if (elapsed > 0.0) {
                const Config& cg = state.cfg();
                const double K = static_cast<double>(state.num_clouds());
                fprintf(stderr,
                        "[BN] t=%.1f E=%.3f C=%.3f UP=%.3f DN=%.3f | ql=%d qn=%d qk=%d qo=%d qs=%d qr=%d\n",
                        t,
                        state.e_busy() / elapsed,
                        state.c_busy() / (elapsed * K),
                        state.up_busy() / elapsed,
                        state.down_busy() / elapsed,
                        state.count_phase(Phase::READY_P_PRE),
                        state.count_phase(Phase::READY_P_PROC),
                        state.count_phase(Phase::READY_D_PRE),
                        state.count_phase(Phase::READY_D_PROC),
                        state.count_phase(Phase::READY_D_POST),
                        state.count_phase(Phase::READY_P_POST));
                fprintf(stderr,
                        "[SCORE] t=%.1f norm_tp=%.4f norm_c=%.4f proj=%.1f | "
                        "tp=%.6g tdr=%.3f tpot=%.3f\n",
                        t,
                        state.oracle().normalized_tp(cg.tp_base, cg.tp_UB),
                        state.oracle().projected_cw_exact(cg.SLO1, cg.SLO2, cg.dist_base),
                        state.oracle().projected_score(cg.w_tp, cg.w_c, cg.tp_base,
                                                       cg.tp_UB, cg.SLO1, cg.SLO2, cg.dist_base),
                        state.oracle().projected_tp(),
                        state.oracle().projected_tdr(),
                        state.oracle().projected_tpot());
            }
        }
    }
#endif
    // PSGO Batch 3 engine (dev build only; PSGO_STATIC removes it from the
    // submission artifact — D-049 option (a), D-061 source-removal).
#ifndef PSGO_STATIC
    if (++frames_ % PSGO_F == 0) psgo_adjust(state);
#endif

    TaskSpec local_task = pick_local(state, t);
#ifdef LOCAL_DIAG
    // Instrument 2: decision trace — what E had available vs what was chosen.
    if (!state.local().busy) {
        if (local_task.ids.size() > 0) {
            const char* names[] = {"P_PRE", "P_PROC", "P_POST", "D_PRE", "D_PROC", "D_POST"};
            fprintf(stderr,
                    "[DEC] t=%.3f chose=%s(m=%d,rid0=%d) | avail: dpost=%d dpre_pool=%d ppre=%d ppost=%d | E_busy=%d\n",
                    t, names[static_cast<int>(local_task.step)],
                    static_cast<int>(local_task.ids.size()), local_task.ids[0],
                    state.count_phase(Phase::READY_D_POST),
                    state.count_phase(Phase::READY_D_PRE),
                    state.count_phase(Phase::READY_P_PRE),
                    state.count_phase(Phase::READY_P_POST),
                    state.local().busy ? 1 : 0);
        } else {
            fprintf(stderr, "[DEC] t=%.3f E_IDLE (no legal local task)\n", t);
        }
    }
#endif
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
#ifdef LOCAL_DIAG
    // Instrument 3: issued group-size histogram (what we ACHIEVED, not the
    // policy target). Dumped once at program end via diag_dump_hist().
    for (const Assignment& a : out) {
        const int m = static_cast<int>(a.spec.ids.size());
        switch (a.spec.step) {
            case StepKind::D_PRE: ++g_hist_dpre_n; g_hist_dpre_sum += m; break;
            case StepKind::D_PROC: ++g_hist_dproc_n; g_hist_dproc_sum += m; break;
            case StepKind::D_POST: ++g_hist_dpost_n; g_hist_dpost_sum += m; break;
            default: break;
        }
    }
#endif
    return out;
}

#ifdef LOCAL_DIAG
// Instrument 3 end-of-run dump (called from Solution.cpp main loop exit).
void diag_dump_hist() {
    fprintf(stderr,
            "[HIST] D_PRE: n=%d mean=%.2f | D_PROC: n=%d mean=%.2f | "
            "D_POST: n=%d mean=%.2f\n",
            g_hist_dpre_n,
            g_hist_dpre_n ? static_cast<double>(g_hist_dpre_sum) / g_hist_dpre_n : 0.0,
            g_hist_dproc_n,
            g_hist_dproc_n ? static_cast<double>(g_hist_dproc_sum) / g_hist_dproc_n : 0.0,
            g_hist_dpost_n,
            g_hist_dpost_n ? static_cast<double>(g_hist_dpost_sum) / g_hist_dpost_n : 0.0);
}
#endif

 TaskSpec Scheduler::pick_local(State& state, double t) {
     (void)t;
     TaskSpec spec;
     if (state.local().busy) return spec;
     int id = -1;
#ifndef PPRE_MAX_INFLIGHT
#define PPRE_MAX_INFLIGHT 2
#endif
#ifdef V11_PPRECAP
      const double worst_up_lin = transfer_time(cfg_, static_cast<double>(State::PPRE_MAXLIN));
      // CRITICAL FIX: Base inflight MUST scale with K to prevent cloud starvation.
      // Capping at 2 starves the clouds when K > 2, destroying norm_tp.
      const int base_inflight = std::max(2, state.num_clouds());
      const int max_inflight = (worst_up_lin < 1.0) ? 100 : base_inflight;
      const bool dist_headroom = (cfg_.dist_base > 0.0) &&
          (state.oracle().excess_dist(cfg_.SLO1, cfg_.SLO2) < 0.5 * cfg_.dist_base);
      int max_inflight_eff = max_inflight;
      if (!dist_headroom && cfg_.w_c > 0.0) {
          // We are on the SLO slope or cliff. We must protect TDR from UP queue
          // inflation, but we CANNOT drop below base_inflight (K) or the clouds
          // will starve and norm_tp will collapse. Throttle slightly, keep full.
          max_inflight_eff = std::max(base_inflight, max_inflight / 2 + 1);
      } else {
          // We have SLO headroom. Open the floodgates to maximize throughput.
          max_inflight_eff = std::max(max_inflight, 2 + 2 * state.num_clouds());
      }
      const bool up_full = state.up_inflight_pre() >= static_cast<size_t>(max_inflight_eff);
#else
      const bool up_full = false; (void)up_full;
#endif
     {
          const int ppre_id = state.rl_head(Phase::READY_P_PRE, -1);
          const int ppost_id = state.rl_head(Phase::READY_P_POST, -1);
          const bool tdr_urgent = (cfg_.dist_base > 0.0 && cfg_.w_c > 0.0 &&
              state.oracle().excess_tdr(cfg_.SLO1) > 0.3 * cfg_.dist_base);
          bool run_ppost = false;
          if (ppost_id >= 0) {
              if (ppre_id < 0 || tdr_urgent) {
                  run_ppost = true;
              } else {
                  const Request& r_pre = state.request(ppre_id);
                  const Request& r_post = state.request(ppost_id);
                  const double a_i = table_.get(Step::PREFILL_PRE, r_pre.lin);
                  const double q_j = table_.get(Step::PREFILL_POST, r_post.lin);
                  const CurrentTask& cc = state.cloud(r_post.cloud);
                  const bool cloud_starved = cc.busy && (cc.busy_until > t + q_j);
                  const bool link_busy = state.up().tail > t + 1.0;
                  if (q_j < a_i && (!cloud_starved || link_busy)) run_ppost = true;
              }
          }
if (!run_ppost) {
#ifdef V11_PPRECAP
             if (!up_full)
#endif
             {
                 id = state.ppre_pop();
                 if (id >= 0) {
                     const Request& r = state.request(id);
                     spec.step = StepKind::P_PRE;
                     spec.cloud = policies_.cloud->choose(state, r, t);
                     spec.ids = {id};
                     return spec;
                 }
             }
        }
         if (ppost_id >= 0) {
             id = state.rl_pop(Phase::READY_P_POST, -1);
             const Request& r = state.request(id);
             spec.step = StepKind::P_POST;
             spec.cloud = r.cloud;
             spec.ids = {id};
             return spec;
         }
     }
     // 2b/World-A (measured): on pure-throughput tests the per-token E cost of
     // a decode round is f(m) = (2S + dpre(m) + dpost(m))/m. When the live
     // table is materially sublinear (f(m*) <= f(1)/2), accumulating the pool
     // to m* (capped by R) cuts E_decode proportionally; on flat or
     // superlinear tables the argmin check below is inactive by construction.
     // Gate: w_c < 0.1 — with waiting weight the decode delay would cost
     // dist; with pure tp it is free (makespan is E-bound on these tests).
     int mstar_target = -1;
     if (cfg_.w_c < 0.1) {
         const int m_star = table_.decode_m_star(cfg_.S);
         const int mcap = std::min(m_star, std::max(1, state.num_requests()));
         const double f_star = (2.0 * cfg_.S + table_.get(Step::DECODE_PRE, mcap)
                                + table_.get(Step::DECODE_POST, mcap)) / static_cast<double>(mcap);
         const double f_ref = (2.0 * cfg_.S + table_.get(Step::DECODE_PRE, 1)
                               + table_.get(Step::DECODE_POST, 1));
         if (2.0 * f_star < f_ref) mstar_target = mcap;
     }
     bool hold = policies_.delay != nullptr && policies_.delay->hold_dpre(state, t);
     // 2c: weight-gated prefill priority. On w_tp == 0 tests throughput is
     // unscored: TDR ends at P POST, so decode timing cannot touch the TDR
     // term, and TPOT is minimized by maximal groups (f(m) non-increasing).
     // Therefore while ANY prefill-side work (P PRE / P POST) is pending, E
     // must serve it exclusively — decode waits until the prefill pipeline
     // drains. Makespan is unscored, so the deferred decode phase is free.
     const bool prefill_priority = (cfg_.w_tp == 0.0 && cfg_.w_c > 0.0) &&
         (state.rl_size(Phase::READY_P_PRE, -1) > 0 ||
          state.rl_size(Phase::READY_P_POST, -1) > 0);
     if (!hold && !prefill_priority) {
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
              if (hid >= 0) {
                  const int dpost_n = state.rl_size(Phase::READY_D_POST, -1);
                  const int dpre_n = state.count_phase(Phase::READY_D_PRE);
                  int target = (cfg_.w_c > 0.0)
                                     ? policies_.batch->group_size(StepKind::D_PRE, 1 << 20, state, t)
                                     : 0;
                  // World-A: accumulate toward the table's m* on pure-tp tests
                  // (the w_c > 0 gate above zeroes the MarginalBatch target
                  // there, which is exactly the D-161 A1b gap).
                  if (mstar_target > target) target = mstar_target;
                  if (target > 0 && dpre_n < target) {
                      if (dpost_n > 0) {
                          dpre_acc_skip = true;  // wait for D_POST recycling to refill the pool
                      } else if (state.multi_token_dpre_count() == 0) {
                          // All requests in qk are first-pass (iter=0).
                          // Check if prefill pipeline will produce more.
                          const int pipe = state.count_phase(Phase::WAIT_INPUT_DOWN)
                                         + state.count_phase(Phase::P_PROC_RUNNING);
                          if (pipe > 0 && dpre_n + pipe >= target) {
                              dpre_acc_skip = true;  // Wait for pipeline to fill the batch
                          }
                      }
                  }
              }
#endif
if (hid >= 0 && !dpre_acc_skip) {
#ifdef V11_PPRECAP
                  if (state.up_inflight_pre() < static_cast<size_t>(max_inflight_eff))
#endif
                 {
                     state.rl_pop(Phase::READY_D_PRE, hc);  // rl_head peeks; pop removes
                     spec.step = StepKind::D_PRE;
                     spec.cloud = -1;
                     spec.ids = {hid};
                     // total D PRE-ready = remaining bucket sizes + the head we just popped
                     int total = 1;
                     for (int c = 0; c < state.num_clouds(); ++c)
                         total += state.rl_size(Phase::READY_D_PRE, c);
                     const int m = policies_.batch->group_size(StepKind::D_PRE, total, state, t);
                     // World-A: issue-size override — fill toward the table's
                     // m* (capped by the actually-ready pool) when active.
                     const int m_eff = (mstar_target > m)
                         ? std::min(mstar_target, total) : m;
                     if (m_eff > 1 && policies_.combine_dpre) {
                         // DYNAMIC CROSS-CLOUD CAP
                         // D_PRE UP transfer size is the member count per cloud, NOT Lin.
                         // We evaluate the cost of transferring 1 token vs the decode compute.
                         const double xfer_1 = transfer_time(cfg_, 1.0);
                         const double dpre_1 = state.table().get(Step::DECODE_PRE, 1);
                         const double dpost_1 = state.table().get(Step::DECODE_POST, 1);
                         const double decode_ref = std::max({dpre_1, dpost_1, 1e-9});
                         const double xfer_ratio = xfer_1 / decode_ref;
                         int dynamic_cap;
                         if (xfer_ratio < 0.05) {
                             dynamic_cap = state.num_clouds();
                         } else if (xfer_ratio < 0.3) {
                             dynamic_cap = std::max(2, state.num_clouds());
                         } else if (xfer_ratio < 1.0) {
                             dynamic_cap = std::max(2, state.num_clouds() / 2);
                         } else if (xfer_ratio < 5.0) {
                             dynamic_cap = std::min(policies_.xremote_cap, 2);
                         } else {
                             dynamic_cap = 1;
                         }
                         // Link saturation override
                         if (state.up().in_flight() > 2 &&
                             state.up().head_finish() > t + 2.0 * xfer_1) {
                             dynamic_cap = 1;
                         }
                         int eff_cap = dynamic_cap;
                         // 1. FIRST fill from the home cloud (hc) up to m_eff.
                         // This adds NO extra UP transfer overhead.
                         while (static_cast<int>(spec.ids.size()) < m_eff &&
                                !state.rl_empty(Phase::READY_D_PRE, hc)) {
                             spec.ids.push_back(state.rl_pop(Phase::READY_D_PRE, hc));
                         }
                         // 2. ONLY THEN add from other clouds if we still need more
                         // and eff_cap allows.
                         int clouds_used = 1;
                         if (static_cast<int>(spec.ids.size()) < m_eff && eff_cap > 1) {
                             std::vector<std::pair<int, int>> others;
                             for (int c = 0; c < state.num_clouds(); ++c) {
                                 if (c == hc) continue;
                                 int sz = state.rl_size(Phase::READY_D_PRE, c);
                                 if (sz > 0) others.push_back({sz, c});
                             }
                             std::sort(others.rbegin(), others.rend()); // Pick clouds with most ready requests first
                             for (auto [sz, c] : others) {
                                 if (clouds_used >= eff_cap ||
                                     static_cast<int>(spec.ids.size()) >= m_eff) break;
                                 while (static_cast<int>(spec.ids.size()) < m_eff &&
                                        !state.rl_empty(Phase::READY_D_PRE, c)) {
                                     spec.ids.push_back(state.rl_pop(Phase::READY_D_PRE, c));
                                 }
                                 ++clouds_used;
                             }
                         }
                     } else if (m_eff > 1) {
                         int want = m_eff - 1;
                         int got = 0;
                         while (got < want && !state.rl_empty(Phase::READY_D_PRE, hc)) {
                             spec.ids.push_back(state.rl_pop(Phase::READY_D_PRE, hc));
                             ++got;
                         }
                     }
                     return spec;
                 }
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

     // 1. Prefill (TDR) — THEOREM 13: Smith's rule — SPT (smallest Lin first)
     // minimizes the mean flow time (TDR); FIFO lets large-Lin prefills block
     // small ones (CF weak tests #5/#6/#14 are TDR-huge, tp-deficit cases).
     // TLE GUARD: this scan is the only unbounded per-frame loop. The protocol
     // allows up to 2*10^6 frames; with R up to 2000 the scan costs up to
     // ~10us/frame => ~21s > the 15s limit => a legal test scores 0. Past the
     // frame budget below, take the FIFO head instead (O(1)): a legal, purely
     // ordering-level choice. TLE (score 0) strictly dominates any ordering
     // loss, and tests under 1.2M frames keep exact SPT behavior.
     int id = -1;
     if (state.frames() >= 1200000LL) {
         id = state.rl_head(Phase::READY_P_PROC, c);
         if (id >= 0) state.rl_remove(id);
     } else {
         int best_rid = -1;
         double best_lin = 1e300;
         for (int rid = state.rl_head(Phase::READY_P_PROC, c); rid != -1;
              rid = state.request(rid).next_ready) {
             if (state.request(rid).lin < best_lin) {
                 best_lin = static_cast<double>(state.request(rid).lin);
                 best_rid = rid;
             }
         }
         if (best_rid >= 0) {
             state.rl_remove(best_rid);
             id = best_rid;
         }
     }
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

     // 2. Decode (Throughput/TPOT)
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