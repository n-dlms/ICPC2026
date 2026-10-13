// =============================================================================
// fwsim.cpp — implementations for the SB-program research scaffold (dev only).
//
// tdr_urgency_refresh / gov_sample — per-frame refresh of the urgency flag
// and the CPU-governor level (SB09: 10 s → level 1, 12 s → level 0).
//
// v_e_hat (P4) — terminal throughput value: survival-conditioned token count
// N_hat and makespan estimate E_hat over unfinished requests, cached on a
// 64-frame key so the O(R) scan amortizes and stays deterministic.
//
// rollout_should_hold (P4) — the SB08 §2 exact hold inequality with the
// direct veto layers (TDR pressure, ready>=2, endgame, wake safety).
//
// cliff_refresh (P7) — the computed binary-cliff controller: Hoeffding
// reserves (δ=0.01 → √(ln 100 / 2) ≈ 1.517427) on the arrived cohort decide
// whether the SAFE behavioral contract latches this frame, and the pooled
// gap-feasibility threshold g_cliff_sr prices the remaining TPOT budget
// into every decode group size.
// =============================================================================

#include "fwsim.hpp"
#include "state.hpp"

namespace icpc2026 {
bool g_force_no_hold=false;
bool g_tdr_urgency=false;
void tdr_urgency_refresh(const State& s,double){
 const Config& cg=s.cfg();
 g_tdr_urgency=(cg.dist_base>0.0&&cg.w_c>0.0&&s.oracle().excess_tdr(cg.SLO1)>0.15*cg.dist_base);
}
static Gov g_gov;
int gov_level(){ return g_gov.level(); }
void gov_sample(long long fc){ g_gov.sample(fc); }
#ifndef PSGO_STATIC
long long g_drift_mismatch=0;
void drift_check(const State& s,double t){ (void)s; (void)t; }
#endif

// ---- P4: survival-conditioned terminal V_E (frame-keyed cache) ----
// O(R) scan, refreshed at most every 64 frames (deterministic: keyed to the
// frame counter, never to wall clock). g_hf = frame the snapshot was taken.
// P7: also stashes g_gl = Σ_unfinished max(0, Lhat−iter−(iter==0?1:0)) — the
// predicted count of FUTURE pooled gaps (a request's next token opens its
// span and is not a gap; every later token is).
static double g_nh=0.0,g_eh=1.0;
long long g_gl=0;
static long long g_hf=-1;
static void horizon_refresh(const State& s,double t){
  double nh=s.oracle().tokens_out;
  double tail=0.0;
  long long gl=0;
  const int nr=s.num_requests();
  for(int i=0;i<nr;++i){
    if(!s.has_request(i)) continue;
    const Request& r=s.request(i);
    if(r.finished) continue;
    double rem=(double)s.predict_lout(i)-(double)r.iter;
    if(rem<0.0) rem=0.0;
    nh+=rem;
    if(rem>tail) tail=rem;
    const double extra=(r.iter==0)?1.0:0.0;
    if(rem>extra) gl+=(long long)(rem-extra);
  }
  const Config& cg=s.cfg();
  double base=t-s.oracle().earliest_arrival;
  if(!(base>0.0)) base=0.0;
  int lb=(int)(s.ewma_lin()+0.5); if(lb<1) lb=1;
  // per-token chain cost at the traffic's average Lin (solo round + transfers)
  const double pg=3.0*cg.S+s.table().get(Step::DECODE_PRE,1)
      +s.table().get(Step::DECODE_PROC,1)+s.table().get(Step::DECODE_POST,1)
      +2.0*transfer_time(cg,1.0);
  g_nh=nh;
  g_eh=base+tail*pg;
  g_gl=gl;
}
static void horizon_sync(const State& s,double t){
  const long long fc=g_gov.fc;
  if(g_hf<0 || (fc&63)==0){ if(g_hf!=fc){ horizon_refresh(s,t); g_hf=fc; } }
}
double v_e_hat(const State& s,double t){
  horizon_sync(s,t);
  const Config& cg=s.cfg();
  if(cg.tp_UB<=cg.tp_base||cg.w_tp<=0.0||!(g_eh>0.0)||g_nh<=0.0) return 0.0;
  const double x=g_nh/g_eh;
  if(x<cg.tp_base||x>=cg.tp_UB) return 0.0;
  return 1000.0*cg.w_tp*g_nh/((cg.tp_UB-cg.tp_base)*g_eh*g_eh);
}

// P4 hold rule (SB08 §2 with exact objects), with the direct veto layers
// applied:
//   + g_tdr_urgency / local tdr_critical early-outs (TDR pressure vetoes holds),
//   + ready >= 2 => false (holding is considered only when EXACTLY ONE request
//     is decode-ready),
//   + pcrit includes BOTH exact finite delay costs (ppost AND span).
bool rollout_should_hold(const State& s,double t,int lvl){
  if(g_force_no_hold) return false;
  if(lvl==0) return false;
  if(g_tdr_urgency) return false;
  const bool tdr_critical=(s.cfg().dist_base>0.0&&s.cfg().w_c>0.0&&
      s.oracle().excess_tdr(s.cfg().SLO1)>0.15*s.cfg().dist_base);
  if(tdr_critical) return false;
  int ready=s.count_phase(Phase::READY_D_PRE);
  if(ready<1) return false;
  if(ready>=2) return false;
  if(s.multi_token_dpre_count()>0) return false;
  int pipe=s.count_phase(Phase::WAIT_INPUT_DOWN)+s.count_phase(Phase::P_PROC_RUNNING);
  if(pipe==0) return false;
  if(is_endgame(s,t)) return false;
  const Config& cg=s.cfg();
  double ve=v_e_hat(s,t);
  if(!(ve>0.0)) return false;
  double dl=s.down().tail-t; if(dl<0.0) dl=0.0;
  int lb=(int)(s.ewma_lin()+0.5); if(lb<1) lb=1;
  const double D=dl+cg.S+s.table().get(Step::PREFILL_POST,lb);
  if(!(D>0.0)) return false;
  const double gain=ve*(2.0*cg.S+s.table().get(Step::DECODE_PRE,1)
                        +s.table().get(Step::DECODE_POST,1))*s.util_e(t);
  const double pcrit=s.util_c(t)*ve*D
      +s.oracle().delay_cost_ppost(cg.w_c,cg.dist_base,cg.SLO1,cg.SLO2,
                                   ready,D,std::max(1,s.num_requests()-s.oracle().count_tdr))
      +s.oracle().delay_cost_span(cg.w_c,cg.dist_base,cg.SLO1,cg.SLO2,
                                  ready,D,(int)s.oracle().total_gaps);
  return gain>pcrit;
}

// ---- P7: computed binary-cliff controller (SB08 §3–§4) --------------------
// Slack accounting on the arrived cohort (SB08 §3.5 horizon-free conditioning:
// R = requests ARRived so far; tdr closes at P_POST, so n_undone = R−count_tdr
// is exact). Central projection mean-matches the realized pooled tdr with a
// solo-chain floor at the traffic's EWMA Lin; floor projection = bare chain.
// Hoeffding per-sample reserve r·√(ln(1/δ)/(2n)); hf = √(ln(100)/2), δ=0.01
// (SB08 §3.3 default risk budget — derived, not tuned). SAFE ⇔ the tdr channel
// is thin-but-winnable; a FLOOR projection beyond −reserve is doomed and never
// latches SAFE (the computed successor of the D8/D9 flips). Trim tier note
// (P7 ledger): the tpot-side SAFE *trigger* was dropped first per ladder —
// tpot stays protected continuously through the g_cliff_sr group-size
// threshold, which is the pooled-gap lever; SAFE latching is tdr-driven.
bool g_cliff_safe=false;
double g_cliff_sr=0.0;
int cliff_srpt(const State& s){
  int best=-1; double bg=1e300;
  for(int c=0;c<s.num_clouds();++c)
    for(int rid=s.rl_head(Phase::READY_D_PRE,c);rid!=-1;rid=s.request(rid).next_ready){
      double rem=(double)s.predict_lout(rid)-(double)s.request(rid).iter;
      if(rem<1.0) rem=1.0;
      if(rem<bg||(rem==bg&&rid<best)){bg=rem;best=rid;}
    }
  return best;
}
void cliff_refresh(const State& s,double t){
  const Config& cg=s.cfg();
  if(!(cg.w_c>0.0)||cg.dist_base>0.0||!(cg.SLO1>0.0)){
    if(g_cliff_safe){g_cliff_safe=false;g_force_no_hold=false;}
    g_cliff_sr=0.0;
    return;
  }
  horizon_sync(s,t);
  const auto& o=s.oracle();
  const int R=s.num_requests();
  int nu=R-o.count_tdr; if(nu<0) nu=0;
  bool safe=false;
  int lb=(int)(s.ewma_lin()+0.5); if(lb<1) lb=1; if(lb>4096) lb=4096;
  const TaskTimeTable& tb=s.table();
  const double fl=3.0*cg.S+tb.get(Step::PREFILL_PRE,lb)
      +tb.get(Step::PREFILL_PROC,lb)+tb.get(Step::PREFILL_POST,lb)
      +2.0*transfer_time(cg,(double)lb);
  // --- tdr channel (only while pending P_POSTs can still move the mean) ---
  if(nu>0){
    const double rd=(double)R;
    const double res=fl*1.517427/std::sqrt((double)nu);   // √(ln(100)/2): δ=0.01
    const double hC=cg.SLO1-(o.sum_tdr+(double)nu*std::max(o.projected_tdr(),fl))/rd;
    const double hF=cg.SLO1-(o.sum_tdr+(double)nu*fl)/rd;
    if(!(hF<-res)&&hC<res) safe=true;
  }
  // --- tpot budget for the decode gap-feasibility threshold ---
  // Same doctrine as the latch: the tightened budget engages only under
  // genuine statistical thinness (pC below its Hoeffding reserve); healthy
  // slack keeps the raw-SLO2 leader path byte-identical.
  const long long gl=g_gl;
  const long long G=o.total_gaps+gl;
  double pC=-1e300, resp=0.0;
  if(G>0&&gl>0){
    const double rf=3.0*cg.S+tb.get(Step::DECODE_PRE,1)
        +tb.get(Step::DECODE_PROC,1)+tb.get(Step::DECODE_POST,1)
        +2.0*transfer_time(cg,1.0);
    pC=cg.SLO2-((double)o.sum_gaps+(double)gl*std::max(o.projected_tpot(),rf))
       /(double)G;
    resp=rf*1.517427/std::sqrt((double)gl);
  }
  g_cliff_sr=(pC>1e-12&&pC<resp)?pC:0.0;
  if(g_cliff_safe!=safe){ g_cliff_safe=safe; g_force_no_hold=safe; }
}
}
