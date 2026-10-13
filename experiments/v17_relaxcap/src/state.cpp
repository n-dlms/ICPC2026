// Layer 3: state machine.
#include "state.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace icpc2026 {

namespace {

#ifdef DEBUG
bool is_ready_phase(Phase p) {
    return p == Phase::READY_P_PRE || p == Phase::READY_P_PROC ||
           p == Phase::READY_P_POST || p == Phase::READY_D_PRE ||
           p == Phase::READY_D_PROC || p == Phase::READY_D_POST;
}
#endif

}

void TransferQueue::push(double enter_t, double dur) {
    const double start = std::max(enter_t, tail);
    finish_times.push_back(start + dur);
    tail = start + dur;
}

void TransferQueue::drain(double t) {
    // Remove every transfer whose finish time has passed by time t. Called once
    // per frame (at the gate) using the frame's timestamp, so it correctly
    // accounts for frame coalescing: a transfer that completed at an earlier
    // instant but whose XDN was delivered in this (later) frame is drained here,
    // before any new enqueues are counted. We remove from the front (oldest)
    // because the FIFO is serialized (finish times non-decreasing).
    while (!finish_times.empty() && finish_times.front() <= t + 1e-9) {
        finish_times.erase(finish_times.begin());
    }
    tail = finish_times.empty() ? 0.0 : finish_times.back();
}

void State::init(const Config& cfg) {
    cfg_ = cfg;
    num_clouds_ = cfg.K;
    reqs_.clear();
    seen_.clear();
    local_ = CurrentTask{};
    clouds_.assign(static_cast<size_t>(cfg.K), CurrentTask{});
    cloud_load_.assign(static_cast<size_t>(cfg.K), 0);
    cloud_decode_est_.assign(static_cast<size_t>(cfg.K), 0.0);
    ppre_.reset();
    g_head_.assign(static_cast<size_t>(NUM_PHASES), -1);
    g_tail_.assign(static_cast<size_t>(NUM_PHASES), -1);
    g_count_.assign(static_cast<size_t>(NUM_PHASES), 0);
    c_head_.assign(static_cast<size_t>(NUM_PHASES), std::vector<int>(static_cast<size_t>(cfg.K), -1));
    c_tail_.assign(static_cast<size_t>(NUM_PHASES), std::vector<int>(static_cast<size_t>(cfg.K), -1));
    c_count_.assign(static_cast<size_t>(NUM_PHASES), std::vector<int>(static_cast<size_t>(cfg.K), 0));
    multi_token_dpre_count_ = 0;
    up_ = TransferQueue{};
    down_ = TransferQueue{};
    // Transfers are bounded by the number of concurrent tasks (local + K clouds);
    // reserve to avoid reallocation as the FIFO fluctuates.
    up_.reserve(static_cast<size_t>(cfg.K) + 8);
    down_.reserve(static_cast<size_t>(cfg.K) + 8);
    finish_pending_.clear();
    stats_ = Stats{};
    oracle_ = Oracle{};
    dynamic_max_batch_ = -1;
}

void State::apply_event(const Event& ev, double t) {
    switch (ev.kind) {
    case EventKind::ARR: {
        const int rid = ev.rid;
        // Requests may arrive in ANY order (protocol sends ARR by arrival time,
        // not by rid). Index by rid, never by push order.
        assert(rid >= 0);
        if (rid >= static_cast<int>(reqs_.size())) {
            reqs_.resize(static_cast<size_t>(rid) + 1);
        }
        if (rid >= static_cast<int>(seen_.size())) {
            seen_.resize(static_cast<size_t>(rid) + 1, 0);
        }
        assert(!seen_[static_cast<size_t>(rid)]);  // no duplicate ARR
        Request& req = reqs_[static_cast<size_t>(rid)];
        req = Request{};
        req.id = rid;
        req.lin = ev.lin;
        req.arrival = t;
        req.phase = Phase::READY_P_PRE;
        seen_[static_cast<size_t>(rid)] = 1;
        req.in_ready_list = true;
        ppre_push(rid);
        stats_.earliest_arrival = std::min(stats_.earliest_arrival, t);
        oracle_.earliest_arrival = std::min(oracle_.earliest_arrival, t);
        break;
    }
    case EventKind::TDN: {
        assert(ev.server >= -1 && ev.server < num_clouds_);
        TaskSpec spec;
        [[maybe_unused]] const bool parsed = spec.parse(ev.spec, cfg_);
        assert(parsed);
        CurrentTask* ct = (ev.server == -1) ? &local_ : &clouds_[static_cast<size_t>(ev.server)];
        assert(ct->busy);
#ifdef DEBUG
        {
            const std::vector<std::string> issued = ct->spec;
            assert(issued.size() == ev.spec.size());
            for (size_t i = 0; i < issued.size(); ++i) assert(issued[i] == ev.spec[i]);
            // LEVEL-2 Extreme 6: at t~1e10-1e12 the double ULP (1e-5..1e-3)
            // dwarfs a fixed 1e-6; scale the tolerance with magnitude so the
            // assert stays meaningful instead of living on bit-exact
            // arithmetic coincidence.
            assert(std::fabs(ct->busy_until - t) <= 1e-6 + std::fabs(t) * 1e-9);
        }
#endif
        ct->busy = false;
        ct->spec.clear();
        ct->ids.clear();

        [[maybe_unused]] const bool local_step = spec.step == StepKind::P_PRE ||
                                                 spec.step == StepKind::P_POST ||
                                                 spec.step == StepKind::D_PRE ||
                                                 spec.step == StepKind::D_POST;
        assert(local_step == (ev.server == -1));

        switch (spec.step) {
        case StepKind::P_PRE: {
            assert(spec.ids.size() == 1);
            const int rid = spec.ids[0];
            Request& req = reqs_[static_cast<size_t>(rid)];
            assert(req.phase == Phase::P_PRE_RUNNING);
            req.phase = Phase::WAIT_INPUT_UP;
            break;
        }
        case StepKind::P_PROC: {
            assert(spec.ids.size() == 1);
            const int rid = spec.ids[0];
            Request& req = reqs_[static_cast<size_t>(rid)];
            assert(req.phase == Phase::P_PROC_RUNNING);
            assert(spec.cloud == req.cloud);
            req.next_ls = spec.le;
            if (spec.le == cfg_.num_layers) {
                req.phase = Phase::WAIT_INPUT_DOWN;
            } else {
                req.phase = Phase::READY_P_PROC;
                req.in_ready_list = true;
                rl_push(Phase::READY_P_PROC, req.cloud, rid);
            }
            break;
        }
        case StepKind::P_POST: {
            assert(spec.ids.size() == 1);
            const int rid = spec.ids[0];
            Request& req = reqs_[static_cast<size_t>(rid)];
            assert(req.phase == Phase::P_POST_RUNNING);
            assert(spec.cloud == req.cloud);
            req.phase = Phase::READY_D_PRE;
            req.ppost_t = t;
            stats_.sum_tdr += t - req.arrival;
            oracle_.sum_tdr += t - req.arrival;
            oracle_.count_tdr += 1;
            req.in_ready_list = true;
            rl_push(Phase::READY_D_PRE, req.cloud, rid);
            break;
        }
        case StepKind::D_PRE: {
            for (const int rid : spec.ids) {
                Request& req = reqs_[static_cast<size_t>(rid)];
                assert(req.phase == Phase::D_PRE_RUNNING);
                req.phase = Phase::WAIT_DEC_UP;
            }
            break;
        }
        case StepKind::D_PROC: {
            for (const int rid : spec.ids) {
                Request& req = reqs_[static_cast<size_t>(rid)];
                assert(req.phase == Phase::D_PROC_RUNNING);
                assert(req.cloud == spec.cloud);
                req.phase = Phase::WAIT_DEC_DOWN;
            }
            break;
        }
        case StepKind::D_POST: {
            for (const int rid : spec.ids) {
                Request& req = reqs_[static_cast<size_t>(rid)];
                ++req.iter;
                if (req.first_token < 0.0) req.first_token = t;
                // Track TPOT gap for iter > 1 (gap between this token and previous)
                if (req.iter > 1) {
                    const double gap = t - req.last_token;
                    oracle_.sum_gaps += gap;
                    oracle_.total_gaps += 1;
                }
                req.last_token = t;
                // D-068: the final D_POST TDN may arrive AFTER this request's
                // FIN in the same frame (FIN is heap-ordered first), so the
                // FIN handler cannot see the final token time / count. Update
                // the projection here so tp/score are order-independent; the
                // FIN handler's update stays for the TDN-first order.
                if (req.last_token > oracle_.last_finish) oracle_.last_finish = req.last_token;
                if (req.finished) oracle_.tokens_out += 1;
                if (req.finished) continue;
                assert(req.phase == Phase::D_POST_RUNNING);
                // We do NOT know Lout (protocol never reveals it). Push optimistically
                // to READY_D_PRE; FIN will cancel via rl_remove if the request is done.
                if (req.in_ready_list) rl_remove(rid);
                req.phase = Phase::READY_D_PRE;
                req.in_ready_list = true;
                rl_push(Phase::READY_D_PRE, req.cloud, rid);
            }
            break;
        }
        }
        break;
    }
    case EventKind::XDN: {
        assert(ev.remote >= 0 && ev.remote < num_clouds_);
#if defined(V12_MIRROR) || defined(DEBUG)
        if (ev.up) up_.drain(t); else down_.drain(t);
#endif
        if (ev.up) ++up_deq_;
        if (ev.pre) {
            assert(ev.ids.size() == 1);
            const int rid = ev.ids[0];
            Request& req = reqs_[static_cast<size_t>(rid)];
            if (ev.up) {
                assert(req.phase == Phase::WAIT_INPUT_UP);
                assert(req.cloud == ev.remote);
                req.phase = Phase::READY_P_PROC;
                req.in_ready_list = true;
                rl_push(Phase::READY_P_PROC, req.cloud, rid);
            } else {
                assert(req.phase == Phase::WAIT_INPUT_DOWN);
                req.phase = Phase::READY_P_POST;
                req.in_ready_list = true;
                rl_push(Phase::READY_P_POST, -1, rid);
            }
        } else {
            for (const int rid : ev.ids) {
                Request& req = reqs_[static_cast<size_t>(rid)];
                if (ev.up) {
                    assert(req.phase == Phase::WAIT_DEC_UP);
                    assert(req.cloud == ev.remote);
                    req.phase = Phase::READY_D_PROC;
                    req.in_ready_list = true;
                    rl_push(Phase::READY_D_PROC, req.cloud, rid);
                } else {
                    assert(req.phase == Phase::WAIT_DEC_DOWN);
                    req.phase = Phase::READY_D_POST;
                    req.in_ready_list = true;
                    rl_push(Phase::READY_D_POST, -1, rid);
                }
            }
        }
        break;
    }
    case EventKind::FIN: {
        Request& req = reqs_[static_cast<size_t>(ev.rid)];
        assert(!req.finished);
        // Remove from ready list BEFORE changing phase, so rl_remove
        // can find the correct ready list by current phase.
        if (req.in_ready_list) {
            rl_remove(ev.rid);
        }
        req.finished = true;
        req.lout = req.iter;
        req.phase = Phase::FINISHED;
        assert(req.cloud >= 0 && req.cloud < num_clouds_);
        --cloud_load_[static_cast<size_t>(req.cloud)];
        cloud_decode_est_[static_cast<size_t>(req.cloud)] -= static_cast<double>(req.lin);
        finish_pending_.push_back(ev.rid);
        // Update Oracle for final score projection
        oracle_.tokens_out += req.iter;
        if (req.last_token > oracle_.last_finish) oracle_.last_finish = req.last_token;
        break;
    }
    }
}

void State::end_frame() {
    for (const int rid : finish_pending_) {
        Request& req = reqs_[static_cast<size_t>(rid)];
        assert(req.finished);
        if (req.stats_recorded) continue;
        assert(req.first_token >= 0.0);
        stats_.sum_span += req.last_token - req.first_token;
        stats_.total_gaps += req.iter;
        stats_.tokens += req.iter;
        stats_.latest_token = std::max(stats_.latest_token, req.last_token);
        req.stats_recorded = true;
    }
    finish_pending_.clear();
}

void State::issue(const TaskSpec& spec, double t) {
    assert(!spec.ids.empty());
    double dur = 0.0;
    switch (spec.step) {
        case StepKind::P_PROC: {
            const Request& r = reqs_[static_cast<size_t>(spec.ids[0])];
            const double frac = static_cast<double>(spec.le - spec.ls) /
                                static_cast<double>(cfg_.num_layers);
            dur = frac * table_.get(step_of(spec.step), r.lin);
            break;
        }
        case StepKind::P_PRE:
        case StepKind::P_POST:
            dur = table_.get(step_of(spec.step), 1);
            break;
        case StepKind::D_PRE:
        case StepKind::D_PROC:
        case StepKind::D_POST:
            dur = table_.get(step_of(spec.step), static_cast<int>(spec.ids.size()));
            break;
    }
    const double busy_until = t + cfg_.S + dur;

    const bool local_step = spec.step == StepKind::P_PRE || spec.step == StepKind::P_POST ||
                            spec.step == StepKind::D_PRE || spec.step == StepKind::D_POST;
    CurrentTask* ct = nullptr;
    if (local_step) {
        assert(!local_.busy);
        ct = &local_;
    } else {
        assert(spec.cloud >= 0 && spec.cloud < num_clouds_);
        assert(!clouds_[static_cast<size_t>(spec.cloud)].busy);
        ct = &clouds_[static_cast<size_t>(spec.cloud)];
    }
    ct->busy = true;
    ct->busy_until = busy_until;
    ct->step = spec.step;
    ct->ids = spec.ids;
#ifdef DEBUG
    ct->spec = spec.tokens();
#endif

    // Decision-driven UP-FIFO projection (V11_PPRECAP):
    // the instant we ISSUE a task that triggers an UP transfer, we project the
    // enqueue. This is forward-looking, not rearview (TDN-time would lag by the
    // whole S + transfer window and let the local blast the queue).
    if (spec.step == StepKind::P_PRE) {
        ++up_projected_enq_;
    } else if (spec.step == StepKind::D_PRE) {
        int distinct_c = 0;
        std::vector<char> seen(static_cast<size_t>(num_clouds_), 0);
        for (const int rid : spec.ids) {
            const int c = reqs_[static_cast<size_t>(rid)].cloud;
            if (!seen[static_cast<size_t>(c)]) { seen[static_cast<size_t>(c)] = 1; ++distinct_c; }
        }
        up_projected_enq_ += distinct_c;
    }

    // Transfer-FIFO mirror model (shared UP/DOWN links the interactor owns).
    // Maintained under V12_MIRROR (and DEBUG) for the timing view; the gate uses
    // the projection above. enter_t = busy_until = the time this task completes,
    // exactly when the interactor enqueues its transfer.
#if defined(V12_MIRROR) || defined(DEBUG)
    if (spec.step == StepKind::P_PRE) {
        const Request& r = reqs_[static_cast<size_t>(spec.ids[0])];
        up_.push(busy_until, transfer_time(cfg_, static_cast<double>(r.lin)));
    } else if (spec.step == StepKind::P_PROC && spec.le == cfg_.num_layers) {
        const Request& r = reqs_[static_cast<size_t>(spec.ids[0])];
        down_.push(busy_until, transfer_time(cfg_, static_cast<double>(r.lin)));
    } else if (spec.step == StepKind::D_PRE) {
        std::vector<int> cnt(static_cast<size_t>(num_clouds_), 0);
        for (const int rid : spec.ids) ++cnt[static_cast<size_t>(reqs_[static_cast<size_t>(rid)].cloud)];
        for (int c = 0; c < num_clouds_; ++c) {
            if (cnt[static_cast<size_t>(c)] > 0) {
                up_.push(busy_until, transfer_time(cfg_, static_cast<double>(cnt[static_cast<size_t>(c)])));
            }
        }
    } else if (spec.step == StepKind::D_PROC) {
        down_.push(busy_until, transfer_time(cfg_, static_cast<double>(spec.ids.size())));
    }
#endif

    for (const int rid : spec.ids) {
        Request& req = reqs_[static_cast<size_t>(rid)];
        assert(!req.finished);
        req.in_ready_list = false;
        if (spec.step == StepKind::P_PRE) {
            assert(req.phase == Phase::READY_P_PRE);
            req.cloud = spec.cloud;
            ++cloud_load_[static_cast<size_t>(spec.cloud)];
            cloud_decode_est_[static_cast<size_t>(spec.cloud)] += static_cast<double>(req.lin);
            req.phase = Phase::P_PRE_RUNNING;
        } else if (spec.step == StepKind::P_PROC) {
            assert(req.phase == Phase::READY_P_PROC);
            assert(spec.cloud == req.cloud);
            req.phase = Phase::P_PROC_RUNNING;
        } else if (spec.step == StepKind::P_POST) {
            assert(req.phase == Phase::READY_P_POST);
            assert(spec.cloud == req.cloud);
            req.phase = Phase::P_POST_RUNNING;
        } else if (spec.step == StepKind::D_PRE) {
            assert(req.phase == Phase::READY_D_PRE);
            req.phase = Phase::D_PRE_RUNNING;
        } else if (spec.step == StepKind::D_PROC) {
            assert(req.phase == Phase::READY_D_PROC);
            assert(spec.cloud == req.cloud);
            req.phase = Phase::D_PROC_RUNNING;
        } else if (spec.step == StepKind::D_POST) {
            assert(req.phase == Phase::READY_D_POST);
            req.phase = Phase::D_POST_RUNNING;
        }
    }
}

void State::ppre_push(int rid) {
    Request& r = reqs_[static_cast<size_t>(rid)];
    r.prev_ready = -1;
    int lin = r.lin;
    if (lin < 0) lin = 0;
    if (lin > PPRE_MAXLIN) lin = PPRE_MAXLIN;
    int& h = ppre_.head[lin];
    r.next_ready = h;
    if (h != -1) reqs_[static_cast<size_t>(h)].prev_ready = rid;
    h = rid;
    if (ppre_.count[lin] == 0) {
        if (lin > ppre_.max_lin) ppre_.max_lin = lin;
    }
    ++ppre_.count[lin];
    r.in_ready_list = true;
}

int State::ppre_pop() {
    while (ppre_.max_lin >= 0 && ppre_.head[ppre_.max_lin] == -1) --ppre_.max_lin;
    if (ppre_.max_lin < 0) return -1;
    int lin = ppre_.max_lin;
    int rid = ppre_.head[lin];
    Request& r = reqs_[static_cast<size_t>(rid)];
    ppre_.head[lin] = r.next_ready;
    if (ppre_.head[lin] != -1) reqs_[static_cast<size_t>(ppre_.head[lin])].prev_ready = -1;
    r.next_ready = r.prev_ready = -1;
    --ppre_.count[lin];
    r.in_ready_list = false;
    return rid;
}

void State::rl_push(Phase p, int cloud, int rid) {
    Request& r = reqs_[static_cast<size_t>(rid)];
    r.prev_ready = -1; r.next_ready = -1;
    int pi = phase_idx(p);
    if (p == Phase::READY_P_PRE) { ppre_push(rid); return; }
    // Zero-Allocation Hot Path (Decision b): track D_PRE requests that have
    // already started emitting tokens (iter > 0), so hold_dpre can query it in O(1).
    const bool mt = (p == Phase::READY_D_PRE) && (r.iter > 0);
    if (is_percloud(p)) {
        int& h = c_head_[pi][cloud];
        int& t = c_tail_[pi][cloud];
        r.next_ready = -1;
        if (h == -1) { h = rid; }
        else { reqs_[static_cast<size_t>(t)].next_ready = rid; r.prev_ready = t; }
        t = rid;
        ++c_count_[pi][cloud];
    } else {
        int& h = g_head_[pi];
        int& t = g_tail_[pi];
        r.next_ready = -1;
        if (h == -1) { h = rid; }
        else { reqs_[static_cast<size_t>(t)].next_ready = rid; r.prev_ready = t; }
        t = rid;
        ++g_count_[pi];
    }
    if (mt) ++multi_token_dpre_count_;
    r.in_ready_list = true;
}

int State::rl_pop(Phase p, int cloud) {
    int pi = phase_idx(p);
    if (p == Phase::READY_P_PRE) return ppre_pop();
    int h = is_percloud(p) ? c_head_[pi][cloud] : g_head_[pi];
    if (h == -1) return -1;
    Request& r = reqs_[static_cast<size_t>(h)];
    int next = r.next_ready;
    if (is_percloud(p)) {
        c_head_[pi][cloud] = next;
        if (next == -1) c_tail_[pi][cloud] = -1;
        if (next != -1) reqs_[static_cast<size_t>(next)].prev_ready = -1;
        --c_count_[pi][cloud];
    } else {
        g_head_[pi] = next;
        if (next == -1) g_tail_[pi] = -1;
        if (next != -1) reqs_[static_cast<size_t>(next)].prev_ready = -1;
        --g_count_[pi];
    }
    if ((p == Phase::READY_D_PRE) && (r.iter > 0)) --multi_token_dpre_count_;
    r.next_ready = r.prev_ready = -1;
    r.in_ready_list = false;
    return h;
}

int State::rl_size(Phase p, int cloud) const {
    int pi = phase_idx(p);
    if (p == Phase::READY_P_PRE) {
        int s = 0;
        for (int b = 0; b < PPRE_BUCKETS; ++b) s += ppre_.count[b];
        return s;
    }
    return is_percloud(p) ? c_count_[pi][cloud] : g_count_[pi];
}

int State::rl_head(Phase p, int cloud) const {
    int pi = phase_idx(p);
    if (p == Phase::READY_P_PRE) {
        if (ppre_.max_lin < 0) return -1;
        return ppre_.head[ppre_.max_lin];
    }
    return is_percloud(p) ? c_head_[pi][cloud] : g_head_[pi];
}

void State::rl_remove(int rid) {
    Request& r = reqs_[static_cast<size_t>(rid)];
    if (!r.in_ready_list) return;
    Phase p = r.phase;
    int pi = phase_idx(p);
    if (p == Phase::READY_P_PRE) {
        int lin = r.lin; if (lin < 0) lin = 0; if (lin > PPRE_MAXLIN) lin = PPRE_MAXLIN;
        int& h = ppre_.head[lin]; int& ta = ppre_.tail[lin]; int& cnt = ppre_.count[lin];
        if (h == rid) h = r.next_ready;
        if (ta == rid) ta = r.prev_ready;
        if (r.prev_ready != -1) reqs_[static_cast<size_t>(r.prev_ready)].next_ready = r.next_ready;
        if (r.next_ready != -1) reqs_[static_cast<size_t>(r.next_ready)].prev_ready = r.prev_ready;
        --cnt;
        r.prev_ready = r.next_ready = -1;
        r.in_ready_list = false;
        if (cnt == 0 && lin == ppre_.max_lin) {
            while (ppre_.max_lin >= 0 && ppre_.head[ppre_.max_lin] == -1) --ppre_.max_lin;
        }
        return;
    }
    int* h = is_percloud(p) ? &c_head_[pi][r.cloud] : &g_head_[pi];
    int* ta = is_percloud(p) ? &c_tail_[pi][r.cloud] : &g_tail_[pi];
    int* ct = is_percloud(p) ? &c_count_[pi][r.cloud] : &g_count_[pi];
    if (*h == rid) *h = r.next_ready;
    if (*ta == rid) *ta = r.prev_ready;
    if (r.prev_ready != -1) reqs_[static_cast<size_t>(r.prev_ready)].next_ready = r.next_ready;
    if (r.next_ready != -1) reqs_[static_cast<size_t>(r.next_ready)].prev_ready = r.prev_ready;
    --(*ct);
    if ((p == Phase::READY_D_PRE) && (r.iter > 0)) --multi_token_dpre_count_;
    r.prev_ready = r.next_ready = -1;
    r.in_ready_list = false;
}

int State::count_phase(Phase p) const {
    int s = 0;
    if (p == Phase::READY_P_PRE) {
        for (int b = 0; b < PPRE_BUCKETS; ++b) s += ppre_.count[b];
        return s;
    }
    int pi = phase_idx(p);
    if (is_percloud(p)) {
        for (int c = 0; c < num_clouds_; ++c) s += c_count_[pi][c];
    } else {
        s = g_count_[pi];
    }
    return s;
}

#ifdef DEBUG
void State::check_invariants(double t) const {
    (void)t;
    for (size_t i = 0; i < reqs_.size(); ++i) {
        if (!seen_[i]) continue;  // skip not-yet-arrived slots
        const Request& req = reqs_[i];
        if (req.in_ready_list) {
            assert(is_ready_phase(req.phase));
        } else {
            assert(!is_ready_phase(req.phase) || req.finished);
        }
        if (req.finished) {
            assert(req.phase == Phase::FINISHED);
            assert(!req.in_ready_list);
        }
    }
    int counted = 0;
    for (int b = 0; b < PPRE_BUCKETS; ++b) counted += ppre_.count[b];
    // Global phases (READY_P_POST, READY_D_POST)
    for (int pi = 0; pi < NUM_PHASES; ++pi) {
        Phase p = static_cast<Phase>(pi);
        if (is_percloud(p) || p == Phase::READY_P_PRE) continue;
        for (int rid = g_head_[pi]; rid != -1; rid = reqs_[static_cast<size_t>(rid)].next_ready) {
            assert(rid >= 0 && rid < static_cast<int>(reqs_.size()));
            assert(reqs_[static_cast<size_t>(rid)].in_ready_list);
            ++counted;
        }
    }
    // Per-cloud phases
    for (int pi = 0; pi < NUM_PHASES; ++pi) {
        Phase p = static_cast<Phase>(pi);
        if (!is_percloud(p)) continue;
        for (int c = 0; c < num_clouds_; ++c) {
            for (int rid = c_head_[pi][c]; rid != -1; rid = reqs_[static_cast<size_t>(rid)].next_ready) {
                assert(rid >= 0 && rid < static_cast<int>(reqs_.size()));
                assert(reqs_[static_cast<size_t>(rid)].in_ready_list);
                ++counted;
            }
        }
    }
    counted += 0;
    int flagged = 0;
    int mt_check = 0;
    for (size_t i = 0; i < reqs_.size(); ++i) {
        if (!seen_[i]) continue;
        if (reqs_[i].in_ready_list) ++flagged;
        if (reqs_[i].phase == Phase::READY_D_PRE && reqs_[i].in_ready_list && reqs_[i].iter > 0) ++mt_check;
    }
    assert(counted == flagged);
    assert(mt_check == multi_token_dpre_count_);
    assert(!local_.busy || !local_.ids.empty());
    for (const CurrentTask& c : clouds_) {
        assert(!c.busy || !c.ids.empty());
    }
}

#endif

}