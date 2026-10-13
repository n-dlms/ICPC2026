// SB05 — statistical inference layer: Lout posterior (survival-conditioned),
// total-work estimate, arrival-process rates, lull-vs-ended posterior, endgame
// trigger. Drop-in replacement for the ad-hoc ewma_lout_/lout_s_/lout_c_
// machinery in src/state.hpp/.cpp (see size ledger in SB05_inference.md).
// Fragment (header-style, namespace sb05) + verification driver under SB05_DRIVER.
// Derivations, model assumptions, failure modes, comparison tables:
//   research/techniques/SB05_inference.md
// Validation vs slow references on synthetic streams (>=1e5 events):
//   research/techniques/SB05_check.py
//
// Statement facts used (problem.md): 1<=Lin<=4096 (line 268), 1<=Lout<=512
// (line 269), R<=2000 not announced (267), arrivals nondecreasing in [0,1e9] ms
// (286), no end-of-arrivals signal (400-401), FIN rides the last D POST TDN
// (242). While a request runs we know Lout >= iter+1 (its FIN is the only
// reveal); observations are therefore Type-I right-censored-then-revealed.
//
// All updates O(1) (bounded by NL=512 constant loops on FIN), all queries O(1).
// No allocation, no RNG, deterministic. Fixed memory 543,928 B static
// (sizeof(Infer), printed by the SB05_check.py 'Z' probe).

#ifndef SB05_INFERENCE_HPP
#define SB05_INFERENCE_HPP

#include <cmath>
#include <cstring>

namespace sb05 {

struct Infer {
    // ---- tuning constants (documented in SB05_inference.md §1.2, §1.5, §3.1, §3.2) ----
    static constexpr int NL = 512;        // max Lout (problem.md:269)
    static constexpr int LG = NL + 1;     // histogram levels 0..512
    static constexpr int NB = 24;         // geometric Lin buckets (half-octave)
    static constexpr int NROW = NB + 1;   // + row NB = global histogram
    static constexpr int MAXLIN = 4096;   // PPRE_MAXLIN (state.hpp:140)
    static constexpr double PRIOR_LOUT = 10.0;  // cold-start prior (matches ewma_lout_ init)
    static constexpr double KAPPA = 4.0;  // EB shrinkage pseudo-count (sigma^2/tau^2)
    static constexpr int GAPB = 32;       // log10 gap bins (quarter-decade, 1ms..1e9ms)
    static constexpr double GAMMA = 0.98; // generation decay (window ~50 gaps)
    static constexpr double PI_DEAD = 0.02; // prior P(stream ended) at any moment

    // ---- Lin -> geometric bucket ----
    // Edges: [1,2),[2,3),[3,4),[4,6),[6,8),[8,11),[11,16),[16,23),[23,32),
    // [32,45),[45,64),[64,91),[91,128),[128,181),[181,256),[256,362),
    // [362,512),[512,724),[724,1024),[1024,1448),[1448,2048),[2048,2896),
    // [2896,4096),[4096,inf)
    unsigned char lin2b[MAXLIN + 1];

    // ---- completed-Lout histograms, suffix aggregates ----
    // Row b<NB: bucket b; row NB: global. cSuf[r][c] = #{fin in row r, L>=c},
    // sSuf[r][c] = Sum{l*n}, qSuf[r][c] = Sum{l*l*n}. Rebuilt incrementally
    // in O(lout) per FIN. tCnt/tSum/tSum2: UNTRUNCATED moments (Mills fallback
    // when no completed observation reaches the query cut).
    int cSuf[NROW][LG];
    double sSuf[NROW][LG];
    double qSuf[NROW][LG];
    int tCnt[NROW];
    double tSum[NROW];
    double tSum2[NROW];

    // Self-consistent (hard-EM) tail correction. Completed-only histograms
    // are length-biased early (short Louts finish first). Each UNFINISHED
    // request injects one expected observation: count 1 at value
    //   v = max(cut, T_glob(cut))
    // into the corrected suffix aggregates at every level <= its cut, where
    // T_glob(c) = E[L|L>=c] under the COMPLETED data (raw truncated mean,
    // Mills fallback, ewma/prior last). This is one EM E-step of the truncated-
    // data NPMLE (Lynden-Bell/Turnbull style) with the injection value frozen
    // at registration; O(cut) per event, O(1) query.
    // Per-request registration state (R <= 2000, problem.md:267).
    static const int RMAX = 2000;
    int aCut[RMAX];
    double aVal[RMAX];
    unsigned char aOn[RMAX];
    // corrected suffix aggregates (completed-exact + active-injected)
    int eCnt[NROW][LG];
    double eSum[NROW][LG];
    double eSum2[NROW][LG];

    // ---- EWMA fallback (kept from current solver; level-3 cold start) ----
    double ewma_lout;
    int ewma_n;

    int fin_count;

    // ---- arrival / decode-ready processes (generation-based decay) ----
    // Decay-by-event avoids clock-scale failure at t~1e9 ms and prices bursts
    // correctly (a burst of tiny gaps raises the rate).
    double aArr, aDr;      // decayed event counts
    double gArr, gDr;      // decayed mean gap (ms)
    int nArr, nDr;

    // ---- inter-arrival gap survival (RAW counts, log-binned) ----
    // uSuf[k] = #{observed gaps in bins >= k}; query O(1). Raw (not decayed)
    // counts are the right basis for END detection: the gap law does not
    // change because arrivals paused; rate slow-down is lambda_hat's job.
    double uSuf[GAPB];
    double uTot;

    void init() {
        std::memset(this, 0, sizeof(*this));
        ewma_lout = PRIOR_LOUT;
        int e[NB + 1] = {1,2,3,4,6,8,11,16,23,32,45,64,91,128,
                         181,256,362,512,724,1024,1448,2048,2896,4096,MAXLIN + 1};
        int b = 0;
        for (int l = 1; l <= MAXLIN; ++l) {
            while (l >= e[b + 1]) ++b;
            lin2b[l] = static_cast<unsigned char>(b);
        }
    }

    static int gap_bin(double g) {
        if (!(g > 1.0)) return 0;                       // NaN/negative/tiny -> bin 0
        const double lg = std::log10(g) * 4.0;          // quarter-decade bins
        if (lg >= GAPB) return GAPB - 1;
        int k = static_cast<int>(lg);
        if (k < 0) k = 0;
        return k;
    }

    // ---- events ----------------------------------------------------------
    void on_arr(int rid, int lin, double gap_ms) {
        ++nArr;
        if (rid >= 0 && rid < RMAX && !aOn[rid])
            inject(rid, lin, 1);
        aArr = aArr * GAMMA + 1.0;
        // EWMA of the gap (mean form: steady state = E[gap], NOT the decayed sum)
        gArr = (nArr == 1) ? gap_ms : gArr * GAMMA + (1.0 - GAMMA) * gap_ms;
        if (nArr >= 2 && gap_ms >= 0.0) {
            const int k = gap_bin(gap_ms);
            for (int j = 0; j <= k; ++j) uSuf[j] += 1.0;
            uTot += 1.0;
        }
    }

    void on_decode_ready(double gap_ms) {
        ++nDr;
        aDr = aDr * GAMMA + 1.0;
        gDr = (nDr == 1) ? gap_ms : gDr * GAMMA + (1.0 - GAMMA) * gap_ms;
    }

    // ---- injection bookkeeping -------------------------------------------
    struct SuffR { int n; double mean; };   // raw completed-suffix view
    static SuffR suffR(const int(&cs)[NROW][LG], const double(&ss)[NROW][LG],
                       int row, int c) {
        SuffR s;
        s.n = cs[row][c];
        s.mean = s.n > 0 ? ss[row][c] / static_cast<double>(s.n) : 0.0;
        return s;
    }
    // Completed-data truncated mean at cut c (fallback chain), for injections.
    double t_glob(int c) const {
        if (c < 1) c = 1;
        if (c > NL) c = NL;
        const SuffR g = suffR(cSuf, sSuf, NB, c);
        if (g.n > 0) return g.mean;
        const Mom rg = mom(NB);
        if (rg.n > 0) {
            const double mu = rg.mu;
            const double sd = rg.sd >= 1.0 ? rg.sd : 1.0;
            const double z = (static_cast<double>(c) - mu) / sd;
            const double est = mu + sd * mills(z);
            return est >= static_cast<double>(c) ? est : static_cast<double>(c);
        }
        if (ewma_n > 0) {
            const double est = ewma_lout >= static_cast<double>(c)
                ? ewma_lout : static_cast<double>(c);
            return est;
        }
        const double p = PRIOR_LOUT >= static_cast<double>(c)
            ? PRIOR_LOUT : static_cast<double>(c);
        return p;
    }

    void inject_add(int r, int c, double v) {
        for (int l = 1; l <= c; ++l) {
            ++eCnt[r][l];
            eSum[r][l] += v;
            eSum2[r][l] += v * v;
        }
    }

    void inject(int rid, int lin, int cut) {
        if (rid < 0 || rid >= RMAX) return;
        if (cut < 1) cut = 1;
        if (cut > NL) cut = NL;
        const int r = static_cast<int>(lin2b[lin < 1 ? 1 : (lin > MAXLIN ? MAXLIN : lin)]);
        if (aOn[rid]) inject_remove(rid, r);
        const double v = t_glob(cut);
        aCut[rid] = cut;
        aVal[rid] = v;
        aOn[rid] = 1;
        inject_add(r, cut, v);
        inject_add(NB, cut, v);
    }

    void inject_remove(int rid, int r) {
        const int c = aCut[rid];
        const double v = aVal[rid];
        for (int l = 1; l <= c; ++l) {
            --eCnt[r][l];
            eSum[r][l] -= v;
            eSum2[r][l] -= v * v;
        }
        aOn[rid] = 0;
    }

    // Token produced: the request's certified cut advanced oc -> nc.
    // Re-injects at the updated expected value. O(cut).
    void on_iter(int rid, int lin, int nc) {
        if (nc > NL) nc = NL;
        inject(rid, lin, nc);
    }

    void on_fin(int rid, int lin, int lout) {
        if (lout < 1) lout = 1;
        if (lout > NL) lout = NL;
        const int r = static_cast<int>(lin2b[lin < 1 ? 1 : (lin > MAXLIN ? MAXLIN : lin)]);
        if (rid >= 0 && rid < RMAX && aOn[rid]) inject_remove(rid, r);
        // incremental suffix update: level ell joins suffixes c<=ell
        for (int c = 1; c <= lout; ++c) {
            ++cSuf[r][c];
            sSuf[r][c] += static_cast<double>(lout);
            qSuf[r][c] += static_cast<double>(lout) * static_cast<double>(lout);
            ++cSuf[NB][c];
            sSuf[NB][c] += static_cast<double>(lout);
            qSuf[NB][c] += static_cast<double>(lout) * static_cast<double>(lout);
        }
        ++fin_count;
        ++tCnt[r]; tSum[r] += static_cast<double>(lout);
        tSum2[r] += static_cast<double>(lout) * static_cast<double>(lout);
        ++tCnt[NB]; tSum[NB] += static_cast<double>(lout);
        tSum2[NB] += static_cast<double>(lout) * static_cast<double>(lout);
        ewma_lout = 0.05 * static_cast<double>(lout) + 0.95 * ewma_lout;
        ++ewma_n;
    }

    // ---- core suffix query ------------------------------------------------
    struct Suff { int n; double mean; double var; };
    static Suff suff(const int(&cs)[NROW][LG], const double(&ss)[NROW][LG],
                     const double(&qs)[NROW][LG], int row, int c) {
        Suff s;
        s.n = cs[row][c];
        if (s.n > 0) {
            s.mean = ss[row][c] / static_cast<double>(s.n);
            s.var = qs[row][c] / static_cast<double>(s.n) - s.mean * s.mean;
            if (s.var < 0.0) s.var = 0.0;
        } else { s.mean = 0.0; s.var = 0.0; }
        return s;
    }

    // Corrected suffix view at cut c. MEAN uses completed-exact +
    // EM-injected mass (location). VAR deliberately uses COMPLETED-only
    // moments blended with a thin-tail floor: injected observations are
    // pseudo-points whose true spread is unknown — counting them in the
    // second moment would fake confidence (validated in SB05_check.py).
    struct SuffC { int n; double mean; double var; int ncomp; };
    SuffC suffC(int row, int c) const {
        SuffC s;
        const int n = eCnt[row][c];
        s.ncomp = cSuf[row][c];
        s.n = n > 0 ? n : 0;
        if (s.n <= 0) { s.mean = 0.0; s.var = 0.0; return s; }
        s.mean = eSum[row][c] / static_cast<double>(s.n);
        if (s.ncomp > 0) {
            const double mc = sSuf[row][c] / static_cast<double>(s.ncomp);
            const double vc =
                qSuf[row][c] / static_cast<double>(s.ncomp) - mc * mc;
            s.var = vc > 0.0 ? vc : 0.0;
        } else s.var = 0.0;
        // thin-tail floor by how much completed evidence reached this cut
        const double hi = static_cast<double>(NL)
            - (s.mean > static_cast<double>(c) ? s.mean : static_cast<double>(c));
        const double fl = (hi > 1.0 ? hi : 1.0) * (hi > 1.0 ? hi : 1.0) / 12.0;
        const double wnc = static_cast<double>(s.ncomp)
            / (static_cast<double>(s.ncomp) + static_cast<double>(N0));
        s.var = wnc * s.var + (1.0 - wnc) * fl;
        return s;
    }

    static double est_mean_of(const SuffC& b, const SuffC& g) {
        if (b.n > 0) return b.mean;
        if (g.n > 0) return g.mean;
        return 0.0;
    }

    // ---- TASK 1: survival-conditioned Lout posterior ----------------------
    // Posterior mean of Lout for a request with Lin bucket b that has produced
    // `iter` tokens (constraint Lout >= c = iter+1). Coherent: result >= c.
    // Hierarchy:
    //   1. completed observations reach c (suffix mass > 0):
    //      empirical truncated mean, bucket shrunk toward global (EB, KAPPA);
    //   2. else raw moments exist (every completion below c): inverse-Mills
    //      normal-tail estimate E[L|L>=c] ~= mu + sigma*m(z), z=(c-mu)/sigma,
    //      m = phi/Phi_bar, sigma floored at 1 (token granularity);
    //   3. else EWMA; 4. prior. Every level floored at c.
    struct Mom { int n; double mu; double sd; };
    Mom mom(int row) const {
        Mom m;
        m.n = tCnt[row];
        if (m.n > 0) {
            m.mu = tSum[row] / static_cast<double>(m.n);
            double v = tSum2[row] / static_cast<double>(m.n) - m.mu * m.mu;
            if (v < 0.0) v = 0.0;
            m.sd = std::sqrt(v);
        } else { m.mu = 0.0; m.sd = 0.0; }
        return m;
    }
    // inverse Mills ratio phi(z)/(1-Phi(z)), capped into its asymptotic
    // z + 1/z beyond z=8 (double erfc underflows far above our range).
    static double mills(double z) {
        if (z > 8.0) return z + 1.0 / z;
        if (z < -8.0) z = -8.0;
        return std::exp(-0.5 * z * z) * 0.7978845608028654 /
               std::erfc(z * 0.70710678118654752);
    }
    // Cold-start spread: blend the raw empirical sd with a uniform-remainder
    // variance floor ((NL-max(c,mu))^2/12) by sample size (N0 pseudo-obs).
    // Without this, few-completion states report absurdly tight CIs.
    static const int N0 = 20;
    static double sd_eff(double sd, double n, double c, double mu) {
        double hi = static_cast<double>(NL) - (mu > c ? mu : c);
        if (hi < 1.0) hi = 1.0;
        const double floor_v = hi * hi / 12.0;
        const double w = n / (n + static_cast<double>(N0));
        const double v = w * sd * sd + (1.0 - w) * floor_v;
        return std::sqrt(v);
    }
    // Same idea for the certified-suffix branch: blend the empirical
    // (censored-sample) variance with a uniform-remainder floor by the number
    // of observations that actually reached cut c. Without this, states with
    // thin completed tails report absurdly tight intervals.
    // returns a (possibly inflated) STANDARD DEVIATION
    static double var_floor(double var, double n, double c, double mean) {
        return sd_eff(std::sqrt(var > 0.0 ? var : 0.0), n, c, mean);
    }

    double predict_lout(int lin, int iter) const {
        int c = iter + 1;
        if (c < 1) c = 1;
        if (c > NL) c = NL;
        const int r = static_cast<int>(lin2b[lin < 1 ? 1 : (lin > MAXLIN ? MAXLIN : lin)]);
        const SuffC b = suffC(r, c);
        const SuffC g = suffC(NB, c);
        double est;
        if (b.n > 0 || g.n > 0) {
            const double nb = static_cast<double>(b.n), ng = static_cast<double>(g.n);
            const double w = nb / (nb + ng + KAPPA);
            const double mb = b.n > 0 ? b.mean : g.mean;
            const double mg = g.n > 0 ? g.mean : mb;
            est = w * mb + ((ng + KAPPA) / (nb + ng + KAPPA)) * mg;
        } else {
            const Mom rb = mom(r), rg = mom(NB);
            if (rb.n > 0 || rg.n > 0) {
                const double nb2 = static_cast<double>(rb.n), ng2 = static_cast<double>(rg.n);
                const double w = nb2 / (nb2 + ng2 + KAPPA);
                const double mu = w * (rb.n > 0 ? rb.mu : rg.mu)
                    + ((ng2 + KAPPA) / (nb2 + ng2 + KAPPA)) * (rg.n > 0 ? rg.mu : rb.mu);
                const double sg = (rg.n > 0 ? rg.sd : rb.sd);
                const double sb = (rb.n > 0 ? rb.sd : sg);
                const double sd = w * sb + (1.0 - w) * sg;
                const double nn = w * static_cast<double>(rb.n)
                    + (1.0 - w) * static_cast<double>(rg.n);
                const double s2 = sd_eff(sd, nn, static_cast<double>(c), mu);
                const double z = (static_cast<double>(c) - mu) / s2;
                est = mu + s2 * mills(z);
            } else if (ewma_n > 0) est = ewma_lout;
            else est = PRIOR_LOUT;
        }
        return est >= static_cast<double>(c) ? est : static_cast<double>(c);
    }

    // Posterior sd of Lout under the same conditioning/hierarchy.
    double predict_sd(int lin, int iter) const {
        int c = iter + 1;
        if (c < 1) c = 1;
        if (c > NL) c = NL;
        const int r = static_cast<int>(lin2b[lin < 1 ? 1 : (lin > MAXLIN ? MAXLIN : lin)]);
        const SuffC b = suffC(r, c);
        const SuffC g = suffC(NB, c);
        if (b.n > 0 || g.n > 0) {
            const double nb = static_cast<double>(b.n), ng = static_cast<double>(g.n);
            const double w = nb / (nb + ng + KAPPA);
            const double vb = b.n > 0 ? b.var : g.var;
            const double vg = g.n > 0 ? g.var : vb;
            const double mb = b.n > 0 ? b.mean : g.mean;
            const double mg = g.n > 0 ? g.mean : mb;
            const double dm = mg >= mb ? mg - mb : mb - mg;
            // law of total variance for the two-component blend, then the
            // thin-tail conservative floor
            const double v = w * vb + (1.0 - w) * vg + w * (1.0 - w) * dm * dm;
            const double nn2 = w * nb + (1.0 - w) * ng;
            const double mu2 = est_mean_of(b, g);
            return var_floor(v, nn2, static_cast<double>(c), mu2);
        }
        const Mom rb = mom(r), rg = mom(NB);
        if (rb.n > 0 || rg.n > 0) {
            const double nb2 = static_cast<double>(rb.n), ng2 = static_cast<double>(rg.n);
            const double w = nb2 / (nb2 + ng2 + KAPPA);
            const double mu = w * (rb.n > 0 ? rb.mu : rg.mu)
                + ((ng2 + KAPPA) / (nb2 + ng2 + KAPPA)) * (rg.n > 0 ? rg.mu : rb.mu);
            const double sg = (rg.n > 0 ? rg.sd : rb.sd);
            const double sb = (rb.n > 0 ? rb.sd : sg);
            const double sd = w * sb + (1.0 - w) * sg;
            const double nn = w * static_cast<double>(rb.n)
                + (1.0 - w) * static_cast<double>(rg.n);
            const double s2 = sd_eff(sd, nn, static_cast<double>(c), mu);
            const double z = (static_cast<double>(c) - mu) / s2;
            const double mi = mills(z);
            const double vt = s2 * s2 * (1.0 + z * mi - mi * mi);
            return std::sqrt(vt > 0.0 ? vt : s2);
        }
        if (ewma_n > 0) { const double d = ewma_lout >= PRIOR_LOUT ? ewma_lout : PRIOR_LOUT; return 0.6 * d; }
        return 0.6 * PRIOR_LOUT;
    }

    // ---- TASK 2: total remaining work over unfinished requests ------------
    // cuts[j] = max(1, iter_j + 1); bl[j] = Lin_j (unclamped). O(m) per call;
    // caller decides cadence (endgame/rollout entry points only).
    // Returns posterior mean of Sum_j (Lout_j - (cut_j - 1)) and its SE:
    //   Var = Sum_j Var(L_j|L>=c_j)  +  Sum_j Var_b(c_j)/n_b(c_j)
    //   (predictive spread + parameter-estimation error of bucket means).
    double total_work(const int* cuts, const int* lins, int m, double* se) const {
        double tot = 0.0, var = 0.0;
        for (int j = 0; j < m; ++j) {
            int c = cuts[j];
            if (c < 1) c = 1;
            const double mu = predict_lout(lins[j], c - 1);
            double rem = mu - static_cast<double>(c - 1);
            if (rem < 1.0) rem = 1.0;
            tot += rem;
            const int r = static_cast<int>(lin2b[lins[j] < 1 ? 1 : (lins[j] > MAXLIN ? MAXLIN : lins[j])]);
            const SuffC b = suffC(r, c);
            const SuffC g = suffC(NB, c);
            double v, npar;
            if (b.n > 0 || g.n > 0) {
                const double nb2 = static_cast<double>(b.n), ng2 = static_cast<double>(g.n);
                const double w = nb2 / (nb2 + ng2 + KAPPA);
                const double vb = b.n > 0 ? b.var : g.var;
                const double vg = g.n > 0 ? g.var : vb;
                const double mb = b.n > 0 ? b.mean : g.mean;
                const double mg = g.n > 0 ? g.mean : mb;
                const double dm = mg >= mb ? mg - mb : mb - mg;
                double vv = w * vb + (1.0 - w) * vg + w * (1.0 - w) * dm * dm;
                const double nn2 = w * nb2 + (1.0 - w) * ng2;
                const double mu2 = b.n > 0 ? b.mean : g.mean;
                v = var_floor(vv, nn2, static_cast<double>(c), mu2);
                v = v * v;
                npar = b.n > 0 ? vb : (g.n > 0 ? vg : v);
            } else {
                const Mom rb = mom(r), rg = mom(NB);
                if (rb.n > 0 || rg.n > 0) {
                    const double nb3 = static_cast<double>(rb.n), ng3 = static_cast<double>(rg.n);
                    const double w = nb3 / (nb3 + ng3 + KAPPA);
                    const double sg = rg.n > 0 ? rg.sd : rb.sd;
                    const double sb = rb.n > 0 ? rb.sd : sg;
                    const double sd = w * sb + (1.0 - w) * sg;
                    const double nn = w * static_cast<double>(rb.n)
                        + (1.0 - w) * static_cast<double>(rg.n);
                    const double mu2 = mu >= static_cast<double>(c)
                        ? mu : static_cast<double>(c);
                    v = sd_eff(sd, nn, static_cast<double>(c), mu2);
                    v = v * v;
                    npar = v;
                } else {
                    const double d = ewma_n > 0 ? ewma_lout : PRIOR_LOUT;
                    const double mu3 = mu >= static_cast<double>(c)
                        ? mu : static_cast<double>(c);
                    v = sd_eff(0.6 * d, static_cast<double>(ewma_n),
                               static_cast<double>(c), mu3);
                    v = v * v;
                    npar = v;
                }
            }
            var += v;                                               // predictive spread
            const int nb = b.n > 0 ? b.n : (g.n > 0 ? g.n : (tCnt[r] > 0 ? tCnt[r] : 1));
            var += npar / static_cast<double>(nb);                  // parameter error
        }
        if (se) *se = std::sqrt(var > 0.0 ? var : 0.0);
        return tot;
    }

    // ---- TASK 3: rates and lull posterior ---------------------------------
    // Decayed-count rate estimates (events per ms). Equivalent sample size
    // alpha = A/(1-GAMMA); mean gap gBar; lambda = (alpha*(1-GAMMA))/gBar
    // normalized to a per-gap expectation: E[A_ss] = GAMMA/(1-GAMMA) under
    // unit rate per gap, so per-gap rate = A*(1-GAMMA)/GAMMA, per ms divide
    // by gBar. Returns 0 before two events.
    double lambda_arr() const {
        if (nArr < 2 || !(gArr > 0.0)) return 0.0;
        return (aArr * (1.0 - GAMMA) / GAMMA) / gArr;
    }
    double lambda_dr() const {
        if (nDr < 2 || !(gDr > 0.0)) return 0.0;
        return (aDr * (1.0 - GAMMA) / GAMMA) / gDr;
    }
    // Gamma-Poisson predictive P(next gap > delta) from the decayed count:
    // alpha = A/(1-GAMMA) equivalent gaps, beta = alpha/lambda_hat.
    double p_wait_arr(double delta) const {
        const double lam = lambda_arr();
        if (lam <= 0.0) return 1.0;
        const double alpha = aArr / (1.0 - GAMMA);
        const double beta = alpha / lam;
        const double r = beta / (beta + (delta > 0.0 ? delta : 0.0));
        return std::pow(r, alpha);
    }
    // Nonparametric lull-vs-ended posterior given observed silence d ms.
    // Model: H_alive => next gap iid from F (empirical); H_dead => none.
    // P(alive|d) = (1-pi0)*Shat(d) / (pi0 + (1-pi0)*Shat(d)) with the
    // add-one-smoothed Bernoulli-exceedance predictive
    //   Shat(d) = (#{observed gaps >= d} + 1) / (n_gaps + 2).
    // Never 0, never 1; decisive exactly when d exceeds the observed support.
    double p_alive(double silence_ms) const {
        if (nArr < 2) return 1.0;
        const int k = gap_bin(silence_ms > 0.0 ? silence_ms : 0.0);
        const double sh = (uSuf[k] + 1.0) / (uTot + 2.0);
        const double alive = (1.0 - PI_DEAD) * sh;
        return alive / (PI_DEAD + alive);
    }
    // R-hat under a hypothesized arrival horizon H (ms from now):
    //   Rhat(H) = N_arrived + lambda_hat * H   (documented: WITHOUT a horizon
    // prior R is unidentifiable — see SB05_inference.md §3.3).
    double r_hat(int arrived, double horizon_ms) const {
        double extra = lambda_arr() * (horizon_ms > 0.0 ? horizon_ms : 0.0);
        return static_cast<double>(arrived) + extra;
    }

    // ---- TASK 4: endgame / hold-D_PRE trigger -----------------------------
    // Wait-for-more-members iff the per-ms arrival value of a joined round
    // exceeds the per-ms holding cost (derivation SB05_inference.md §4):
    //   hold <=> p_alive_dr * lambda_dr * C1 > p_crit + rho_S * n_mt
    // C1 = singleton-round chain time 3S+dpre(1)+dproc(1)+dpost(1)+2*lat+2*beta
    // p_crit = P(held pool carries the global-last token) (work-share proxy),
    // n_mt = held requests already emitting (their spans stretch while held),
    // rho_S = V_S/V_E (SB04 calculus; 0 when w_tp=0 kills V_E).
    static bool hold_dpre(double p_al, double lam_dr, double C1,
                          double p_crit, double n_mt, double rho_S) {
        return p_al * lam_dr * C1 > p_crit + rho_S * n_mt;
    }
};

}  // namespace sb05

#endif

#ifdef SB05_DRIVER
// Verification driver: reads an event script on stdin, prints estimator
// values. Commands (one per line):
//   I                      init/reset
//   A <rid> <lin> <gap_ms>       arrival (gap since previous ARR)
//   T <rid> <lin> <new_cut>      token produced (certified cut advanced)
//   D <gap_ms>             decode-ready event (gap since previous)
//   F <rid> <lin> <lout>         finish
//   Q <lin> <iter>         print predict_lout
//   V <lin> <iter>         print predict_sd
//   LA / LD                print lambda_arr / lambda_dr
//   PW <delta>             print p_wait_arr(delta)
//   PA <silence>           print p_alive(silence)
//   RH <arrived> <horiz>   print r_hat
//   W <m>; then m lines "Wj <cut> <lin>"; then "We"   -> total_work + SE
//   H <p_al> <lam> <C1> <pcrit> <nmt> <rhoS>          -> hold_dpre 0/1
//   Z                         print sizeof(Infer)
//   X                         quit
#include <cstdio>
namespace {
sb05::Infer g_infer;
int g_w_cut[2048];
int g_w_lin[2048];
int g_w_m = 0;
}
int main() {
    g_infer.init();
    char cmd[8];
    while (std::scanf("%7s", cmd) == 1) {
        if (cmd[0] == 'X') break;
        if (cmd[0] == 'Z') { std::printf("%zu\n", sizeof(sb05::Infer)); continue; }
        switch (cmd[0]) {
        case 'I': g_infer.init(); break;
        case 'A': { int id, l; double g; if (std::scanf("%d %d %lf", &id, &l, &g) != 3) return 1; g_infer.on_arr(id, l, g); break; }
        case 'D': { double g; if (std::scanf("%lf", &g) != 1) return 1; g_infer.on_decode_ready(g); break; }
        case 'T': { int id, l, nc; if (std::scanf("%d %d %d", &id, &l, &nc) != 3) return 1; g_infer.on_iter(id, l, nc); break; }
        case 'F': { int id, l, o; if (std::scanf("%d %d %d", &id, &l, &o) != 3) return 1; g_infer.on_fin(id, l, o); break; }
        case 'Q': { int l, i; if (std::scanf("%d %d", &l, &i) != 2) return 1; std::printf("%.17g\n", g_infer.predict_lout(l, i)); break; }
        case 'V': { int l, i; if (std::scanf("%d %d", &l, &i) != 2) return 1; std::printf("%.17g\n", g_infer.predict_sd(l, i)); break; }
        case 'L': std::printf("%.17g\n", cmd[1] == 'A' ? g_infer.lambda_arr() : g_infer.lambda_dr()); break;
        case 'P': { double d; if (std::scanf("%lf", &d) != 1) return 1; std::printf("%.17g\n", cmd[1] == 'W' ? g_infer.p_wait_arr(d) : g_infer.p_alive(d)); break; }
        case 'R': { int a; double h; if (std::scanf("%d %lf", &a, &h) != 2) return 1; std::printf("%.17g\n", g_infer.r_hat(a, h)); break; }
        case 'W': {
            if (cmd[1] == 's') { if (std::scanf("%d", &g_w_m) != 1) return 1; }
            else if (cmd[1] == 'j') {
                int j = 0; int cut, lin;
                if (std::scanf("%d %d %d", &j, &cut, &lin) != 3) return 1;
                g_w_cut[j] = cut; g_w_lin[j] = lin;
            } else if (cmd[1] == 'e') {
                double se = 0.0;
                const double tw = g_infer.total_work(g_w_cut, g_w_lin, g_w_m, &se);
                std::printf("%.17g %.17g\n", tw, se);
            }
            break;
        }
        case 'H': {
            double pa, lm, c1, pc, nm, rs;
            if (std::scanf("%lf %lf %lf %lf %lf %lf", &pa, &lm, &c1, &pc, &nm, &rs) != 6) return 1;
            std::printf("%d\n", sb05::Infer::hold_dpre(pa, lm, c1, pc, nm, rs) ? 1 : 0);
            break;
        }
        default: return 1;
        }
    }
    std::fflush(stdout);
    return 0;
}
#endif
