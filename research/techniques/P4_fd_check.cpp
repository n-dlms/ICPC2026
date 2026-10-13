// P4_fd_check.cpp — standalone driver for the NEW State::Oracle exact
// calculus (src/state.hpp). Reads state lines on stdin, prints every swapped
// body so the Python side can finite-difference them against an independent
// implementation of problem.md:425-495.
//
// Line format (mode A):
//   A w_tp w_c tp_base tp_UB SLO1 SLO2 dist_base A E N tdr tpot G ndly ddly rhat ghat
// The aggregates are reconstructed so the Oracle's projected_* accessors see
// exactly (E,N,R=1,tdr,tpot,G): earliest_arrival=A, last_finish=A+E,
// tokens_out=N, sum_tdr=tdr with count_tdr=1, sum_gaps=tpot with
// total_gaps=(G>0?1:0).
#include "../../src/state.hpp"
#include <cstdio>

int main() {
    char mode = 'A';
    while (std::scanf(" %c", &mode) == 1) {
        if (mode != 'A') break;
        double w_tp, w_c, tp_base, tp_UB, SLO1, SLO2, dist_base;
        double A, E, N, tdr, tpot, G, ddly;
        long long ndly, rhat, ghat;
        if (std::scanf("%lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lld %lf %lld %lld",
                       &w_tp, &w_c, &tp_base, &tp_UB, &SLO1, &SLO2, &dist_base,
                       &A, &E, &N, &tdr, &tpot, &G, &ndly, &ddly, &rhat, &ghat) != 17)
            return 1;
        icpc2026::State::Oracle o;
        o.earliest_arrival = A;
        o.last_finish = A + E;
        o.tokens_out = static_cast<long long>(N);
        o.sum_tdr = tdr;
        o.count_tdr = 1;
        o.sum_gaps = tpot;
        o.total_gaps = (G > 0.0) ? 1 : 0;
        std::printf("%.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g\n",
                    o.marginal_tp_v(w_tp, tp_base, tp_UB),
                    o.marginal_tdr_v(w_c, dist_base, SLO1, SLO2),
                    o.marginal_tpot_v(w_c, dist_base, SLO1, SLO2),
                    o.projected_score(w_tp, w_c, tp_base, tp_UB, SLO1, SLO2, dist_base),
                    o.projected_tp(),
                    o.delay_cost_ppost(w_c, dist_base, SLO1, SLO2,
                                       static_cast<int>(ndly), ddly, static_cast<int>(rhat)),
                    o.delay_cost_span(w_c, dist_base, SLO1, SLO2,
                                      static_cast<int>(ndly), ddly, static_cast<int>(ghat)),
                    o.projected_cw_exact(SLO1, SLO2, dist_base));
    }
    return 0;
}
