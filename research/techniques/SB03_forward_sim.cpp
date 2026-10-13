// ============================================================================
// SB03_forward_sim.cpp — reference forward simulator (in-artifact judge replica)
// ICPC Online Challenge 2026, CF 2251A. SB03, 2026-08-23 (rev 2:
// audited + fixed + extended; see research/techniques/SB03_simulator_implementation.md).
//
// COMPACT EXACT FORWARD MODEL of the judge for per-decision rollout search
// inside src/submission.cpp (SB02 architecture):
//   - O(K) min-scan event loop, NO heap (SB02 §1.2)
//   - undo-journal rollback (SB02 §1.4): every mutation recorded WITH ITS WIDTH,
//     rollback(mark) restores bit-for-bit
//   - TIE_EPS = 1e-9 same-timestamp coalescing (SB02 §1.5)
//   - per-decision candidate rollout: mark -> apply(candidate) -> advance -> value
//
// Judge-order rules mirrored (statement = problem.md):
//   - transfer time          problem.md:208  (latency + 8*len*bpt/(bw*1e6))
//   - table lookup           problem.md:233  (exact | interp v1+(v2-v1)*(b-b1)/(b2-b1) | clamp)
//   - transfer enqueue time  problem.md:133  ("on completion ... queued immediately")
//       -> ALL transfers enqueue at the triggering TDN, in TDN (interactor
//          event) order = (time, push-seq); push-seq = assignment order
//          (problem.md:206 "Otherwise transfers enter in interactor event
//          order; tasks started together and finishing together use
//          assignment-line order").
//   - per-cloud UP split     problem.md:164  (increasing cloud index, len =
//          that cloud's member count, ids as a subsequence of the D PRE line)
//   - DOWN per D PROC group  problem.md:165-166 (len = group size)
//   - only last P PROC piece queues DOWN  problem.md:151-152
//   - piece duration         problem.md:149  ((le-ls)/num_layers * pproc(Lin))
//   - S paid once per task   problem.md:111-115
//   - frame coalescing       problem.md:242  (only exactly-equal internal
//          timestamps coalesce; TIE_EPS models the ulp-level indistinguishability)
//   - FIN beside final D POST TDN            problem.md:242
//   - UP entry order is ALSO assignment order a priori (single local computer
//          serializes P PRE/D PRE completions), but we enqueue at TDN time
//          anyway — judge-op-for-op, no extra cost.
//
// Build (test driver + judge-format trace), as one single line:
//   g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -DSB03_DRIVER -DSB03_TRACE
//       -o /tmp/sb03sim research/techniques/SB03_forward_sim.cpp
// The artifact core (everything outside #ifdef SB03_DRIVER / SB03_TRACE) is
// what gets sized for the 65,535-char submission; see
// research/techniques/SB03_simulator_implementation.md.
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace sb03 {

static const double TIE_EPS = 1e-9;  // SB02 §1.5 (9-decimal print resolution)
static const double INF = 1e300;

enum Step { P_PRE, P_PROC, P_POST, D_PRE, D_PROC, D_POST, N_STEP };
// 17 phases — same machine as src/state.hpp Phase.
enum Ph {
    R_PPRE, PPRE_R, W_IUP, R_PPROC, PPROC_R, W_IDN, R_PPOST, PPOST_R,
    R_DPRE, DPRE_R, W_DUP, R_DPROC, DPROC_R, W_DDN, R_DPOST, DPOST_R, DONE
};

struct ArrIn { double t; int lin, lhat; };   // lhat = determinized Lout (F0: true Lout)
struct HEv { double s; int k, a; };          // frame event: k 0 ARR, 1 TDN, 2 FIN
struct Xf { double f; int len, off, n, rc; bool pre; };   // FIFO entry; rc = remote

static bool has_dup(const std::vector<int>& v) {   // problem.md:379-380
    std::vector<int> c(v);
    std::sort(c.begin(), c.end());
    for (size_t i = 1; i < c.size(); ++i) if (c[i] == c[i - 1]) return true;
    return false;
}

// ----------------------------------------------------------------------------
// Sim — one hypothetical branch of the judge. Immutable-after-load: cfg, table,
// arrival stream. Everything else is journaled and rollback-able.
// ----------------------------------------------------------------------------
struct Sim {
    // ---- immutable config (in-artifact: shared with the solver's cfg/table) --
    int K = 1, layers = 1, R = 0;
    double S = 1, lat = 1, bw = 1;
    double bpt = 1;                    // bytes_per_token (<= 1e6)
    std::vector<ArrIn> arrs;           // arrival stream (F1 samples reslot here)
    struct Row { int b; double v; };
    std::vector<Row> tab[N_STEP];      // per column, sorted by batch, -1 skipped

    // ---- undo journal (SB02 §1.4) -------------------------------------------
    // Scalar writes -> entries carrying the EXACT width saved (restoring 8
    // bytes blindly would clobber the adjacent field — e.g. Rq::it after
    // Rq::ph — audit defect #1, fixed). Vector appends -> resize-to-old.
    bool jlive = true;                 // false = mutate without recording
    struct US { void* p; uint64_t o; unsigned char w; };
    struct UV { void* p; size_t n; char t; };    // t: 0=vector<int>, 1=vector<Xf>
    struct Mark { size_t s, v; };
    std::vector<US> js;
    std::vector<UV> jv;
    std::vector<HEv> evb;              // scratch: heap events of one frame
    Mark mark() const { return Mark{ js.size(), jv.size() }; }
    template <class T> void jw(T* p, T v) {
        static_assert(sizeof(T) <= 8, "journal word");
        if (jlive) {
            US e; e.p = (void*)p; e.w = (unsigned char)sizeof(T);
            memset(&e.o, 0, 8); memcpy(&e.o, p, sizeof(T));
            js.push_back(e);
        }
        *p = v;
    }
    void jpi(int x) {
        if (jlive) jv.push_back(UV{ (void*)&ar, ar.size(), 0 });
        ar.push_back(x);
    }
    void jpx(bool u, const Xf& x) {
        if (jlive) jv.push_back(UV{ u ? (void*)&up : (void*)&dn,
                                     u ? up.size() : dn.size(), 1 });
        (u ? up : dn).push_back(x);
    }
    void rollback(Mark m) {
        while (js.size() > m.s) {
            US& e = js.back(); memcpy(e.p, &e.o, e.w); js.pop_back();
        }
        while (jv.size() > m.v) {
            UV& e = jv.back();
            if (e.t == 0) ((std::vector<int>*)(e.p))->resize(e.n);
            else ((std::vector<Xf>*)(e.p))->resize(e.n);
            jv.pop_back();
        }
    }
    void jclear() { js.clear(); jv.clear(); }     // live mode only (no pending rollback)

    // ---- per-request minimal record (SB02 §1.1 table) ------------------------
    struct Rq {
        int ph = R_PPRE, it = 0, nls = 0, cl = -1, lin = 0, lhat = 1;
        double arr = 0, ftk = -1, ltk = -1;
        bool seen = false, fin = false;
    };
    std::vector<Rq> rq;

    // ---- resources + running tasks ------------------------------------------
    struct Task {
        bool busy = false;
        double f = 0, du = 0;          // TDN time = busy_until; dur (TDN echo)
        int st = P_PRE, seq = 0, fseq = -1, ls = 0, le = 0, off = 0, n = 0;
    };
    double lfree = 0.0;                // local busy_until
    double cfree[8] = {0,0,0,0,0,0,0,0};
    Task lt, ct[8];
    int seqc = 0;                      // global push sequence (heap tie order)
    int narr = 0;                      // arrivals injected so far
    double now = 0.0;
    long long nev = 0;                 // simulated-event counter (rollout H cap;
                                       // JOURNALED so rollback restores it exactly)
    long long nevt = 0;                // cumulative total (never rolled back)

    // ---- transfer FIFOs: append-only + head index (pop = head++ journaled) --
    std::vector<Xf> up, dn;
    int uph = 0, dnh = 0;              // entries before head are delivered
    std::vector<int> ar;               // id arena (append-only inside a rollout)

    // ---- oracle accumulators (terminal-evaluator inputs, SB01 §4.1) ---------
    double sum_tdr = 0, n_tdr = 0, sum_gap = 0, sum_span = 0;
    double tok = 0, last_fin = 0, first_arr = INF;
    long long n_gap = 0;
    int done = 0;

    // ---- basic cost model: verbatim judge formulas --------------------------
    // transfer time: problem.md:208.
    double xfer(double len) const { return lat + 8.0 * len * bpt / (bw * 1e6); }
    // table lookup: problem.md:233. Clamp below/above; linear interp with the
    // multiply-first expression order v1 + (v2-v1)*(b-b1)/(b2-b1) (SB02 §1.5
    // documents the ulp-level interp-order question; both orders are licensed
    // by problem.md:290 "ordinary double-precision arithmetic ... lookup").
    double dur(int c, int b) const {
        const std::vector<Row>& v = tab[c];
        if (b <= v.front().b) return v.front().v;
        if (b >= v.back().b) return v.back().v;
        size_t i = (size_t)(std::upper_bound(v.begin(), v.end(), b,
                        [](int x, const Row& r) { return x < r.b; }) - v.begin()) - 1;
        if (v[i].b == b) return v[i].v;
        const double b1 = v[i].b, b2 = v[i + 1].b;
        return v[i].v + (v[i + 1].v - v[i].v) * (b - b1) / (b2 - b1);
    }

    // ---- pending-cause min-scan: <= K+4 timestamps, O(K), no heap (SB02 1.2)
    double next_event_time() const {
        double b = INF;
        if (lt.busy && lt.f < b) b = lt.f;
        for (int k = 0; k < K; ++k) if (ct[k].busy && ct[k].f < b) b = ct[k].f;
        if (uph < (int)up.size() && up[uph].f < b) b = up[uph].f;
        if (dnh < (int)dn.size() && dn[dnh].f < b) b = dn[dnh].f;
        if (narr < R && arrs[narr].t < b) b = arrs[narr].t;
        return b;
    }
    // Judge main-loop condition: done < R or a pending cause exists (unfinished
    // requests always have an in-flight task/transfer; FIFO backlog alone with
    // done == R is impossible — FIN rides the final D POST TDN, after which no
    // transfer of that request can be pending).
    bool active() const {
        if (done < R || narr < R || lt.busy) return true;
        for (int k = 0; k < K; ++k) if (ct[k].busy) return true;
        return false;
    }

    // ---- FIFO enqueue --------------------------------------------------------
    // finish = max(enter, TAIL_LAST_FINISH) + transfer(len). The tail is the
    // LAST APPENDED entry: entries are appended exactly at their triggering
    // TDN in (time, push-seq) order, so the vector order IS the FIFO order and
    // back() is the tail (problem.md:57/206).
    // ids may point into `ar` ONLY via the _a variant (no appends => no realloc).
    void enq_up(double enter, int len, bool pre, int rc, const int* ids, int n) {
        Xf x; x.len = len; x.pre = pre; x.n = n; x.off = (int)ar.size(); x.rc = rc;
        for (int i = 0; i < n; ++i) jpi(ids[i]);
        x.f = std::max(enter, up.empty() ? 0.0 : up.back().f) + xfer(len);
        jpx(true, x);
    }
    void enq_dn(double enter, int len, bool pre, int rc, const int* ids, int n) {
        Xf x; x.len = len; x.pre = pre; x.n = n; x.off = (int)ar.size(); x.rc = rc;
        for (int i = 0; i < n; ++i) jpi(ids[i]);
        x.f = std::max(enter, dn.empty() ? 0.0 : dn.back().f) + xfer(len);
        jpx(false, x);
    }
    // arena-offset variant: the ids are already stored in ar[off..off+n)
    // (the triggering task's member slice) — no appends, no realloc hazard.
    void enq_up_a(double enter, int len, bool pre, int rc, int off, int n) {
        Xf x; x.f = std::max(enter, up.empty() ? 0.0 : up.back().f) + xfer(len);
        x.len = len; x.pre = pre; x.n = n; x.off = off; x.rc = rc;
        jpx(true, x);
    }
    void enq_dn_a(double enter, int len, bool pre, int rc, int off, int n) {
        Xf x; x.f = std::max(enter, dn.empty() ? 0.0 : dn.back().f) + xfer(len);
        x.len = len; x.pre = pre; x.n = n; x.off = off; x.rc = rc;
        jpx(false, x);
    }

    // ---- delivery effect -----------------------------------------------------
    void xdn(bool isup) {
        const Xf x = isup ? up[uph] : dn[dnh];     // copy: q untouched by journal
        for (int i = 0; i < x.n; ++i) {
            Rq& r = rq[ar[x.off + i]];
            if (r.fin) continue;                   // finished ids never carried
            jw(&r.ph, (int)(x.pre ? (isup ? R_PPROC : R_PPOST)
                                  : (isup ? R_DPROC : R_DPOST)));
        }
        jw(isup ? &uph : &dnh, (isup ? uph : dnh) + 1);   // pop head
        jw(&nev, nev + 1); ++nevt;
#ifdef SB03_TRACE
        tr_xdn(isup, x);
#endif
    }

    // ---- TDN effect: state flips + TRANSFER ENQUEUES (at TDN time) -----------
    // problem.md:133: "on completion, the ... transfer is queued immediately".
    // Entry order inside one frame = (time, push-seq) = assignment order
    // (problem.md:206). Within one D PRE: increasing cloud index (problem.md:164).
    void tdn_effect(Task& T) {
        if (T.st == P_PRE) {
            const int rid = ar[T.off];
            Rq& r = rq[rid];
            jw(&r.ph, (int)W_IUP);
            enq_up_a(T.f, r.lin, true, r.cl, T.off, 1);      // len = Lin
        } else if (T.st == P_PROC) {
            const int rid = ar[T.off];
            Rq& r = rq[rid];
            jw(&r.nls, T.le);
            jw(&r.ph, (int)(T.le == layers ? W_IDN : R_PPROC));
            if (T.le == layers)                              // ONLY the last piece
                enq_dn_a(T.f, r.lin, true, r.cl, T.off, 1);  // len = Lin (pm:151)
        } else if (T.st == P_POST) {
            jw(&rq[ar[T.off]].ph, (int)R_DPRE);    // tdr booked at apply time
        } else if (T.st == D_PRE) {
            for (int i = 0; i < T.n; ++i)
                jw(&rq[ar[T.off + i]].ph, (int)W_DUP);
            // one UP per distinct cloud, increasing cloud index, len = that
            // cloud's member count, ids = subsequence of the D PRE line (pm:164).
            // Copy members to a stack buffer FIRST: enq_up appends to `ar`,
            // which may reallocate and dangle an arena pointer.
            static int byc[8][2048];
            int nc[8] = {0,0,0,0,0,0,0,0};
            for (int i = 0; i < T.n; ++i) {
                const int c = rq[ar[T.off + i]].cl;
                byc[c][nc[c]++] = ar[T.off + i];
            }
            for (int c = 0; c < K; ++c)
                if (nc[c] > 0) enq_up(T.f, nc[c], false, c, byc[c], nc[c]);
        } else if (T.st == D_PROC) {
            for (int i = 0; i < T.n; ++i)
                jw(&rq[ar[T.off + i]].ph, (int)W_DDN);
            enq_dn_a(T.f, T.n, false, rq[ar[T.off]].cl, T.off, T.n); // len = m (pm:165)
        }                                          // D_POST: bookkeeping at apply
        jw(&T.busy, false);
    }

    // ---- arrival injection (F0/F1/F2 determinization entry point) -----------
    void inject_arrival(int rid, int lin, double t) {
        Rq& r = rq[rid];
        jw(&r.seen, true); jw(&r.lin, lin); jw(&r.arr, t); jw(&r.ph, (int)R_PPRE);
        jw(&r.lhat, arrs[rid].lhat);   // determinized Lout (F1 resamples here)
        jw(&first_arr, first_arr < t ? first_arr : t);
        jw(&narr, narr + 1);
    }

    // ---- apply ALL pending causes within TIE_EPS of tau, in judge order -----
    // Judge order inside one frame (line order carries NO scheduling priority,
    // problem.md:242; mirrored only so the trace is deterministic):
    //   1. FIFO head deliveries (UP then DOWN);
    //   2. heap events in (time, seq) push order; seq = ARR list index
    //      (< R) < TDN/FIN assignment push order; a D POST pushes its FINs
    //      BEFORE its TDN so their push-seq is lower.
    void deliver(double tau) {
        jw(&now, tau);
        while (uph < (int)up.size() && std::fabs(up[uph].f - tau) < TIE_EPS) xdn(true);
        while (dnh < (int)dn.size() && std::fabs(dn[dnh].f - tau) < TIE_EPS) xdn(false);
        evb.clear();
        for (int a = narr; a < R && std::fabs(arrs[a].t - tau) < TIE_EPS; ++a)
            evb.push_back(HEv{ (double)a, 0, a });     // ARR seq = list index
        if (lt.busy && std::fabs(lt.f - tau) < TIE_EPS) {
            evb.push_back(HEv{ (double)lt.seq, 1, -1 });
            if (lt.st == D_POST)                       // FINs ride AFTER the TDN
                for (int i = 0; i < lt.n; ++i)
                    if (rq[ar[lt.off + i]].fin)
                        evb.push_back(HEv{ lt.fseq + (double)i, 2, ar[lt.off + i] });
        }
        for (int k = 0; k < K; ++k) {
            if (ct[k].busy && std::fabs(ct[k].f - tau) < TIE_EPS) {
                evb.push_back(HEv{ (double)ct[k].seq, 1, k });
                if (ct[k].st == D_POST)
                    for (int i = 0; i < ct[k].n; ++i)
                        if (rq[ar[ct[k].off + i]].fin)
                            evb.push_back(HEv{ ct[k].fseq + (double)i, 2,
                                               ar[ct[k].off + i] });
            }
        }
        std::sort(evb.begin(), evb.end(),
                  [](const HEv& a, const HEv& b) { return a.s < b.s; });
        jw(&nev, nev + (long long)evb.size()); nevt += (long long)evb.size();
        for (const HEv& e : evb) {
            if (e.k == 0) {
#ifdef SB03_TRACE
                tr_arr(e.a, arrs[e.a].lin);
#endif
                inject_arrival(e.a, arrs[e.a].lin, arrs[e.a].t);
            } else if (e.k == 1) {
                Task& T = e.a < 0 ? lt : ct[e.a];
#ifdef SB03_TRACE
                tr_tdn(T);
#endif
                tdn_effect(T);
            } else {
#ifdef SB03_TRACE
                tr_fin(e.a);          // FIN effects were booked at D POST apply
#endif
            }
        }
    }

    // ---- advance the model to T (every event <= T + TIE_EPS applied) --------
    // Live mode: the caller advances only to OBSERVED frame times (re-anchor,
    // SB02 §1.5(4)). Rollout mode: advance_to is the hypothetical clock. F0
    // stops at the last injected arrival by construction (none pending).
    void advance_to(double T) {
        for (;;) {
            const double tau = next_event_time();
            if (!(tau < T + TIE_EPS)) break;          // coalesce within TIE_EPS
            deliver(tau);
        }
        jw(&now, now > T ? now : T);
    }

    // ---- apply one assignment at t (legality mirror of the judge) -----------
    struct Act { int srv = -1, st = P_PRE, ls = 0, le = 0, cl = 0; std::vector<int> ids; };

    const char* apply(const Act& a, double t) {
        if (a.st == P_PRE) {
            if (a.srv != -1) return "P PRE server must be E";
            if ((int)a.ids.size() != 1) return "P PRE arity";
            const int rid = a.ids[0];
            if (rid < 0 || rid >= R || !rq[rid].seen) return "rid not arrived";
            if (a.cl < 0 || a.cl >= K) return "P PRE remote out of range";
            Rq& r = rq[rid];
            if (r.ph != R_PPRE || r.fin) return "P PRE wrong phase";
            if (t < lfree - TIE_EPS) return "local busy";
            const double d = dur(P_PRE, r.lin);
            const double f = t + S + d;
            const int off = (int)ar.size(); jpi(rid);
            jw(&lfree, f);
            jw(&lt.busy, true); jw(&lt.f, f); jw(&lt.du, d); jw(&lt.st, (int)P_PRE);
            jw(&lt.off, off); jw(&lt.n, 1); jw(&lt.seq, seqc); jw(&seqc, seqc + 1);
            jw(&lt.fseq, -1);
            jw(&r.cl, a.cl); jw(&r.ph, (int)PPRE_R);
            // UP is enqueued at the TDN (tdn_effect), entry order = (time,seq).
            jw(&nev, nev + 1); ++nevt;
            return nullptr;
        }
        if (a.st == P_PROC) {
            const int cl = a.srv;
            if (cl < 0 || cl >= K) return "cloud range";
            if (cl != a.cl) return "P PROC server != remote";
            if ((int)a.ids.size() != 1) return "P PROC arity";
            const int rid = a.ids[0];
            if (rid < 0 || rid >= R || !rq[rid].seen) return "rid not arrived";
            Rq& r = rq[rid];
            if (r.cl != cl) return "P PROC remote != assigned";
            if (t < cfree[cl] - TIE_EPS) return "cloud busy";
            if (r.ph != R_PPROC) return "P PROC wrong phase";
            if (a.ls != r.nls || a.le <= a.ls || a.le > layers) return "illegal piece";
            const double d = (a.le - a.ls) / (double)layers * dur(P_PROC, r.lin);
            const double f = t + S + d;
            const int off = (int)ar.size(); jpi(rid);
            Task& T = ct[cl];
            jw(&cfree[cl], f);
            jw(&T.busy, true); jw(&T.f, f); jw(&T.du, d); jw(&T.st, (int)P_PROC);
            jw(&T.ls, a.ls); jw(&T.le, a.le); jw(&T.off, off); jw(&T.n, 1);
            jw(&T.seq, seqc); jw(&seqc, seqc + 1); jw(&T.fseq, -1);
            jw(&r.nls, a.le);
            jw(&r.ph, (int)(a.le == layers ? W_IDN : R_PPROC));
            // DOWN (last piece only) enqueued at the TDN (tdn_effect).
            jw(&nev, nev + 1); ++nevt;
            return nullptr;
        }
        if (a.st == P_POST) {
            if (a.srv != -1) return "P POST server must be E";
            if ((int)a.ids.size() != 1) return "P POST arity";
            const int rid = a.ids[0];
            if (rid < 0 || rid >= R || !rq[rid].seen) return "rid not arrived";
            Rq& r = rq[rid];
            if (r.cl != a.cl) return "P POST remote != assigned";
            if (t < lfree - TIE_EPS) return "local busy";
            if (r.ph != R_PPOST) return "P POST wrong phase";
            const double d = dur(P_POST, r.lin);
            const double f = t + S + d;
            const int off = (int)ar.size(); jpi(rid);
            jw(&lfree, f);
            jw(&lt.busy, true); jw(&lt.f, f); jw(&lt.du, d); jw(&lt.st, (int)P_POST);
            jw(&lt.off, off); jw(&lt.n, 1); jw(&lt.seq, seqc); jw(&seqc, seqc + 1);
            jw(&lt.fseq, -1);
            jw(&r.ph, (int)R_DPRE);
            // TDR booked at ASSIGNMENT time with the (deterministic) completion
            // f — no later event can change it (non-preemptive).
            jw(&sum_tdr, sum_tdr + f - r.arr); jw(&n_tdr, n_tdr + 1);
            jw(&nev, nev + 1); ++nevt;
            return nullptr;
        }
        if (a.st == D_PRE) {
            if (a.srv != -1) return "D PRE server must be E";
            const int m = (int)a.ids.size();
            if (m < 1) return "malformed group";
            if (has_dup(a.ids)) return "dup id";       // problem.md:379-380
            for (int rid : a.ids) {
                if (rid < 0 || rid >= R || !rq[rid].seen) return "rid not arrived";
                if (rq[rid].ph != R_DPRE || rq[rid].fin) return "D PRE wrong phase";
            }
            if (t < lfree - TIE_EPS) return "local busy";
            const double d = dur(D_PRE, m);
            const double f = t + S + d;
            const int off = (int)ar.size();
            for (int rid : a.ids) jpi(rid);
            jw(&lfree, f);
            jw(&lt.busy, true); jw(&lt.f, f); jw(&lt.du, d); jw(&lt.st, (int)D_PRE);
            jw(&lt.off, off); jw(&lt.n, m); jw(&lt.seq, seqc); jw(&seqc, seqc + 1);
            jw(&lt.fseq, -1);
            for (int rid : a.ids) jw(&rq[rid].ph, (int)DPRE_R);
            // Per-cloud UPs enqueued at the TDN (tdn_effect), increasing index.
            jw(&nev, nev + 1); ++nevt;
            return nullptr;
        }
        if (a.st == D_PROC) {
            const int cl = a.srv;
            if (cl < 0 || cl >= K) return "cloud range";
            if (cl != a.cl) return "D PROC server != remote";
            const int m = (int)a.ids.size();
            if (m < 1) return "malformed group";
            if (has_dup(a.ids)) return "dup id";       // problem.md:379-380
            for (int rid : a.ids) {
                if (rid < 0 || rid >= R || !rq[rid].seen) return "rid not arrived";
                if (rq[rid].ph != R_DPROC || rq[rid].fin) return "D PROC wrong phase";
                if (rq[rid].cl != cl) return "D PROC member wrong cloud";
            }
            if (t < cfree[cl] - TIE_EPS) return "cloud busy";
            const double d = dur(D_PROC, m);
            const double f = t + S + d;
            const int off = (int)ar.size();
            for (int rid : a.ids) jpi(rid);
            Task& T = ct[cl];
            jw(&cfree[cl], f);
            jw(&T.busy, true); jw(&T.f, f); jw(&T.du, d); jw(&T.st, (int)D_PROC);
            jw(&T.off, off); jw(&T.n, m); jw(&T.seq, seqc); jw(&seqc, seqc + 1);
            jw(&T.fseq, -1);
            for (int rid : a.ids) jw(&rq[rid].ph, (int)W_DDN);
            // DOWN (len = m) enqueued at the TDN (tdn_effect).
            jw(&nev, nev + 1); ++nevt;
            return nullptr;
        }
        if (a.st == D_POST) {
            if (a.srv != -1) return "D POST server must be E";
            const int m = (int)a.ids.size();
            if (m < 1) return "malformed group";
            if (has_dup(a.ids)) return "dup id";       // problem.md:379-380
            for (int rid : a.ids) {
                if (rid < 0 || rid >= R || !rq[rid].seen) return "rid not arrived";
                if (rq[rid].ph != R_DPOST || rq[rid].fin) return "D POST wrong phase";
            }
            if (t < lfree - TIE_EPS) return "local busy";
            const double d = dur(D_POST, m);
            const double f = t + S + d;
            const int off = (int)ar.size();
            for (int rid : a.ids) jpi(rid);
            jw(&lfree, f);
            jw(&lt.busy, true); jw(&lt.f, f); jw(&lt.du, d); jw(&lt.st, (int)D_POST);
            jw(&lt.off, off); jw(&lt.n, m);
            // Token bookkeeping at ASSIGNMENT time, token time = f (determin-
            // istic completion). TDN first, then its FINs — the statement
            // examples print that order (problem.md:535-537/653-656); within-
            // frame order carries no priority (problem.md:242) either way.
            int nfin = 0;
            for (int rid : a.ids) {
                Rq& r = rq[rid];
                jw(&r.it, r.it + 1);
                if (r.ftk < 0) jw(&r.ftk, f);
                if (r.it > 1) { jw(&sum_gap, sum_gap + f - r.ltk); jw(&n_gap, n_gap + 1); }
                jw(&r.ltk, f);
                if (r.it >= r.lhat) {                 // FIN at iter == Lout_hat
                    jw(&r.fin, true);
                    jw(&sum_span, sum_span + r.ltk - r.ftk);
                    jw(&tok, tok + r.it);
                    jw(&last_fin, last_fin > f ? last_fin : f);
                    jw(&done, done + 1);
                    jw(&r.ph, (int)DONE);
                    ++nfin;
                } else {
                    jw(&r.ph, (int)R_DPRE);           // ready for next iteration
                }
            }
            jw(&lt.seq, seqc); jw(&seqc, seqc + 1);       // TDN seq first
            jw(&lt.fseq, nfin > 0 ? seqc : -1);             // FINs after the TDN
            if (nfin > 0) jw(&seqc, seqc + nfin);
            jw(&nev, nev + 1); ++nevt;
            return nullptr;
        }
        return "unknown step";
    }

    // ---- terminal value: exact score (problem.md:425-495) --------------------
    double sc_slo1 = 1, sc_slo2 = 1, sc_ub = 2, sc_b = 0, sc_db = 1, sc_wt = 0, sc_wc = 0;
    double score() const {
        const double tp = last_fin > first_arr ? tok / (last_fin - first_arr) : 0.0;
        const double ctp = std::max(0.0, std::min(1.0, (tp - sc_b) / (sc_ub - sc_b)));
        const double mtdr = sum_tdr / R;
        const double mtpot = n_gap > 0 ? sum_span / (double)n_gap : 0.0;
        const double e1 = std::max(0.0, (mtdr - sc_slo1) / sc_slo1);
        const double e2 = std::max(0.0, (mtpot - sc_slo2) / sc_slo2);
        const double dist = std::sqrt(e1 * e1 + e2 * e2);
        const double cw = sc_db > 0.0 ? std::max(0.0, 1.0 - dist / sc_db)
                                      : (dist == 0.0 ? 1.0 : 0.0);
        return 1000.0 * (sc_wt * ctp + sc_wc * cw);
    }

#ifdef SB03_TRACE
    // ---- test-only judge-format trace (compiled out of the artifact) --------
    struct Tr { double t; std::string s; };
    std::vector<Tr> tr;
    void tr_xdn(bool isup, const Xf& x) {
        char b[16384]; int p = 0;
        p += snprintf(b + p, sizeof(b) - p, "XDN %s %d %lld %s %d",
                      isup ? "UP" : "DOWN", x.rc,
                      (long long)x.len * (long long)bpt, x.pre ? "PRE" : "DEC", x.n);
        for (int i = 0; i < x.n; ++i)
            p += snprintf(b + p, sizeof(b) - p, " %d", ar[x.off + i]);
        tr.push_back(Tr{ now, b });
    }
    void tr_arr(int rid, int lin) {
        char b[64]; snprintf(b, sizeof(b), "ARR %d %d", rid, lin);
        tr.push_back(Tr{ now, b });
    }
    void tr_fin(int rid) {
        char b[32]; snprintf(b, sizeof(b), "FIN %d", rid);
        tr.push_back(Tr{ now, b });
    }
    void tr_tdn(const Task& T) {
        char b[16384]; int p = 0;
        const int rid0 = ar[T.off];
        if (T.st == P_PROC || T.st == D_PROC)
            p += snprintf(b + p, sizeof(b) - p, "TDN C%d", rq[rid0].cl);
        else
            p += snprintf(b + p, sizeof(b) - p, "TDN E");
        if (T.st == P_PRE)  p += snprintf(b + p, sizeof(b) - p, " P PRE %d %d", rq[rid0].cl, rid0);
        if (T.st == P_PROC) p += snprintf(b + p, sizeof(b) - p, " P PROC %d %d %d %d", T.ls, T.le, rq[rid0].cl, rid0);
        if (T.st == P_POST) p += snprintf(b + p, sizeof(b) - p, " P POST %d %d", rq[rid0].cl, rid0);
        if (T.st == D_PRE)  p += snprintf(b + p, sizeof(b) - p, " D PRE -1 %d", T.n);
        if (T.st == D_PROC) p += snprintf(b + p, sizeof(b) - p, " D PROC %d %d", rq[rid0].cl, T.n);
        if (T.st == D_POST) p += snprintf(b + p, sizeof(b) - p, " D POST -1 %d", T.n);
        if (T.st == D_PRE || T.st == D_PROC || T.st == D_POST)   // group ids
            for (int i = 0; i < T.n; ++i)
                p += snprintf(b + p, sizeof(b) - p, " %d", ar[T.off + i]);
        snprintf(b + p, sizeof(b) - p, " %.9f", T.du);
        tr.push_back(Tr{ now, b });
    }
#endif
};

}  // namespace sb03

// ============================================================================
// Test driver (reference only — the artifact sizer strips everything below).
// ============================================================================
#ifdef SB03_DRIVER
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/resource.h>

using namespace sb03;
using clk = std::chrono::steady_clock;
static double us_since(clk::time_point a, clk::time_point b) {
    return std::chrono::duration<double, std::micro>(b - a).count();
}
static long rss_kb() {
    struct rusage ru; getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;   // KiB on Linux
}

static std::string fmt9(double t) { char b[64]; snprintf(b, sizeof(b), "%.9f", t); return b; }

// ---- case loading (test-case format: cfg line, scoring line, N rows, R arr) -
static bool load_case(const char* path, Sim& s, std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "open case"; return false; }
    std::vector<std::string> tk;
    std::string ln;
    while (std::getline(f, ln)) {
        size_t h = ln.find('#'); if (h != std::string::npos) ln = ln.substr(0, h);
        std::istringstream ss(ln); std::string w;
        while (ss >> w) tk.push_back(w);
    }
    size_t i = 0; auto nx = [&](double& v) { v = atof(tk[i++].c_str()); };
    auto ni = [&](int& v) { v = atoi(tk[i++].c_str()); };
    ni(s.K); nx(s.S); nx(s.lat); nx(s.bw); nx(s.bpt); ni(s.layers);
    nx(s.sc_slo1); nx(s.sc_slo2); nx(s.sc_ub); nx(s.sc_b); nx(s.sc_db);
    nx(s.sc_wt); nx(s.sc_wc);
    int N; ni(N);
    for (int r = 0; r < N; ++r) {
        int b; double v[6];
        ni(b); for (int c = 0; c < 6; ++c) nx(v[c]);
        for (int c = 0; c < 6; ++c)
            if (v[c] >= 0) s.tab[c].push_back(Sim::Row{ b, v[c] });
    }
    ni(s.R);
    s.arrs.resize(s.R);
    for (int r = 0; r < s.R; ++r) { nx(s.arrs[r].t); ni(s.arrs[r].lin); ni(s.arrs[r].lhat); }
    for (int c = 0; c < 6; ++c) {
        std::sort(s.tab[c].begin(), s.tab[c].end(),
                  [](const Sim::Row& a, const Sim::Row& b) { return a.b < b.b; });
        if (s.tab[c].empty()) { err = "empty column"; return false; }
    }
    s.rq.resize(s.R);
    s.seqc = s.R;        // judge pre-pushes all ARRs: they own seq 0..R-1
    s.js.reserve(1 << 16); s.jv.reserve(1 << 14);
    s.ar.reserve(1 << 18);
    return true;
}

// ---- action text <-> Act ----------------------------------------------------
static bool parse_act(const std::string& l, Sim::Act& a, std::string& err) {
    std::istringstream ss(l); std::string w;
    if (!(ss >> w)) { err = "empty line"; return false; }
    if (w == "E") a.srv = -1;
    else if (w[0] == 'C' && w.size() > 1 && isdigit((unsigned char)w[1]))
        a.srv = atoi(w.c_str() + 1);
    else { err = "bad server"; return false; }
    std::string s1, s2;
    if (!(ss >> s1 >> s2)) { err = "bad step"; return false; }
    const std::string st = s1 + " " + s2;
    if (st == "P PRE")  { a.st = P_PRE;  ss >> a.cl; int r; ss >> r; a.ids = { r }; }
    else if (st == "P PROC") { a.st = P_PROC; ss >> a.ls >> a.le >> a.cl; int r; ss >> r; a.ids = { r }; }
    else if (st == "P POST") { a.st = P_POST; ss >> a.cl; int r; ss >> r; a.ids = { r }; }
    else if (st == "D PRE" || st == "D POST") {
        a.st = (st == "D PRE") ? D_PRE : D_POST; a.cl = -1;
        int m; std::string dash; ss >> dash >> m;
        for (int k = 0; k < m; ++k) { int r; ss >> r; a.ids.push_back(r); }
    }
    else if (st == "D PROC") {
        a.st = D_PROC; ss >> a.cl; int m; ss >> m;
        for (int k = 0; k < m; ++k) { int r; ss >> r; a.ids.push_back(r); }
    }
    else { err = "unknown step"; return false; }
    return true;
}

static std::string act_str(const Sim::Act& a) {
    std::ostringstream o;
    if (a.srv < 0) o << "E"; else o << "C" << a.srv;
    const int r0 = a.ids.empty() ? 0 : a.ids[0];
    switch (a.st) {
        case P_PRE:  o << " P PRE " << a.cl << " " << r0; break;
        case P_PROC: o << " P PROC " << a.ls << " " << a.le << " " << a.cl << " " << r0; break;
        case P_POST: o << " P POST " << a.cl << " " << r0; break;
        case D_PRE:  o << " D PRE -1 " << a.ids.size(); for (int r : a.ids) o << " " << r; break;
        case D_PROC: o << " D PROC " << a.cl << " " << a.ids.size(); for (int r : a.ids) o << " " << r; break;
        case D_POST: o << " D POST -1 " << a.ids.size(); for (int r : a.ids) o << " " << r; break;
        default: break;
    }
    return o.str();
}

// ---- greedy base policy (test generator: legal, work-conserving, never stuck)
// split: 0 = group ALL ready decode members; k>0 = cap decode group size at k
// (split=1 is the max-frame degenerate for the stress run).
// chunk: 0/1 = full-piece prefills; k>1 = split big prefills into ~k pieces.
static std::vector<Sim::Act> greedy(Sim& s, int split = 0, int chunk = 0) {
    std::vector<Sim::Act> out;
    if (!s.lt.busy) {
        int id = -1;
        for (int r = 0; r < s.R && id < 0; ++r)
            if (s.rq[r].seen && !s.rq[r].fin && s.rq[r].ph == R_PPOST) id = r;
        if (id < 0)
            for (int r = 0; r < s.R && id < 0; ++r)
                if (s.rq[r].seen && !s.rq[r].fin && s.rq[r].ph == R_PPRE) id = r;
        if (id >= 0) {
            Sim::Act a;
            a.st = (s.rq[id].ph == R_PPOST) ? P_POST : P_PRE;
            a.cl = (a.st == P_POST) ? s.rq[id].cl : (id % s.K);
            a.srv = -1; a.ids = { id };
            out.push_back(a);
        } else {
            Sim::Act a; a.srv = -1; a.st = D_PRE;      // D PRE: ready by rid
            for (int r = 0; r < s.R; ++r)
                if (s.rq[r].seen && !s.rq[r].fin && s.rq[r].ph == R_DPRE) a.ids.push_back(r);
            if (split > 0 && (int)a.ids.size() > split) a.ids.resize(split);
            if (!a.ids.empty()) out.push_back(a);
            else {
                Sim::Act b; b.srv = -1; b.st = D_POST; // D POST: ready by rid
                for (int r = 0; r < s.R; ++r)
                    if (s.rq[r].seen && !s.rq[r].fin && s.rq[r].ph == R_DPOST) b.ids.push_back(r);
                if (split > 0 && (int)b.ids.size() > split) b.ids.resize(split);
                if (!b.ids.empty()) out.push_back(b);
            }
        }
    }
    for (int k = 0; k < s.K; ++k) {
        if (s.ct[k].busy) continue;
        int id = -1;
        for (int r = 0; r < s.R && id < 0; ++r)
            if (s.rq[r].seen && !s.rq[r].fin && s.rq[r].ph == R_PPROC && s.rq[r].cl == k) id = r;
        if (id >= 0) {
            Sim::Act a; a.st = P_PROC; a.srv = k; a.cl = k; a.ids = { id };
            a.ls = s.rq[id].nls; a.le = s.layers;
            if (chunk > 1 && s.rq[id].nls == 0 && s.layers >= chunk && s.rq[id].lin >= 512) {
                a.le = a.ls + s.layers / chunk;
                if (a.le >= s.layers - (s.layers / chunk) / 2) a.le = s.layers;
            }
            out.push_back(a);
        } else {
            Sim::Act a; a.st = D_PROC; a.srv = k; a.cl = k;
            for (int r = 0; r < s.R; ++r)
                if (s.rq[r].seen && !s.rq[r].fin && s.rq[r].ph == R_DPROC && s.rq[r].cl == k)
                    a.ids.push_back(r);
            if (split > 0 && (int)a.ids.size() > split) a.ids.resize(split);
            if (!a.ids.empty()) out.push_back(a);
        }
    }
    return out;
}

// ---- frame loop: advance to next event time, log frame, apply response -----
struct RunStat { long long frames = 0, events = 0; };
static bool run_frames(Sim& s, const std::vector<std::vector<std::string>>& script,
                       bool gen, std::ostream* out, RunStat& st, std::string& err,
                       bool keep_trace = false) {
    (void)keep_trace;    // only consumed under SB03_TRACE
    size_t fi = 0;
    while (s.active()) {
        const double t = s.next_event_time();
        if (t >= INF) { err = "stuck: ready work but no pending cause"; return false; }
#ifdef SB03_TRACE
        if (!keep_trace) s.tr.clear();
#endif
        s.advance_to(t);
        if (out) {
#ifdef SB03_TRACE
            *out << fmt9(t) << "\n" << s.tr.size() << "\n";
            for (const auto& e : s.tr) *out << e.s << "\n";
#endif
        }
        st.frames++;
        st.events = s.nevt;
        std::vector<std::string> resp;
        if (fi < script.size()) resp = script[fi];
        else if (gen) { for (const auto& a : greedy(s)) resp.push_back(act_str(a)); }
        for (const auto& ln : resp) {
            if (ln == "EMPTY") continue;
            Sim::Act a;
            if (!parse_act(ln, a, err)) { err = "parse: " + ln + ": " + err; return false; }
            const char* e = s.apply(a, t);
            if (e) { err = std::string(e) + " @ t=" + fmt9(t) + " line=" + ln; return false; }
        }
        ++fi;
        s.jclear();
    }
    return true;
}

// ---- script file IO (blocks separated by blank lines; EMPTY = 0 response) --
static bool load_script(const char* path, std::vector<std::vector<std::string>>& sc) {
    std::ifstream f(path);
    if (!f) return false;
    std::string ln; std::vector<std::string> cur;
    while (std::getline(f, ln)) {
        if (!ln.empty() && ln[0] == ';') continue;
        if (ln.find_first_not_of(" \t\r") == std::string::npos) {
            if (!cur.empty()) { sc.push_back(cur); cur.clear(); }
        } else cur.push_back(ln);
    }
    if (!cur.empty()) sc.push_back(cur);
    return true;
}
static void save_script(const char* path, const std::vector<std::vector<std::string>>& sc) {
    std::ofstream f(path);
    for (size_t i = 0; i < sc.size(); ++i) {
        f << "; frame " << i << "\n";
        if (sc[i].empty()) f << "EMPTY\n";
        else for (const auto& l : sc[i]) f << l << "\n";
        f << "\n";
    }
}

// ---- transcript comparison: group consecutive events by identical t --------
static bool check_transcript(const Sim& s, const std::vector<std::string>& want,
                             std::string& why) {
    (void)s;             // trace entries only exist under SB03_TRACE
    std::vector<std::pair<std::string, std::string>> W, G;
    for (const auto& w : want) {
        const size_t p = w.find('|');
        W.push_back({ w.substr(0, p), w.substr(p + 1) });
    }
#ifdef SB03_TRACE
    for (const auto& e : s.tr) G.push_back({ fmt9(e.t), e.s });
#endif
    if (G.size() != W.size()) {
        why = "event count " + std::to_string(G.size()) + " != " + std::to_string(W.size());
        return false;
    }
    size_t i = 0, j = 0;
    while (i < G.size()) {
        size_t i2 = i; while (i2 < G.size() && G[i2].first == G[i].first) ++i2;
        size_t j2 = j; while (j2 < W.size() && W[j2].first == G[i].first) ++j2;
        if (j2 - j != i2 - i) {
            why = "frame " + G[i].first + " count " + std::to_string(i2 - i)
                + " != " + std::to_string(j2 - j);
            return false;
        }
        std::vector<std::string> A, B;
        for (size_t k = i; k < i2; ++k) A.push_back(G[k].second);
        for (size_t k = j; k < j2; ++k) B.push_back(W[k].second);
        std::sort(A.begin(), A.end()); std::sort(B.begin(), B.end());
        if (A != B) {
            why = "frame " + G[i].first + " events differ";
            for (size_t k = 0; k < A.size() && k < B.size(); ++k)
                if (A[k] != B[k]) { why += ": got '" + A[k] + "' want '" + B[k] + "'"; break; }
            return false;
        }
        i = i2; j = j2;
    }
    return true;
}

// ---- Example 1 (problem.md:569-620): one request end-to-end, 12 events -----
static int mode_ex1() {
    Sim s;
    s.K = 1; s.S = 1; s.lat = 2; s.bw = 1; s.bpt = 125000; s.layers = 4;
    s.R = 1; s.arrs = { { 0, 4, 1 } };
    s.tab[P_PRE].push_back({ 1, 3 });
    s.tab[P_PROC].push_back({ 1, 10 });
    s.tab[P_POST].push_back({ 1, 2 });
    s.tab[D_PRE].push_back({ 1, 1 });
    s.tab[D_PROC].push_back({ 1, 4 });
    s.tab[D_POST].push_back({ 1, 1 });
    s.rq.resize(1); s.seqc = 1;
    const std::vector<std::vector<std::string>> script = {
        { "E P PRE 0 0" },        // t=0    ARR
        { "EMPTY" },              // t=4    TDN P PRE
        { "C0 P PROC 0 4 0 0" },  // t=10   XDN UP (full piece [0,4))
        { "EMPTY" },              // t=21   TDN P PROC
        { "E P POST 0 0" },       // t=27   XDN DOWN
        { "E D PRE -1 1 0" },     // t=30   TDN P POST (TDR=30)
        { "EMPTY" },              // t=32   TDN D PRE
        { "C0 D PROC 0 1 0" },    // t=35   XDN UP DEC
        { "EMPTY" },              // t=40   TDN D PROC
        { "E D POST -1 1 0" },    // t=43   XDN DOWN DEC
        { "EMPTY" },              // t=45   TDN D POST + FIN 0
    };
    const std::vector<std::string> want = {
        "0.000000000|ARR 0 4",
        "4.000000000|TDN E P PRE 0 0 3.000000000",
        "10.000000000|XDN UP 0 500000 PRE 1 0",
        "21.000000000|TDN C0 P PROC 0 4 0 0 10.000000000",
        "27.000000000|XDN DOWN 0 500000 PRE 1 0",
        "30.000000000|TDN E P POST 0 0 2.000000000",
        "32.000000000|TDN E D PRE -1 1 0 1.000000000",
        "35.000000000|XDN UP 0 125000 DEC 1 0",
        "40.000000000|TDN C0 D PROC 0 1 0 4.000000000",
        "43.000000000|XDN DOWN 0 125000 DEC 1 0",
        "45.000000000|TDN E D POST -1 1 0 1.000000000",
        "45.000000000|FIN 0",
    };
    RunStat st; std::string err;
    if (!run_frames(s, script, false, nullptr, st, err, /*keep_trace=*/true)) {
        std::cout << "EX1 FAIL: " << err << "\n"; return 1;
    }
    std::string why;
    if (!check_transcript(s, want, why)) { std::cout << "EX1 FAIL: " << why << "\n"; return 1; }
    if (std::fabs(s.sum_tdr - 30.0) > 1e-9 || s.done != 1) {
        std::cout << "EX1 FAIL: tdr/done\n"; return 1;
    }
    std::cout << "EX1 PASS: 12 events across 11 frames, TDR=30.000, token@45.000\n";
    return 0;
}

// ---- Example 2 (problem.md:622-658): cross-cloud group excerpt, 11 events --
static int mode_ex2() {
    Sim s;
    s.K = 2; s.S = 1; s.lat = 2; s.bw = 1; s.bpt = 125000; s.layers = 1;
    s.R = 2; s.arrs = { { 0, 4, 1 }, { 0, 4, 1 } };
    s.tab[P_PRE].push_back({ 1, 1 });
    s.tab[P_PROC].push_back({ 1, 1 });
    s.tab[P_POST].push_back({ 1, 1 });
    s.tab[P_POST].push_back({ 4, 1.25 });
    s.tab[D_PRE].push_back({ 1, 0.5 });
    s.tab[D_PRE].push_back({ 2, 1.5 });
    s.tab[D_PROC].push_back({ 1, 4 });
    s.tab[D_POST].push_back({ 1, 0.5 });
    s.tab[D_POST].push_back({ 2, 1.5 });
    // excerpt starts mid-test: req0 READY_D_PRE on C0, req1's P POST running
    s.rq.resize(2);
    s.rq[0].seen = true; s.rq[0].lin = 4; s.rq[0].cl = 0; s.rq[0].ph = R_DPRE;
    s.rq[0].arr = 0; s.rq[0].lhat = 1;
    s.rq[1].seen = true; s.rq[1].lin = 4; s.rq[1].cl = 1; s.rq[1].ph = PPOST_R;
    s.rq[1].arr = 0; s.rq[1].lhat = 1;
    s.ar = { 1 };
    s.lt.busy = true; s.lt.f = 16.5; s.lt.du = 1.25; s.lt.st = P_POST;
    s.lt.off = 0; s.lt.n = 1; s.lt.seq = 2; s.lt.fseq = -1;
    s.narr = 2; s.seqc = 2; s.first_arr = 0;
    const std::vector<std::vector<std::string>> script = {
        { "E D PRE -1 2 0 1" }, { "EMPTY" }, { "C0 D PROC 0 1 0" },
        { "C1 D PROC 1 1 1" }, { "EMPTY" }, { "EMPTY" },
        { "E D POST -1 2 0 1" }, { "EMPTY" },
    };
    const std::vector<std::string> want = {
        "16.500000000|TDN E P POST 1 1 1.250000000",
        "19.000000000|TDN E D PRE -1 2 0 1 1.500000000",
        "22.000000000|XDN UP 0 125000 DEC 1 0",
        "25.000000000|XDN UP 1 125000 DEC 1 1",
        "27.000000000|TDN C0 D PROC 0 1 0 4.000000000",
        "30.000000000|TDN C1 D PROC 1 1 1 4.000000000",
        "30.000000000|XDN DOWN 0 125000 DEC 1 0",
        "33.000000000|XDN DOWN 1 125000 DEC 1 1",
        "35.500000000|TDN E D POST -1 2 0 1 1.500000000",
        "35.500000000|FIN 0",
        "35.500000000|FIN 1",
    };
    RunStat st; std::string err;
    if (!run_frames(s, script, false, nullptr, st, err, /*keep_trace=*/true)) {
        std::cout << "EX2 FAIL: " << err << "\n"; return 1;
    }
    std::string why;
    if (!check_transcript(s, want, why)) { std::cout << "EX2 FAIL: " << why << "\n"; return 1; }
    std::cout << "EX2 PASS: 11 events, both FIN@35.500, FIFO order C0-then-C1\n";
    return 0;
}

// ============================================================================
// Self-constructed correctness checks (a)-(e). No repository tooling involved.
// ============================================================================

// (a) FIFO discipline: 3 UP transfers, entry order = P PRE assignment order,
// completion order strictly FIFO even when a later entry alone would finish
// sooner (no overtaking), tail rule f = max(enter, tail) + xfer.
static int mode_fifo() {
    Sim s;
    s.K = 2; s.S = 1; s.lat = 1; s.bw = 1; s.bpt = 100000; s.layers = 1;
    s.R = 3; s.arrs = { { 0, 10, 1 }, { 0, 1, 1 }, { 0, 5, 1 } };
    for (int c = 0; c < 6; ++c) s.tab[c].push_back({ 1, 1 });
    s.rq.resize(3); s.seqc = 3;
    const std::vector<std::vector<std::string>> script = {
        { "E P PRE 0 0" },        // t=0  ARR x3 (coalesced)
        { "E P PRE 0 1" },        // t=2  TDN P PRE 0  -> UP0 (len 10) enter 2
        { "E P PRE 1 2" },        // t=4  TDN P PRE 1  -> UP1 (len 1)  enter 4
        { "EMPTY" },              // t=6  TDN P PRE 2  -> UP2 (len 5)  enter 6
        { "C0 P PROC 0 1 0 0" },  // t=11 XDN UP 0
    };
    // UP finishes: UP0 = 2+9 = 11; UP1 = max(4,11)+1.8 = 12.8; UP2 = max(6,12.8)+5 = 17.8
    RunStat st; std::string err;
    std::vector<std::pair<std::string, std::string>> xups;
    size_t fi = 0;
    while (s.active()) {
        const double t = s.next_event_time();
        if (t >= INF) { std::cout << "FIFO FAIL: stuck\n"; return 1; }
#ifdef SB03_TRACE
        s.tr.clear();
#endif
        s.advance_to(t);
#ifdef SB03_TRACE
        for (const auto& e : s.tr)
            if (e.s.compare(0, 6, "XDN UP") == 0) xups.push_back({ fmt9(e.t), e.s });
#endif
        std::vector<std::string> resp;
        if (fi < script.size()) resp = script[fi];
        else for (const auto& a : greedy(s)) resp.push_back(act_str(a));
        for (const auto& ln : resp) {
            if (ln == "EMPTY") continue;
            Sim::Act a;
            if (!parse_act(ln, a, err)) { std::cout << "FIFO FAIL: parse " << ln << "\n"; return 1; }
            const char* e = s.apply(a, t);
            if (e) { std::cout << "FIFO FAIL: " << e << " @ " << ln << "\n"; return 1; }
        }
        ++fi;
        s.jclear();
    }
    if (xups.size() < 3) { std::cout << "FIFO FAIL: want >=3 XDN UP, got " << xups.size() << "\n"; return 1; }
    const char* want[3] = {
        "11.000000000|XDN UP 0 1000000 PRE 1 0",
        "12.800000000|XDN UP 0 100000 PRE 1 1",
        "17.800000000|XDN UP 1 500000 PRE 1 2",
    };
    for (int i = 0; i < 3; ++i) {
        const std::string got = xups[i].first + "|" + xups[i].second;
        if (got != want[i]) {
            std::cout << "FIFO FAIL: XDN UP #" << i << " got '" << got << "' want '" << want[i] << "'\n";
            return 1;
        }
    }
    if (s.done != 3) { std::cout << "FIFO FAIL: done " << s.done << "\n"; return 1; }
    std::cout << "FIFO PASS: 3 UPs complete in entry order at 11.000/12.800/17.800 "
              << "(no overtake; tail-bound waits exact)\n";
    return 0;
}

// (b) per-cloud UP split of one cross-cloud D PRE, members {c0:2, c1:1}, D PRE
// line order "2 0 1" (c1 first) — per-cloud UPs must enter by CLOUD INDEX with
// ids as a subsequence of the D PRE line, sizes 2*bpt / 1*bpt. A 4th request
// arrives at t=0 to provide the frame the D PRE responds to; greedy completes
// the run afterwards.
static int mode_split() {
    Sim s;
    s.K = 2; s.S = 1; s.lat = 1; s.bw = 1; s.bpt = 125000; s.layers = 1;
    s.R = 4; s.arrs = { { 0, 1, 1 }, { 0, 1, 1 }, { 0, 1, 1 }, { 0, 1, 1 } };
    s.tab[P_PRE].push_back({ 1, 1 });
    s.tab[P_PROC].push_back({ 1, 1 });
    s.tab[P_POST].push_back({ 1, 1 });
    s.tab[D_PRE].push_back({ 1, 1 });
    s.tab[D_PRE].push_back({ 3, 3 });
    s.tab[D_PROC].push_back({ 1, 1 });
    s.tab[D_PROC].push_back({ 2, 2 });
    s.tab[D_POST].push_back({ 1, 1 });
    s.tab[D_POST].push_back({ 3, 3 });
    s.rq.resize(4);
    for (int i = 0; i < 3; ++i) {
        s.rq[i].seen = true; s.rq[i].lin = 1; s.rq[i].ph = R_DPRE;
        s.rq[i].arr = 0; s.rq[i].lhat = 1;
    }
    s.rq[0].cl = 0; s.rq[1].cl = 0; s.rq[2].cl = 1;
    s.narr = 3; s.seqc = 4; s.first_arr = 0;
    const std::vector<std::vector<std::string>> script = {
        { "E D PRE -1 3 2 0 1" },  // t=0  ARR 3; group spans C0{0,1}, C1{2}
        { "EMPTY" },              // t=4  TDN D PRE (dur 3) -> UP C0 then UP C1
        { "C0 D PROC 0 2 0 1" },  // t=7  XDN UP 0 250000 DEC 2 0 1
        { "C1 D PROC 1 1 2" },    // t=9  XDN UP 1 125000 DEC 1 2
        { "EMPTY" },              // t=10 TDN C0 D PROC
        { "EMPTY" },              // t=11 TDN C1 D PROC
        { "EMPTY" },              // t=13 XDN DOWN 0 250000 DEC 2 0 1
        { "E D POST -1 3 2 0 1" },// t=15 XDN DOWN 1 125000 DEC 1 2
        { "E P PRE 1 3" },        // t=19 TDN D POST + FIN x3; seed rid3 (the last
                                  //      frame MUST create a future event)
    };
    RunStat st; std::string err;
    std::vector<std::pair<std::string, std::string>> xups, dpost;
    size_t fi = 0;
    while (s.active()) {
        const double t = s.next_event_time();
        if (t >= INF) { std::cout << "SPLIT FAIL: stuck\n"; return 1; }
#ifdef SB03_TRACE
        s.tr.clear();
#endif
        s.advance_to(t);
#ifdef SB03_TRACE
        for (const auto& e : s.tr) {
            if (e.s.compare(0, 6, "XDN UP") == 0) xups.push_back({ fmt9(e.t), e.s });
            if (e.s.compare(0, 13, "TDN E D POST ") == 0) dpost.push_back({ fmt9(e.t), e.s });
        }
#endif
        std::vector<std::string> resp;
        if (fi < script.size()) resp = script[fi];
        else for (const auto& a : greedy(s)) resp.push_back(act_str(a));
        for (const auto& ln : resp) {
            if (ln == "EMPTY") continue;
            Sim::Act a;
            if (!parse_act(ln, a, err)) { std::cout << "SPLIT FAIL: parse " << ln << "\n"; return 1; }
            const char* e = s.apply(a, t);
            if (e) { std::cout << "SPLIT FAIL: " << e << " @ " << ln << "\n"; return 1; }
        }
        ++fi;
        s.jclear();
    }
    const char* wu[2] = { "7.000000000|XDN UP 0 250000 DEC 2 0 1",
                          "9.000000000|XDN UP 1 125000 DEC 1 2" };
    if (xups.size() < 2) { std::cout << "SPLIT FAIL: XDN UP count " << xups.size() << "\n"; return 1; }
    for (int i = 0; i < 2; ++i) {
        const std::string got = xups[i].first + "|" + xups[i].second;
        if (got != wu[i]) {
            std::cout << "SPLIT FAIL: XDN UP #" << i << " got '" << got << "' want '" << wu[i] << "'\n";
            return 1;
        }
    }
    bool saw_post = false;
    for (const auto& d : dpost)
        if (d.first == "19.000000000" &&
            d.second == "TDN E D POST -1 3 2 0 1 3.000000000") saw_post = true;
    if (!saw_post) { std::cout << "SPLIT FAIL: D POST frame missing\n"; return 1; }
    if (s.done != 4) { std::cout << "SPLIT FAIL: done " << s.done << "\n"; return 1; }
    std::cout << "SPLIT PASS: D PRE -1 3 '2 0 1' -> UP C0(2x125000, ids 0 1) @7.000 first, "
              "UP C1(125000, id 2) @9.000 second; D POST @19.000 FIN x3, all 4 done\n";
    return 0;
}

// (c) chunking: 3 pieces [0,2),[2,4),[4,6) of layers=6, pproc=30 -> dur 10 each;
// ONLY the last piece queues DOWN; TDN dur echoes exactly 10.000000000.
static int mode_chunk() {
    Sim s;
    s.K = 1; s.S = 1; s.lat = 1; s.bw = 1; s.bpt = 125000; s.layers = 6;
    s.R = 1; s.arrs = { { 0, 1, 1 } };
    s.tab[P_PRE].push_back({ 1, 1 });
    s.tab[P_PROC].push_back({ 1, 30 });
    s.tab[P_POST].push_back({ 1, 1 });
    s.tab[D_PRE].push_back({ 1, 1 });
    s.tab[D_PROC].push_back({ 1, 1 });
    s.tab[D_POST].push_back({ 1, 1 });
    s.rq.resize(1); s.seqc = 1;
    const std::vector<std::vector<std::string>> script = {
        { "E P PRE 0 0" },          // t=0  ARR
        { "EMPTY" },                // t=2  TDN P PRE -> UP (len 1) done 4
        { "C0 P PROC 0 2 0 0" },    // t=4  XDN UP; piece1 dur 2/6*30 = 10
        { "C0 P PROC 2 4 0 0" },    // t=15 TDN piece1 (no DOWN)
        { "C0 P PROC 4 6 0 0" },    // t=26 TDN piece2 (no DOWN)
        { "EMPTY" },                // t=37 TDN piece3 (LAST -> DOWN done 39)
        { "E P POST 0 0" },         // t=39 XDN DOWN PRE
        { "E D PRE -1 1 0" },       // t=41 TDN P POST (TDR=41)
        { "EMPTY" },                // t=43 TDN D PRE
        { "C0 D PROC 0 1 0" },      // t=45 XDN UP DEC
        { "EMPTY" },                // t=47 TDN D PROC
        { "E D POST -1 1 0" },      // t=49 XDN DOWN DEC
        { "EMPTY" },                // t=51 TDN D POST + FIN
    };
    RunStat st; std::string err;
    if (!run_frames(s, script, false, nullptr, st, err, /*keep_trace=*/true)) {
        std::cout << "CHUNK FAIL: " << err << "\n"; return 1;
    }
    const std::vector<std::string> want = {
        "0.000000000|ARR 0 1",
        "2.000000000|TDN E P PRE 0 0 1.000000000",
        "4.000000000|XDN UP 0 125000 PRE 1 0",
        "15.000000000|TDN C0 P PROC 0 2 0 0 10.000000000",
        "26.000000000|TDN C0 P PROC 2 4 0 0 10.000000000",
        "37.000000000|TDN C0 P PROC 4 6 0 0 10.000000000",
        "39.000000000|XDN DOWN 0 125000 PRE 1 0",
        "41.000000000|TDN E P POST 0 0 1.000000000",
        "43.000000000|TDN E D PRE -1 1 0 1.000000000",
        "45.000000000|XDN UP 0 125000 DEC 1 0",
        "47.000000000|TDN C0 D PROC 0 1 0 1.000000000",
        "49.000000000|XDN DOWN 0 125000 DEC 1 0",
        "51.000000000|TDN E D POST -1 1 0 1.000000000",
        "51.000000000|FIN 0",
    };
    std::string why;
    if (!check_transcript(s, want, why)) { std::cout << "CHUNK FAIL: " << why << "\n"; return 1; }
    if (std::fabs(s.sum_tdr - 41.0) > 1e-9) { std::cout << "CHUNK FAIL: tdr\n"; return 1; }
    std::cout << "CHUNK PASS: 3 pieces dur 10.000000000 each (=(le-ls)/6*30), one DOWN "
              "(PRE) only after the LAST piece @39.000, TDR=41.000\n";
    return 0;
}

// (d) tie/coalescing: two cloud tasks assigned in ONE response (assignment-line
// order) with identical durations finish at the exact same double -> ONE frame
// with both TDNs; their DOWNs enter in assignment-line order (9.000 then
// 11.000); an UP and a DOWN completing at the same instant coalesce into one
// frame (t=11). rid2 arrives at t=0 to provide the first frame; greedy
// completes the run.
static int mode_tie() {
    Sim s;
    s.K = 2; s.S = 1; s.lat = 1; s.bw = 1; s.bpt = 125000; s.layers = 6;
    s.R = 3;
    s.arrs = { { 0, 1, 1 }, { 0, 1, 1 }, { 0, 1, 1 } };
    s.tab[P_PRE].push_back({ 1, 1 });
    s.tab[P_PROC].push_back({ 1, 6 });   // full piece [0,6) dur 6 -> tie at 7
    s.tab[P_POST].push_back({ 1, 1 });
    s.tab[D_PRE].push_back({ 1, 1 });
    s.tab[D_PRE].push_back({ 2, 2 });
    s.tab[D_PROC].push_back({ 1, 1 });
    s.tab[D_PROC].push_back({ 2, 2 });
    s.tab[D_POST].push_back({ 1, 1 });
    s.tab[D_POST].push_back({ 3, 3 });
    s.rq.resize(3);
    s.rq[0].seen = true; s.rq[0].lin = 1; s.rq[0].cl = 0; s.rq[0].ph = R_PPROC;
    s.rq[0].arr = 0; s.rq[0].lhat = 1;
    s.rq[1].seen = true; s.rq[1].lin = 1; s.rq[1].cl = 1; s.rq[1].ph = R_PPROC;
    s.rq[1].arr = 0; s.rq[1].lhat = 1;
    s.narr = 2; s.seqc = 3; s.first_arr = 0;   // rid2 arrives at t=0 (frame 0)
    const std::vector<std::vector<std::string>> script = {
        { "C0 P PROC 0 6 0 0", "C1 P PROC 0 6 1 1" },  // t=0 ARR 2: tie pair starts
        { "E P PRE 0 2" },        // t=7  BOTH TDNs in one frame
        { "E P POST 0 0" },       // t=9  XDN DOWN 0 + TDN P PRE 2
        { "E P POST 1 1", "C0 P PROC 0 6 0 2" },  // t=11 XDN UP 0 + XDN DOWN 1
        { "E D PRE -1 2 0 1" },   // t=13 both ready -> greedy completes
    };
    std::vector<std::pair<std::string, std::vector<std::string>>> frames_ev;
    std::vector<std::pair<std::string, std::string>> xdns_down;
    std::string err;
    size_t fi = 0;
    while (s.active()) {
        const double t = s.next_event_time();
        if (t >= INF) { std::cout << "TIE FAIL: stuck\n"; return 1; }
#ifdef SB03_TRACE
        s.tr.clear();
#endif
        s.advance_to(t);
#ifdef SB03_TRACE
        std::vector<std::string> ev;
        for (const auto& e : s.tr) {
            ev.push_back(e.s);
            if (e.s.compare(0, 8, "XDN DOWN") == 0) xdns_down.push_back({ fmt9(e.t), e.s });
        }
        frames_ev.push_back({ fmt9(t), ev });
#endif
        std::vector<std::string> resp;
        if (fi < script.size()) resp = script[fi];
        else for (const auto& a : greedy(s)) resp.push_back(act_str(a));
        for (const auto& ln : resp) {
            if (ln == "EMPTY") continue;
            Sim::Act a;
            if (!parse_act(ln, a, err)) { std::cout << "TIE FAIL: parse " << ln << "\n"; return 1; }
            const char* e = s.apply(a, t);
            if (e) { std::cout << "TIE FAIL: " << e << " @ " << ln << "\n"; return 1; }
        }
        ++fi;
        s.jclear();
    }
    // (i) the exact tie: one frame at 7.000 holding both TDNs
    bool tie_frame = false, coal_frame = false;
    for (const auto& f : frames_ev) {
        if (f.first != "7.000000000") continue;
        int hit = 0;
        for (const auto& e : f.second) {
            if (e == "TDN C0 P PROC 0 6 0 0 6.000000000") ++hit;
            if (e == "TDN C1 P PROC 0 6 1 1 6.000000000") ++hit;
        }
        tie_frame = (hit == 2 && f.second.size() == 2);
    }
    if (!tie_frame) { std::cout << "TIE FAIL: no single 7.000 frame with both TDNs\n"; return 1; }
    // (ii) DOWN FIFO order = assignment-line order: C0's at 9.000, C1's at 11.000
    if (xdns_down.size() < 2 || xdns_down[0].first != "9.000000000" ||
        xdns_down[0].second != "XDN DOWN 0 125000 PRE 1 0" ||
        xdns_down[1].first != "11.000000000" ||
        xdns_down[1].second != "XDN DOWN 1 125000 PRE 1 1") {
        std::cout << "TIE FAIL: DOWN order wrong: "
                  << (xdns_down.size() >= 2 ? xdns_down[0].first + "|" + xdns_down[0].second +
                       " then " + xdns_down[1].first + "|" + xdns_down[1].second
                       : "too few") << "\n";
        return 1;
    }
    // (iii) UP+DOWN coalescing at 11.000 (rid2's UP + rid1's DOWN, one frame)
    for (const auto& f : frames_ev) {
        if (f.first != "11.000000000") continue;
        bool up = false, dn = false;
        for (const auto& e : f.second) {
            if (e == "XDN UP 0 125000 PRE 1 2") up = true;
            if (e == "XDN DOWN 1 125000 PRE 1 1") dn = true;
        }
        coal_frame = up && dn;
    }
    if (!coal_frame) { std::cout << "TIE FAIL: no UP+DOWN coalesced 11.000 frame\n"; return 1; }
    if (s.done != 3) { std::cout << "TIE FAIL: done " << s.done << "\n"; return 1; }
    std::cout << "TIE PASS: equal-duration pair -> ONE 7.000 frame with both TDNs, "
              "DOWNs in assignment-line order (9.000 C0 then 11.000 C1), "
              "UP+DOWN coalesce @11.000, 3/3 done\n";
    return 0;
}

// ---- journal exactness checksum (rollback must restore this bit-for-bit) ---
static uint64_t sim_chk(const Sim& s) {
    uint64_t h = 1469598103934665603ull;
    auto md = [&](double v) { uint64_t x; memcpy(&x, &v, 8); h ^= x; h *= 1099511628211ull; };
    auto mi = [&](uint64_t x) { h ^= x; h *= 1099511628211ull; };
    md(s.lfree); md(s.now);
    for (int k = 0; k < 8; ++k) md(s.cfree[k]);
    auto mt = [&](const Sim::Task& T) {
        mi((T.busy ? 1u : 0u) | ((uint64_t)T.st << 1) | ((uint64_t)T.off << 8) |
           ((uint64_t)T.n << 20) | ((uint64_t)T.seq << 32) | ((uint64_t)(T.fseq + 1) << 50));
        md(T.f); md(T.du); mi((uint64_t)T.ls << 32 | (uint64_t)T.le);
    };
    mt(s.lt); for (int k = 0; k < 8; ++k) mt(s.ct[k]);
    mi(s.uph); mi(s.dnh);
    auto mq = [&](const std::vector<Xf>& q) {
        mi(q.size());
        for (const auto& x : q) {
            md(x.f); mi((uint64_t)x.len | ((uint64_t)x.off << 20) |
                        ((uint64_t)x.n << 36) | ((uint64_t)x.rc << 50) |
                        (x.pre ? 1u : 0u));
        }
    };
    mq(s.up); mq(s.dn);
    mi(s.ar.size());
    for (int x : s.ar) mi((uint64_t)x);
    for (const auto& r : s.rq) {
        mi((uint64_t)r.ph | ((uint64_t)r.it << 8) | ((uint64_t)r.nls << 20) |
           ((uint64_t)(r.cl + 1) << 26) | ((uint64_t)r.lin << 32) |
           ((uint64_t)r.lhat << 45) | (r.seen ? 1ull << 55 : 0ull) | (r.fin ? 1ull << 56 : 0ull));
        md(r.arr); md(r.ftk); md(r.ltk);
    }
    md(s.sum_tdr); md(s.n_tdr); md(s.sum_gap); md(s.sum_span);
    md(s.tok); md(s.last_fin); md(s.first_arr);
    mi((uint64_t)s.n_gap); mi((uint64_t)s.done); mi((uint64_t)s.narr);
    mi((uint64_t)s.seqc); mi((uint64_t)s.nev);
    return h;
}

// (e) rollback: mid-run snapshot, 50 random LEGAL actions interleaved with
// advances, journal-rollback, state must be bit-for-bit identical and the run
// must still complete afterwards.
struct Rnd {
    unsigned st;
    Rnd(unsigned s) : st(s ? s : 1u) {}
    unsigned operator()() { st ^= st << 13; st ^= st >> 7; st ^= st << 17; return st; }
};
static std::vector<Sim::Act> rand_legal(Sim& s, Rnd& rn) {
    std::vector<Sim::Act> out;
    const unsigned roll = rn() % 4;
    if (!s.lt.busy) {
        int idp = -1, idq = -1;
        for (int r = 0; r < s.R; ++r) {
            const auto& q = s.rq[r];
            if (!q.seen || q.fin) continue;
            if (q.ph == R_PPOST && idp < 0) idp = r;
            if (q.ph == R_PPRE && idq < 0) idq = r;
        }
        std::vector<int> dpre, dpost;
        for (int r = 0; r < s.R; ++r) {
            const auto& q = s.rq[r];
            if (!q.seen || q.fin) continue;
            if (q.ph == R_DPRE) dpre.push_back(r);
            if (q.ph == R_DPOST) dpost.push_back(r);
        }
        const bool idle = roll == 0 && (idp >= 0 || idq >= 0 || !dpre.empty() || !dpost.empty())
                          && (s.narr < s.R || s.lt.busy);
        if (!idle) {
            if (idp >= 0) { Sim::Act a; a.st = P_POST; a.cl = s.rq[idp].cl; a.ids = { idp }; out.push_back(a); }
            else if (idq >= 0) {
                Sim::Act a; a.st = P_PRE; a.cl = (int)(rn() % (unsigned)s.K); a.ids = { idq };
                out.push_back(a);
            } else if (!dpre.empty()) {
                Sim::Act a; a.st = D_PRE;
                for (int r : dpre) if (rn() % 2) a.ids.push_back(r);
                if (a.ids.empty()) a.ids.push_back(dpre[0]);
                out.push_back(a);
            } else if (!dpost.empty()) {
                Sim::Act a; a.st = D_POST;
                for (int r : dpost) if (rn() % 2) a.ids.push_back(r);
                if (a.ids.empty()) a.ids.push_back(dpost[0]);
                out.push_back(a);
            }
        }
    }
    for (int k = 0; k < s.K; ++k) {
        if (s.ct[k].busy) continue;
        int idp = -1;
        std::vector<int> dq;
        for (int r = 0; r < s.R; ++r) {
            const auto& q = s.rq[r];
            if (!q.seen || q.fin || q.cl != k) continue;
            if (q.ph == R_PPROC && idp < 0) idp = r;
            if (q.ph == R_DPROC) dq.push_back(r);
        }
        if (idp >= 0 && rn() % 4 != 0) {
            Sim::Act a; a.st = P_PROC; a.srv = k; a.cl = k; a.ids = { idp };
            a.ls = s.rq[idp].nls; a.le = s.layers;
            if (a.ls == 0 && s.layers > 1 && rn() % 2) a.le = s.layers / 2;
            out.push_back(a);
        } else if (!dq.empty() && rn() % 4 != 0) {
            Sim::Act a; a.st = D_PROC; a.srv = k; a.cl = k;
            for (int r : dq) if (rn() % 2) a.ids.push_back(r);
            if (a.ids.empty()) a.ids.push_back(dq[0]);
            out.push_back(a);
        }
    }
    return out;
}

static int mode_roll(unsigned seed) {
    Sim s;
    s.K = 4; s.S = 1.5; s.lat = 0.8; s.bw = 20.0; s.bpt = 60000; s.layers = 32;
    auto row = [&](int c, int b, double v) { s.tab[c].push_back(Sim::Row{ b, v }); };
    row(P_PRE, 1, 0.6);    row(P_PRE, 64, 2.0);    row(P_PRE, 4096, 60.0);
    row(P_PROC, 1, 0.9);   row(P_PROC, 64, 2.6);   row(P_PROC, 4096, 70.0);
    row(P_POST, 1, 0.4);   row(P_POST, 64, 1.4);   row(P_POST, 4096, 40.0);
    row(D_PRE, 1, 0.5);    row(D_PRE, 8, 1.2);     row(D_PRE, 64, 6.0);
    row(D_PROC, 1, 5.0);   row(D_PROC, 8, 8.0);    row(D_PROC, 64, 20.0);
    row(D_POST, 1, 0.5);   row(D_POST, 8, 1.0);    row(D_POST, 64, 4.0);
    Rnd rn(seed);
    s.R = 120;
    s.arrs.resize(s.R);
    for (int i = 0; i < s.R; ++i)
        s.arrs[i] = { i * 2.5, (int)(1 + rn() % 4096), (int)(1 + rn() % 4) };
    s.rq.resize(s.R);
    s.seqc = s.R;
    s.js.reserve(1 << 18); s.jv.reserve(1 << 16); s.evb.reserve(4096);
    s.ar.reserve(1 << 18);
    s.jlive = true;
    Rnd rr(seed ^ 0x5bf03635u);
    long long frames = 0;
    int nrand = 0;
    Sim::Mark mk{};
    uint64_t pre = 0;
    bool marked = false;
    while (s.active()) {
        const double t = s.next_event_time();
        if (t >= INF) { std::cout << "ROLL FAIL: stuck pre-rollback\n"; return 1; }
        s.advance_to(t);
        ++frames;
        if (frames == 40) {
            marked = true;
            mk = s.mark();
            pre = sim_chk(s);
            // 50 random legal actions interleaved with random advances
            while (nrand < 50) {
                auto acts = rand_legal(s, rr);
                if (acts.empty()) {
                    const double t2 = s.next_event_time();
                    if (t2 >= INF) break;
                    s.advance_to(t2);
                    continue;
                }
                for (const auto& a : acts) {
                    const char* e = s.apply(a, s.now);
                    if (e) { std::cout << "ROLL FAIL: rand illegal: " << e << "\n"; return 1; }
                    ++nrand;
                    if (nrand >= 50) break;
                }
                if (nrand < 50 && rr() % 2) {
                    const double t2 = s.next_event_time();
                    if (t2 < INF) s.advance_to(t2);
                }
            }
            s.rollback(mk);
            if (sim_chk(s) != pre) {
                std::cout << "ROLL FAIL: checksum differs after rollback\n"; return 1;
            }
            if (s.js.size() != mk.s || s.jv.size() != mk.v) {
                std::cout << "ROLL FAIL: journal sizes " << s.js.size() << "/" << s.jv.size()
                          << " != mark " << mk.s << "/" << mk.v << "\n"; return 1;
            }
        }
        std::vector<std::string> resp;
        for (const auto& a : greedy(s)) resp.push_back(act_str(a));
        for (const auto& ln : resp) {
            Sim::Act a; std::string perr;
            if (!parse_act(ln, a, perr)) { std::cout << "ROLL FAIL: parse\n"; return 1; }
            const char* e = s.apply(a, t);
            if (e) { std::cout << "ROLL FAIL: post-roll illegal: " << e << "\n"; return 1; }
        }
        s.jclear();
    }
    if (!marked) { std::cout << "ROLL FAIL: never reached frame 40\n"; return 1; }
    if (s.done != s.R) { std::cout << "ROLL FAIL: done " << s.done << "/" << s.R << "\n"; return 1; }
    std::cout << "ROLL PASS: " << nrand << " journaled random actions rolled back bit-for-bit "
              "(checksum equal, journal sizes restored), run completed " << s.done << "/" << s.R
              << " in " << frames << " frames\n";
    return 0;
}

// ---- perf bench: R / totLout synth load, greedy base, D_PRE candidate
// rollouts with journal-exactness self-check.
static void bench_setup(Sim& s, int RN, int totLout, unsigned seed) {
    s.K = 8; s.S = 1.5; s.lat = 0.8; s.bw = 20.0; s.bpt = 60000; s.layers = 32;
    auto row = [&](int c, int b, double v) { s.tab[c].push_back(Sim::Row{ b, v }); };
    row(P_PRE, 1, 0.6);    row(P_PRE, 64, 2.0);    row(P_PRE, 4096, 60.0);
    row(P_PROC, 1, 0.9);   row(P_PROC, 64, 2.6);   row(P_PROC, 4096, 70.0);
    row(P_POST, 1, 0.4);   row(P_POST, 64, 1.4);   row(P_POST, 4096, 40.0);
    row(D_PRE, 1, 0.5);    row(D_PRE, 8, 1.2);     row(D_PRE, 64, 6.0);
    row(D_PRE, 512, 35.0); row(D_PRE, 4096, 260.0);
    row(D_PROC, 1, 5.0);   row(D_PROC, 8, 8.0);    row(D_PROC, 64, 20.0);
    row(D_PROC, 512, 35.0); row(D_PROC, 4096, 90.0);
    row(D_POST, 1, 0.5);   row(D_POST, 8, 1.0);    row(D_POST, 64, 4.0);
    row(D_POST, 512, 25.0); row(D_POST, 4096, 200.0);
    s.sc_slo1 = 20000; s.sc_slo2 = 60; s.sc_ub = 1.0; s.sc_b = 0.01;
    s.sc_db = 8; s.sc_wt = 0.6; s.sc_wc = 0.4;
    unsigned st = seed ? seed : 1u;
    auto rnd = [&]() { st ^= st << 13; st ^= st >> 7; st ^= st << 17; return st; };
    s.R = RN; s.arrs.resize(RN);
    const int base = totLout / RN;
    int rem = totLout - base * RN;
    for (int i = 0; i < RN; ++i) {
        double t = i * 2.5;
        if (i % 50 == 0 && i > 0) t = s.arrs[i - 1].t;    // same-time burst pairs
        s.arrs[i] = { t, (int)(1 + rnd() % 4096), base + (rem-- > 0 ? 1 : 0) };
    }
    s.rq.resize(RN);
    s.seqc = RN;
    s.js.reserve(1 << 18); s.jv.reserve(1 << 16); s.evb.reserve(4096);
    s.ar.reserve(1 << 18);
}

struct BenchOut {
    long long frames = 0, events = 0, rollouts = 0, chk_bad = 0;
    double wall_us = 0, roll_us = 0, score = 0;
    double sim_us = 0, pol_us = 0;      // sim core vs test-policy generation
    int done = 0;
};

static bool bench_run(Sim& s, bool journal_live, bool rollouts_on, int split, int chunk,
                      BenchOut& o) {
    s.jlive = journal_live;
    const auto T0 = clk::now();
    while (s.active()) {
        const double t = s.next_event_time();
        if (t >= INF) return false;
        const auto tA = clk::now();
        s.advance_to(t);
        const auto tB = clk::now();
        o.sim_us += us_since(tA, tB);
        ++o.frames;
        const auto tC = clk::now();
        auto acts = greedy(s, split, chunk);
        o.pol_us += us_since(tC, clk::now());
        if (rollouts_on) {
            s.jlive = true;                    // journal inside rollouts
            for (const auto& a : acts) {
                if (a.st != D_PRE) continue;
                const int pool = (int)a.ids.size();
                if (pool < 4) continue;
                const int ms[3] = { 1, pool / 2, pool };
                for (int mi = 0; mi < 3; ++mi) {
                    const int m = ms[mi];
                    if (m < 1 || m > pool) continue;
                    const uint64_t pre = sim_chk(s);
                    const Sim::Mark mk = s.mark();
                    const auto r0 = clk::now();
                    Sim::Act c; c.st = D_PRE; c.srv = -1; c.cl = -1;
                    for (int k = 0; k < m; ++k) c.ids.push_back(a.ids[k]);
                    if (s.apply(c, t) == nullptr) s.advance_to(t + 60.0);
                    const double val = s.score(); (void)val;
                    o.roll_us += us_since(r0, clk::now());
                    s.rollback(mk);
                    if (sim_chk(s) != pre) ++o.chk_bad;
                    ++o.rollouts;
                }
            }
            s.jlive = journal_live;
        }
        if (!acts.empty()) {
            const auto tD = clk::now();
            for (const auto& a : acts) {
                const char* e = s.apply(a, t);
                if (e) { std::cerr << "bench illegal: " << e << "\n"; return false; }
            }
            o.sim_us += us_since(tD, clk::now());
        }
        s.jclear();
    }
    o.wall_us = us_since(T0, clk::now());
    o.events = s.nevt; o.done = s.done; o.score = s.score();
    return true;
}

static int mode_bench(int RN, int totLout, unsigned seed) {
    BenchOut a, b;
    {
        Sim s; bench_setup(s, RN, totLout, seed);
        if (!bench_run(s, false, false, 0, 0, a)) { std::cout << "BENCH A FAIL\n"; return 1; }
    }
    {
        Sim s; bench_setup(s, RN, totLout, seed);
        if (!bench_run(s, true, true, 0, 0, b)) { std::cout << "BENCH B FAIL\n"; return 1; }
    }
    const double live_b = b.wall_us - b.roll_us;
    std::cout << "BENCH R=" << RN << " totLout=" << totLout << " K=8 layers=32\n";
    std::cout << "  pass A (mirror-only, no journal): events=" << a.events
              << " frames=" << a.frames << " wall=" << a.wall_us / 1e6 << "s"
              << " => " << (a.wall_us * 1000.0 / (double)a.events) << " ns/event"
              << " done=" << a.done << "/" << RN << " score=" << a.score << "\n";
    std::cout << "  pass B (journal+rollouts): events=" << b.events
              << " frames=" << b.frames << " wall=" << b.wall_us / 1e6 << "s"
              << " live=" << live_b / 1e6 << "s"
              << " => " << (live_b * 1000.0 / (double)b.events) << " ns/event(journaled)\n";
    std::cout << "  rollouts=" << b.rollouts
              << " mean=" << (b.rollouts ? b.roll_us / b.rollouts : 0.0) << " us/rollout"
              << " total_rollout=" << b.roll_us / 1e6 << "s"
              << " chk_mismatch=" << b.chk_bad << "\n";
    return b.chk_bad == 0 && b.done == RN ? 0 : 1;
}

// ---- 2M-event stress: singleton decode (split=1) maximizes frames; 4-piece
// chunked prefills inflate P PROC TDNs; report CPU, us/event, peak RSS, and
// the extrapolated 2e6-frame time. Pass B adds journaled rollouts.
static int mode_stress(int RN, int totLout, unsigned seed) {
    BenchOut a, b;
    {
        Sim s; bench_setup(s, RN, totLout, seed);
        if (!bench_run(s, false, false, 1, 4, a)) { std::cout << "STRESS A FAIL\n"; return 1; }
    }
    const long rss_a = rss_kb();
    {
        Sim s; bench_setup(s, RN, totLout, seed);
        if (!bench_run(s, true, true, 0, 0, b)) { std::cout << "STRESS B FAIL\n"; return 1; }
    }
    const long rss_b = rss_kb();
    const double uspf_sim = a.sim_us / (double)a.frames;
    const double nsev_sim = a.sim_us * 1000.0 / (double)a.events;
    std::cout << "STRESS R=" << RN << " totLout=" << totLout << " K=8 layers=32 "
              << "(pass A: singleton decode + 4-piece prefills)\n";
    std::cout << "  pass A (forward, mirror-only): events=" << a.events
              << " frames=" << a.frames << " wall=" << a.wall_us / 1e6 << "s"
              << " [sim core " << a.sim_us / 1e6 << "s = " << nsev_sim << " ns/event, "
              << uspf_sim << " us/frame; test-policy scan " << a.pol_us / 1e6 << "s]"
              << " done=" << a.done << "/" << RN << " score=" << a.score << "\n";
    std::cout << "  extrapolated 2e6-frame test (sim core only): "
              << (uspf_sim * 2.0e6 / 1e6) << " s (limit 15 s)\n";
    std::cout << "  pass B (journal+rollouts): simulated_events(incl rollouts)=" << b.events
              << " frames=" << b.frames << " wall=" << b.wall_us / 1e6 << "s"
              << " rollouts=" << b.rollouts
              << " mean=" << (b.rollouts ? b.roll_us / b.rollouts : 0.0) << " us/rollout"
              << " chk_mismatch=" << b.chk_bad << "\n";
    std::cout << "  peak RSS: pass A " << rss_a / 1024.0 << " MB, pass B "
              << rss_b / 1024.0 << " MB (limit 256 MB)\n";
    return a.chk_bad == 0 && b.chk_bad == 0 && a.done == RN && b.done == RN ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "modes: ex1 | ex2 | fifo | split | chunk | tie | roll [seed] | "
                     "greedy <case> <scriptout> | case <case> <script> | "
                     "bench <R> <totLout> <seed> | stress <R> <totLout> <seed>\n";
        return 2;
    }
    const std::string m = argv[1];
    if (m == "ex1") return mode_ex1();
    if (m == "ex2") return mode_ex2();
    if (m == "fifo") return mode_fifo();
    if (m == "split") return mode_split();
    if (m == "chunk") return mode_chunk();
    if (m == "tie") return mode_tie();
    if (m == "roll") return mode_roll(argc >= 3 ? (unsigned)atoi(argv[2]) : 42u);
    if (m == "greedy" && argc >= 4) {
        Sim s; std::string err;
        if (!load_case(argv[2], s, err)) { std::cerr << "load: " << err << "\n"; return 2; }
        std::vector<std::vector<std::string>> sc;
        while (s.active()) {
            const double t = s.next_event_time();
            if (t >= INF) { std::cerr << "stuck\n"; return 1; }
#ifdef SB03_TRACE
            s.tr.clear();
#endif
            s.advance_to(t);
            std::vector<std::string> resp;
            for (const auto& a : greedy(s)) resp.push_back(act_str(a));
            sc.push_back(resp);
            for (const auto& ln : resp) {
                Sim::Act a;
                if (!parse_act(ln, a, err)) { std::cerr << err << "\n"; return 1; }
                const char* e = s.apply(a, t);
                if (e) { std::cerr << "gen illegal: " << e << " " << ln << "\n"; return 1; }
            }
            s.jclear();
        }
        save_script(argv[3], sc);
        std::cout << "GREEDY OK frames=" << sc.size() << " events=" << s.nevt
                  << " done=" << s.done << "/" << s.R
                  << " score=" << s.score() << "\n";
        return 0;
    }
    if (m == "case" && argc >= 4) {
        Sim s; std::string err;
        if (!load_case(argv[2], s, err)) { std::cerr << "load: " << err << "\n"; return 2; }
        std::vector<std::vector<std::string>> sc;
        if (!load_script(argv[3], sc)) { std::cerr << "load script\n"; return 2; }
        RunStat st;
        if (!run_frames(s, sc, false, &std::cout, st, err)) {
            std::cerr << "CASE FAIL: " << err << "\n"; return 1;
        }
        std::cerr << "STAT frames=" << st.frames << " events=" << s.nevt
                  << " done=" << s.done << "/" << s.R << " score=" << s.score() << "\n";
        return 0;
    }
    if (m == "bench" && argc >= 5) {
        return mode_bench(atoi(argv[2]), atoi(argv[3]), (unsigned)atoi(argv[4]));
    }
    if (m == "stress" && argc >= 5) {
        return mode_stress(atoi(argv[2]), atoi(argv[3]), (unsigned)atoi(argv[4]));
    }
    std::cerr << "bad mode\n";
    return 2;
}
#endif  // SB03_DRIVER
