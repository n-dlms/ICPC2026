#ifndef ICPC2026_POLICIES_HPP
#define ICPC2026_POLICIES_HPP

// Layer 5: optimization policies — the only layer that changes between candidates.
#include "protocol.hpp"
#include "state.hpp"
#include "task_table.hpp"

#include <vector>

namespace icpc2026 {

class CloudPolicy {
public:
    virtual ~CloudPolicy() = default;
    virtual int choose(const State& s, const Request& r, double t) const = 0;
};

class LeastLoadedCloud : public CloudPolicy {
public:
    int choose(const State& s, const Request& r, double t) const override;
};

class QueueAwareCloud : public CloudPolicy {
public:
    int choose(const State& s, const Request& r, double t) const override;
};

// 2.3: balance cumulative estimated decode load (State::decode_est) across
// clouds to minimize makespan (latest final token). Lin is the only proxy for
// Lout available at assign time; the estimate is corrected on FIN.
class DecodeLoadBalanceCloud : public CloudPolicy {
public:
    int choose(const State& s, const Request& r, double t) const override;
};

// PART 4.4: the irreversible P PRE cloud-assignment decision. Scores each cloud
//   effective_ready[k] = max(compute_free[k], up_queue_tail)
//   decode_load[k]     = estimated remaining decode work on k (State::decode_est)
//   batch_affinity[k]  = requests already on k that can decode-batch together
//   score[k] = effective_ready[k] + alpha*decode_load[k] - beta*batch_affinity[k]
//   assign = argmin(score). alpha/beta tunable (PPA_ALPHA/PPA_BETA).
class PpreCloudAssign : public CloudPolicy {
public:
    int choose(const State& s, const Request& r, double t) const override;
};

class RoundRobinCloud : public CloudPolicy {
public:
    explicit RoundRobinCloud(int k) : next_(0), k_(k) {}
    int choose(const State& s, const Request& r, double t) const override;
private:
    mutable int next_;
    int k_;
};

class ChunkPolicy {
public:
    virtual ~ChunkPolicy() = default;
    virtual std::pair<int,int> next_piece(const State& s, const Request& r, double t) const = 0;
};

class OnePieceChunk : public ChunkPolicy {
public:
    std::pair<int,int> next_piece(const State& s, const Request& r, double t) const override;
};

class AdaptiveChunk : public ChunkPolicy {
public:
    std::pair<int,int> next_piece(const State& s, const Request& r, double t) const override;
};

class DelayPolicy {
public:
    virtual ~DelayPolicy() = default;
    virtual bool hold_dpre(const State& s, double t) const = 0;
};

class NoDelay : public DelayPolicy {
public:
    bool hold_dpre(const State& s, double t) const override;
};

class SloAwareDelay : public DelayPolicy {
public:
    bool hold_dpre(const State& s, double t) const override;
    // PSGO Batch 1: alpha for dynamic hold behavior
    void set_alpha(double a) { alpha_ = a; }
private:
    double alpha_ = 1.0;
};

class BatchPolicy {
public:
    virtual ~BatchPolicy() = default;
    virtual int group_size(StepKind step, int n_ready, const State& s, double t) const = 0;
};

class SizeOneBatch : public BatchPolicy {
public:
    int group_size(StepKind step, int n_ready, const State& s, double t) const override;
};

class GreedyAllBatch : public BatchPolicy {
public:
    int group_size(StepKind step, int n_ready, const State& s, double t) const override;
};

class MarginalBatch : public BatchPolicy {
public:
    // Zero-Allocation Hot Path (Essay 3): the profit-maximizing batch size for a
    // decode step depends only on the (fixed) setup cost S, the (fixed) cross-cloud
    // UP latency, and the (immutable) table. So the optimal m is a per-step constant
    // precomputed once at startup; group_size is then an O(1) min(n_ready, m*).
    void precompute(const struct Config& cfg, const class TaskTimeTable& tbl);
    int  group_size(StepKind step, int n_ready, const State& s, double t) const override;
    // PSGO Batch 1: set alpha for dynamic batch sizing
    void set_alpha(double a) { alpha_ = a; }
    // PSGO Batch 3: expose precomputed max for the gradient engine.
    int max_batch_size(StepKind step) const {
        return max_m_[static_cast<int>(step_of(step))];
    }
private:
    int max_m_[6] = {1, 1, 1, 1, 1, 1};  // indexed by (int)Step; only decode cols used
    double alpha_ = 1.0;  // PSGO Batch 1: aggressiveness parameter
};

struct Policies {
    const CloudPolicy* cloud = nullptr;
    const ChunkPolicy* chunk = nullptr;
    const BatchPolicy* batch = nullptr;
    const DelayPolicy* delay = nullptr;
    // Runtime cross-remote D PRE combine control (PART 7 adaptive switch,
    // replaces compile-time V8_PRECOMBINE / V9_XREMOTE gating).
    bool combine_dpre = false;  // if true, D PRE may batch members across clouds
    int xremote_cap = 3;        // max distinct clouds in a combined group (high-latency bound)
    // PSGO Batch 1: continuous aggressiveness parameter alpha ∈ [0, 1]
    // alpha = 1.0 -> Pure Throughput (max batching, ignore SLOs)
    // alpha = 0.0 -> Pure Waiting (size-1 batches, protect SLOs strictly)
    double alpha = 1.0;
    // PSGO Batch 3: unscaled precomputed max_m_[D_PROC] (set at startup after
    // MarginalBatch::precompute). The gradient engine uses it for the init
    // (max_m*alpha), shrink (max_m*alpha*(1-P)), and growth ceiling (min(max_m,+1))
    // formulas; the initial scaled value is written straight into State.
    int psgo_max_m = 1;
};

}

#endif