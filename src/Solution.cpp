// =============================================================================
// Solution.cpp — Layer 4 entry point: the frame loop and the wire.
//
// main() drives the judge protocol end to end:
//   1. read config + task-time table; select the runtime policy stack
//      (compute_alpha: the regime detector — trivial-SLO early-out, the
//      dist_base==0 all-or-nothing analysis, and the continuous w_tp/w_c
//      mapping);
//   2. per frame: apply every event, refresh the traffic-aware cliff flip
//      (under dist_base==0, provably-unreachable SLOs permanently disable
//      SLO-driven holds), then hand the state to Scheduler::decide;
//   3. serialize the response through BufWriter — a 128 KiB hand-rolled
//      buffer with one write(2) per frame (the statement requires a flush
//      after every response; std::ostream overhead is measurable at the
//      2·10^6-frame scale);
//   4. at END, emit the [ORACLE FINAL] projection — the local oracle gate
//      (tools/interactor.py --oracle) requires it to match the judge's
//      realized means to < 1e-6 — plus the D-141 structural triage meters
//      (busy-time vs physics-minimum decomposition; diagnostics only).
// =============================================================================
#include "protocol.hpp"
#include "task_table.hpp"
#include "state.hpp"
#include "scheduler.hpp"
#include "policies.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <iomanip>
#include <iostream>
#include <vector>
#include <chrono>

using namespace icpc2026;

// Buffered output writer: replaces std::cout to eliminate per-call
// std::ostream overhead and flush-induced syscall micro-costs.
// write() serialises into a 128 KiB buffer; flush() issues a single
// write(2) per frame (required by the interactor protocol).
class BufWriter {
public:
    BufWriter() { buf_.reserve(BUF); }
    ~BufWriter() { flush(); }

    void flush() {
        if (pos_ == 0) return;
        ssize_t n = ::write(1, buf_.data(), pos_);
        if (n > 0) {
            if (static_cast<size_t>(n) < pos_) std::memmove(buf_.data(), buf_.data() + n, pos_ - n);
            pos_ -= static_cast<size_t>(n);
        }
    }

    void write(const char* s, size_t n) {
        if (pos_ + n > BUF) flush();
        if (n >= BUF) {
            flush();
            while (n > 0) {
                ssize_t w = ::write(1, s, n);
                if (w <= 0) break;
                s += w;
                n -= static_cast<size_t>(w);
            }
            return;
        }
        std::memcpy(buf_.data() + pos_, s, n);
        pos_ += n;
    }

    void c_str(const char* s) { write(s, std::strlen(s)); }

    void i64(long long v) {
        if (v == 0) { c_str("0"); return; }
        if (v < 0) { c_str("-"); v = -v; }
        char tmp[32]; int n = 0;
        while (v > 0) { tmp[n++] = char('0' + (v % 10)); v /= 10; }
        for (int i = n - 1; i >= 0; --i) digit(tmp[i]);
    }

    void digit(char c) { write(&c, 1); }
    void space() { digit(' '); }
    void nl()    { digit('\n'); }

private:
    static constexpr size_t BUF = (1 << 17); // 128 KiB
    std::vector<char> buf_;
    size_t pos_ = 0;
};

// PSGO Batch 1: Continuous SLO Vector Space — alpha ∈ [0, 1]
// alpha = 1.0 -> Pure Throughput (max batching, ignore SLOs)
// alpha = 0.0 -> Pure Waiting (size-1 batches, protect SLOs strictly)
static double compute_alpha(const Config& cfg, const TaskTimeTable& table) {
    // All-or-Nothing Gate (7.4): when dist_base == 0, c_w is binary (1 iff the
    // exact distance is 0, effectively never reached), so the score collapses to
    // throughput-only (w_c contributes ~0). In BOTH gate outcomes we therefore
    // optimize throughput behavior.
    if (cfg.dist_base == 0.0) {
        const double tproc = transfer_time(cfg, 1.0);
        const double min_TDR = 3.0 * cfg.S
                              + table.get(Step::PREFILL_PRE, 1)
                              + table.get(Step::PREFILL_PROC, 1)
                              + table.get(Step::PREFILL_POST, 1)
                              + 2.0 * tproc;
        const double min_TPOT = 3.0 * cfg.S
                              + table.get(Step::DECODE_PRE, 1)
                              + table.get(Step::DECODE_PROC, 1)
                              + table.get(Step::DECODE_POST, 1)
                              + 2.0 * tproc;
        // If SLOs are unattainable even at min settings, go pure throughput.
        if (min_TDR > cfg.SLO1 || min_TPOT > cfg.SLO2) {
            return 1.0;
        }
        // Otherwise fall through to continuous weight mapping.
    }
    // Continuous weight mapping (dist_base > 0 or SLOs attainable):
    // alpha = w_tp / (w_tp + w_c)
    // alpha = 1.0 when w_c = 0 (pure throughput)
    // alpha = 0.0 when w_tp = 0 (pure waiting)
    // alpha = 0.5 when balanced
    //
    // LEVEL-2 X10 / D-058: SLOs trivially satisfiable. Bound the WORST-case
    // TDR/TPOT (every request chained behind every other: R <= 2000, Lin and
    // group sizes <= 4096/2000, abyss transfer time included). If even that
    // serialized monster meets both targets, dist = 0 for ANY schedule and
    // the waiting component is provably 1.0 — every drop of SLO protection
    // is wasted: pure throughput.
    {
        const double Rmax = 2000.0;
        const double pre = table.get(Step::PREFILL_PRE, 4096);
        const double proc = table.get(Step::PREFILL_PROC, 4096);
        const double post = table.get(Step::PREFILL_POST, 4096);
        const double tr4k = transfer_time(cfg, 4096.0);
        const double worst_tdr = Rmax * (3.0 * cfg.S + pre + proc + post + 2.0 * tr4k);
        const double dpre = std::max(table.get(Step::DECODE_PRE, 1), table.get(Step::DECODE_PRE, 2000));
        const double dpost = std::max(table.get(Step::DECODE_POST, 1), table.get(Step::DECODE_POST, 2000));
        const double tr2k = static_cast<double>(cfg.K) * cfg.latency_ms
                         + 8.0 * 2000.0 * cfg.bytes_per_token / (cfg.bandwidth_gbps * 1e6);
        const double dproc_wc = table.get(Step::DECODE_PROC, std::max(1, 2000 / std::max(1, cfg.K)));
        const double worst_tpot = Rmax * (3.0 * cfg.S + dpre + dproc_wc + dpost + 2.0 * tr2k);
        if (worst_tdr <= cfg.SLO1 && worst_tpot <= cfg.SLO2) {
            return 1.0;
        }
    }
    const double sum_w = cfg.w_tp + cfg.w_c;
    if (sum_w <= 0.0) return 0.5;  // fallback
    double alpha = cfg.w_tp / sum_w;
    // Clamp to [0, 1] for safety
    if (alpha < 0.0) alpha = 0.0;
    if (alpha > 1.0) alpha = 1.0;
    return alpha;
}

int main() {
    BufWriter w;

    ProtocolReader reader;
    Config cfg;
    TaskTimeTable table;
    int N = 0;
    std::vector<std::vector<double>> rows;
    if (reader.read_config(cfg) && reader.read_table(N, rows)) {
        table.load(N, rows);

        State state;
        state.init(cfg);
        state.set_table(table);

// ---- PSGO Batch 1: Continuous SLO Vector Space ----
        const double alpha = compute_alpha(cfg, table);
        // One-shot startup regime line (within the <1KB stderr budget).
        std::cerr << "[alpha] " << alpha
                  << " (w_tp=" << cfg.w_tp << " w_c=" << cfg.w_c
                  << " dist_base=" << cfg.dist_base
                  << " SLO1=" << cfg.SLO1 << " SLO2=" << cfg.SLO2 << ")\n";

        // All policy objects live for the whole program (referenced by pointer).
        PrefillLoadBalanceCloud balance;
        MarginalBatch marginal;
        SloAwareDelay slo_aware;
        NoDelay no_delay;

        Policies policies;
        policies.alpha = alpha;

        // Cloud assignment: balance the ACTUAL prefill pipeline (P_PROC queue
        // + busy_until), not the Lin proxy for decode load.
        policies.cloud = &balance;

        OnePieceChunk onepiece;
        AdaptiveChunk achunk;

        // THEOREM 11: starvation-aware chunking — but gated to the S<1.0
        // population: the always-on variant broke example1 (S=1.0, score
        // 500.000 -> 0.000: the chunked schedule costs 1ms TDR, TP drops
        // below tp_base and the binary waiting component flips to 0).
        policies.chunk = (cfg.S < 1.0 && cfg.num_layers > 8)
        ? static_cast<ChunkPolicy*>(&achunk)
        : static_cast<ChunkPolicy*>(&onepiece);

        // Batching: MarginalBatch throughout; alpha reaches it via the SLO cap
        // scan and the dynamic cap ladder.
        policies.batch = &marginal;

        // Delay policy: use SloAwareDelay for alpha < 0.8, NoDelay for alpha >= 0.8
        policies.delay = (alpha < 0.8) ? static_cast<DelayPolicy*>(&slo_aware)
                                       : static_cast<DelayPolicy*>(&no_delay);

        // Dynamic cross-remote D PRE combine parameters
        policies.combine_dpre = (alpha > 0.2);
        policies.xremote_cap = 1 + static_cast<int>(alpha * (cfg.K - 1));

        // Set alpha on MarginalBatch for dynamic batch sizing
        marginal.set_alpha(alpha);
        // Set alpha on SloAwareDelay for dynamic hold behavior
        slo_aware.set_alpha(alpha);

        Scheduler scheduler(cfg, table, policies);
        // Zero-Allocation Hot Path (Essay 3): precompute the marginal batch size
        // once at startup (NOT per frame — earlier bug ran this inside the output
        // loop, hanging the solver). For a linear/sublinear table the profit cap is
        // the largest listed batch_size (Spec D-004: batch_size in [1..4096]);
        // group_size is then a O(1) min(n_ready, max_m_).
        MarginalBatch* mb = dynamic_cast<MarginalBatch*>(const_cast<BatchPolicy*>(policies.batch));
        if (mb) {
            mb->precompute(cfg, table);
#ifndef PSGO_STATIC
            // D-064b Variant D3: flag a "flat" decode table (batching
            // computationally free up to the max) -> the OD-9 trigger (flat clamp
            // + w_c>0). NOTE: the naive test max_m(D_PRE)==max_batch_for(D_PRE)
            // is a no-op here -- both the flat (FLAT_ROWS) and default
            // (DEFAULT_ROWS) tables climb to the 1<<20 unbounded sentinel, so
            // max_m != max_batch_for for BOTH. The real discriminator is that
            // each decode column's duration is constant across the batch-size
            // range; only FLAT_ROWS satisfies that.
            bool flat = true;
            for (int sc = static_cast<int>(Step::DECODE_PRE);
                 sc <= static_cast<int>(Step::DECODE_POST); ++sc) {
                const Step col = static_cast<Step>(sc);
                const int maxb = table.max_batch_for(col);
                if (maxb <= 1) continue;
                if (table.get(col, maxb) > table.get(col, 1) + 1e-9) { flat = false; break; }
            }
            state.set_flat_decode_table(flat);
            policies.psgo_max_m = mb->max_batch_size(StepKind::D_PROC);
#endif
        }

        std::vector<Event> events;
        double t = 0.0;
        // ESSAY 2 / D-050: traffic-aware all-or-nothing cliff. The startup gate
        // (compute_alpha) floors TDR with Lin=1 lookups; real traffic may have a
        // much higher floor. Under dist_base==0 the waiting component is binary:
        // if every ARRIVED request's own minimum TDR already exceeds SLO1, the
        // mean can never meet it and SLO-chasing is provably worthless — flip
        // permanently to pure throughput. Any feasible arrival cancels the flip
        // (mixed traffic may still meet the mean).
        bool cliff_feasible = false;
        bool cliff_flipped = false;
        long long frame_count = 0;
        const long long MAX_FRAMES = 10000000LL; // emergency deadlock guard (Trap 10)
        while (reader.next_frame(t, events)) {
            ++frame_count;
            if (frame_count > MAX_FRAMES) {
                // Emergency: issue nothing to avoid deadlock; judge will time out.
                w.c_str("0\n");
                w.flush();
                break;
            }
            for (const Event& ev : events) state.apply_event(ev, t);
            state.end_frame();
            if (cfg.dist_base == 0.0 && !cliff_flipped && !cliff_feasible) {
                bool any_arr = false;
                for (const Event& ev : events) {
                    if (ev.kind != EventKind::ARR) continue;
                    any_arr = true;
                    const double fl = 3.0 * cfg.S
                        + table.get(Step::PREFILL_PRE, ev.lin)
                        + table.get(Step::PREFILL_PROC, ev.lin)
                        + table.get(Step::PREFILL_POST, ev.lin)
                        + 2.0 * transfer_time(cfg, static_cast<double>(ev.lin));
                    if (fl <= cfg.SLO1 + 1e-9) { cliff_feasible = true; break; }
                }
                if (any_arr && !cliff_feasible) {
                    cliff_flipped = true;
                    // Minimal, empirically-validated flip: disable the SLO-driven
                    // D PRE hold only. Forcing alpha=1 caps (max_m) REGRESSED the
                    // sublinear-table trap by 6x (90.5 -> 13.9): MarginalBatch's
                    // precomputed cap is already the safe throughput optimum, and
                    // coarser groups fragment the decode pipeline. The hold is
                    // the only behavior that sacrifices throughput for a
                    // provably-unreachable SLO.
                    policies.delay = &no_delay;
                    std::cerr << "[cliff] traffic-aware all-or-nothing flip: "
                                 "every arrived request's min TDR > SLO1; "
                                 "SLO-driven D PRE holds disabled\n";
                }
            }
#ifdef LOCAL_DIAG
            // Periodic oracle projection: LOCAL diagnostic builds only. A
            // 2M-frame test would emit ~46KB of stderr here — if the judge's
            // interactor does not drain stderr, a 64KB pipe fills and BLOCKS
            // the solver (TLE). The submission keeps only the single-shot
            // [alpha]/[cliff]/[ORACLE FINAL] lines (<1KB total).
            if (frame_count % 10000 == 0) {
                const auto& o = state.oracle();
                std::cerr << std::setprecision(12)
                          << "[ORACLE frame=" << frame_count
                          << " tdr=" << o.projected_tdr()
                          << " tpot=" << o.projected_tpot()
                          << " tp=" << o.projected_tp()
                          << " tdr_excess=" << o.excess_tdr(state.cfg().SLO1)
                          << " tpot_excess=" << o.excess_tpot(state.cfg().SLO2)
                          << " cw=" << o.projected_cw_exact(state.cfg().SLO1, state.cfg().SLO2, state.cfg().dist_base)
                          << " score=" << o.projected_score(state.cfg().w_tp, state.cfg().w_c,
                                                              state.cfg().tp_base, state.cfg().tp_UB,
                                                              state.cfg().SLO1, state.cfg().SLO2, state.cfg().dist_base)
                          << "]\n";
            }
#endif
            const std::vector<Assignment> assigns = scheduler.decide(state, t);
            w.i64(assigns.size());
            w.nl();
            for (const Assignment& a : assigns) {
                const TaskSpec& sp = a.spec;
                switch (sp.step) {
                    case StepKind::P_PRE:
                        w.c_str("E P PRE "); w.i64(sp.cloud); w.space(); w.i64(sp.ids[0]); w.nl();
                        break;
                    case StepKind::P_PROC:
                        w.c_str("C"); w.i64(sp.cloud); w.c_str(" P PROC ");
                        w.i64(sp.ls); w.space(); w.i64(sp.le); w.space();
                        w.i64(sp.cloud); w.space(); w.i64(sp.ids[0]); w.nl();
                        break;
                    case StepKind::P_POST:
                        w.c_str("E P POST "); w.i64(sp.cloud); w.space(); w.i64(sp.ids[0]); w.nl();
                        break;
                    case StepKind::D_PRE:
                        w.c_str("E D PRE -1 "); w.i64(sp.ids.size());
                        for (int rid : sp.ids) { w.space(); w.i64(rid); }
                        w.nl();
                        break;
                    case StepKind::D_PROC:
                        w.c_str("C"); w.i64(sp.cloud); w.c_str(" D PROC ");
                        w.i64(sp.cloud); w.space(); w.i64(sp.ids.size());
                        for (int rid : sp.ids) { w.space(); w.i64(rid); }
                        w.nl();
                        break;
                    case StepKind::D_POST:
                        w.c_str("E D POST -1 "); w.i64(sp.ids.size());
                        for (int rid : sp.ids) { w.space(); w.i64(rid); }
                        w.nl();
                        break;
                }
            }
            w.flush();
        }
        const auto& o = state.oracle();
        // D-124: [ORACLE FINAL] is REQUIRED by the local oracle gate
        // (interactor.py oracle_final_check: tdr/tpot must match the judge's
        // to < 1e-6); the marginal/region tail is informational only and was
        // dropped from the artifact (saved ~540 chars, stderr stays tiny).
        std::cerr << std::setprecision(12)
                  << "[ORACLE FINAL frame=" << frame_count
                  << " tdr=" << o.projected_tdr()
                  << " tpot=" << o.projected_tpot()
                  << "]\n";
        // D-141m structural triage meter: actual busy time vs the physics
        // minimum, computed from each request's real Lin and observed tokens.
        // mbar=R is the most optimistic batching, so e_min/up_min are true
        // lower bounds. Read: E/mk~1 local-saturated; UP/mk~1 link-saturated;
        // everything << 1 with mk >> last_arr -> starvation (recoverable);
        // mk ~ last_arr -> arrival-capped; 2*tdr*R ~ up_min -> TDR is bytes-bound.
        const double mk = o.last_finish - o.earliest_arrival;
        long long ntok = 0;
        double e_min = 0, up_min = 0, e_prefill = 0;
        const int nr = state.num_requests();
        const int mbar = std::max(1, nr);
        for (int i = 0; i < nr; ++i) {
            if (!state.has_request(i)) continue;
            const auto& r = state.request(i);
            ntok += r.iter;
            e_min += 2.0 * cfg.S + table.get(Step::PREFILL_PRE, r.lin) + table.get(Step::PREFILL_POST, r.lin);
            e_prefill += 2.0 * cfg.S + table.get(Step::PREFILL_PRE, r.lin) + table.get(Step::PREFILL_POST, r.lin);
            up_min += transfer_time(cfg, static_cast<double>(r.lin));
        }
        e_min += (static_cast<double>(ntok) / mbar) *
            (2.0 * cfg.S + table.get(Step::DECODE_PRE, mbar) + table.get(Step::DECODE_POST, mbar));
        up_min += (static_cast<double>(ntok) / mbar) *
            static_cast<double>(std::max(1, cfg.K)) *
            transfer_time(cfg, static_cast<double>(mbar) / std::max(1, cfg.K));
        // 2b: E-time decomposition. E_decode vs f(m*)*T decides World A
        // (flat table: big headroom) vs World B (linear table: floor-bound).
        const int m_star = table.decode_m_star(cfg.S);
        const double f_star = (2.0 * cfg.S + table.get(Step::DECODE_PRE, m_star)
                               + table.get(Step::DECODE_POST, m_star)) / static_cast<double>(m_star);
        const double e_decode = state.e_busy() - e_prefill;
        const double floor_decode = f_star * static_cast<double>(ntok);
        std::cerr << "[METER] mk=" << mk << " E=" << state.e_busy() << "/" << e_min
                  << " UP=" << state.up_busy() << "/" << up_min << " C=" << state.c_busy()
                  << " tokens=" << ntok << " R=" << nr << " last_arr=" << state.last_arr()
                  << " 2*tdr*R=" << 2.0 * o.projected_tdr() * nr << "\n";
        std::cerr << "[ED] Ep=" << e_prefill << " Ed=" << e_decode
                  << " fl(m*=" << m_star << ")=" << floor_decode << "\n";
    }
#ifdef LOCAL_DIAG
    // Instrument 3: end-of-run group-size histogram (dev builds only).
    diag_dump_hist();
#endif
    return 0;
}