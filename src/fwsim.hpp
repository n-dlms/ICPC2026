// =============================================================================
// fwsim.hpp — research scaffold for the SB-program computation modules.
//
// Dev-build only; NOT part of the generated submission artifact (excluded
// from UNITS in tools/merge_submission.py). Contains:
//   FwSim     — the lean forward-simulation core (undo journal, O(K) event
//               scan) designed in SB02 and verified in SB03;
//   Gov       — CPU governor (SB09): watches elapsed wall time and degrades
//               search levels near the TLE budget;
//   P4 hooks  — terminal-value helpers (v_e_hat, survival-conditioned
//               remaining-work estimate) used by the exact hold rule;
//   P7 hooks  — the computed binary-cliff controller (cliff_refresh,
//               g_cliff_safe / g_cliff_sr, SRPT pick) derived in SB08.
// The shipped artifact implements the equivalent decisions with the
// closed-form oracles in state.hpp/policies.cpp; fwsim hosts the
// computation-driven replacements for post-contest research.
// =============================================================================
#ifndef ICPC2026_FWSIM_HPP
#define ICPC2026_FWSIM_HPP

#include "state.hpp"
#include <vector>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cstring>
namespace icpc2026 {
struct FwSim {
 double lfree=0;
 double cfree[8]={0,0,0,0,0,0,0,0};
 struct Task{bool b=false; double f=0; double du=0; int st=0; int sq=0; int ls=0; int le=0; int off=0; int n=0;};
 Task lt;
 Task ct[8];
 struct Xf{double f=0; int l=0; int o=0; int n=0; int rc=0; bool pr=false;};
 std::vector<Xf> up,dn;
 int uph=0,dnh=0;
 std::vector<int> ar;
 struct US{void* p; uint64_t o; uint8_t w;};
 std::vector<US> js;
 struct UV{void* p; size_t n; char t;};
 std::vector<UV> jv;
 static constexpr double EPS=1e-9;
 void anchor(const State& s,double t){
   lfree=s.local().busy?s.local().busy_until:t;
   for(int k=0;k<s.num_clouds()&&k<8;++k) cfree[k]=s.cloud(k).busy?s.cloud(k).busy_until:t;
   lt.b=s.local().busy; lt.f=s.local().busy_until; lt.st=(int)s.local().step;
   for(int k=0;k<s.num_clouds()&&k<8;++k){ ct[k].b=s.cloud(k).busy; ct[k].f=s.cloud(k).busy_until; ct[k].st=(int)s.cloud(k).step; }
   up.clear(); dn.clear(); uph=dnh=0; ar.clear(); js.clear(); jv.clear();
   up.reserve(64); dn.reserve(64); ar.reserve(256); js.reserve(1024); jv.reserve(256);
 }
 double nxt() const{
   double b=1e300;
   if(lt.b&&lt.f<b) b=lt.f;
   for(int k=0;k<8;++k) if(ct[k].b&&ct[k].f<b) b=ct[k].f;
   if(uph<(int)up.size()&&up[uph].f<b) b=up[uph].f;
   if(dnh<(int)dn.size()&&dn[dnh].f<b) b=dn[dnh].f;
   return b;
 }
 void dlv(double tau){
   (void)tau;
   if(uph<(int)up.size()&&fabs(up[uph].f-tau)<EPS) ++uph;
   if(dnh<(int)dn.size()&&fabs(dn[dnh].f-tau)<EPS) ++dnh;
 }
 void adv(double T){
   for(;;){ double tau=nxt(); if(!(tau<T+EPS)) break; dlv(tau); }
 }
 struct Mk{size_t s; size_t v;};
 Mk mark() const {return {js.size(), jv.size()};}
 void rollback(Mk m){
   while(js.size()>m.s){ auto &e=js.back(); memcpy(e.p,&e.o,e.w); js.pop_back(); }
   while(jv.size()>m.v){ auto &e=jv.back(); if(e.t==0) ((std::vector<int>*)e.p)->resize(e.n); else ((std::vector<Xf>*)e.p)->resize(e.n); jv.pop_back(); }
 }
 template<class T> void jw(T* p,T v){ US e; e.p=(void*)p; e.w=sizeof(T); e.o=0; memcpy(&e.o,p,sizeof(T)); js.push_back(e); *p=v; }
 void jpi(int x){ jv.push_back({(void*)&ar, ar.size(),0}); ar.push_back(x); }
 void jpx(bool u,Xf x){ jv.push_back({u?(void*)&up:(void*)&dn, u?up.size():dn.size(),1}); (u?up:dn).push_back(x); }
};
struct Gov{
 std::chrono::steady_clock::time_point t0=std::chrono::steady_clock::now();
 int lvl=1;
 long long fc=0;
 void sample(long long c){
   fc=c;
   if((c&1023)==0){
     auto now=std::chrono::steady_clock::now();
     auto d=now-t0;
     double el= (double)d.count()*1e-9;
     if(el>10.0) lvl=1;
     if(el>12.0) lvl=0;
   }
 }
 int level() const {return lvl;}
};
inline bool is_endgame(const State& s,double now){
 double silence=now-s.last_arr();
 if(silence<0) silence=0;
 double avg_gap=50.0;
 int n=s.num_requests();
 if(n>1){
   double tot=s.last_arr()-s.oracle().earliest_arrival;
   if(tot>1e-9) avg_gap=tot/ (double)std::max(1,n-1);
   else avg_gap=50.0;
 }
 double p_alive = silence>0? exp(-silence/ std::max(1.0,avg_gap)):1.0;
 return p_alive<0.05;
}
inline int drain_star(const State& s){
 int best=-1; double best_cp=-1e300;
 for(int c=0;c<s.num_clouds();++c){
   for(int rid=s.rl_head(Phase::READY_D_PRE,c); rid!=-1; rid=s.request(rid).next_ready){
     const Request& r=s.request(rid);
     double rem = 0;
     if(r.phase==Phase::READY_D_PRE){
       int est = s.predict_lout(rid);
       int remTok = est - r.iter;
       if(remTok<1) remTok=1;
       double pg = s.table().get(Step::DECODE_PRE,1)+s.table().get(Step::DECODE_POST,1)+s.table().get(Step::DECODE_PROC,1);
       rem = remTok*pg;
     }
     if(rem>best_cp){ best_cp=rem; best=rid; }
   }
 }
 if(best>=0) return best;
 return -1;
}
inline int drain_mmax(const State& s,int pool){
 (void)s; (void)pool;
 return pool;
}
extern bool g_force_no_hold;
// TDR-urgency veto. Refreshed once per frame next to
// cliff_refresh/gov_sample; true => holds are vetoed and D_PRE groups go m=1.
extern bool g_tdr_urgency;
void tdr_urgency_refresh(const State&,double);
bool rollout_should_hold(const State& s,double t,int lvl);
int gov_level();
void gov_sample(long long fc);
// P4 terminal value: survival-conditioned V_E at projected finals.
// N_hat = tokens_out + sum_r max(0, Lout_hat_r - iter_r) over unfinished
// requests (the deployed bucket-mean Lout estimator; NOT the full SB05
// posterior); E_hat = (t - earliest_arrival) + tail * per-token chain cost.
// Refreshed from the live State at most every 64 frames (frame-keyed =>
// deterministic); the ramp/clamp region gates are the exact ones.
double v_e_hat(const State& s,double t);
// ---- P7 (SB08 §3–§4): computed binary-cliff controller --------------------
// Replaces the hand-tuned D8 traffic-aware flip / D9 startup alpha early-out.
// δ=0.01 and the Hoeffding reserve r·√(ln(1/δ)/(2n)) are DERIVED (SB08 §3.3,
// distribution-free bound), not tuned per-test. cliff_refresh runs once per
// frame; while SAFE the behavioral contract holds: prefill absolute priority
// (structural), no holds (g_force_no_hold), single-piece chunking, SRPT pop
// order, and decode groups sized inside the remaining-SLO2-margin gap-feasible
// set (group_size threshold = g_cliff_sr). Doom valve: a channel whose floor
// projection sits beyond −reserve never latches SAFE (nothing left to protect;
// throughput owns hopeless rows — escape hatch against permanent SAFE).
// Fixed-order accumulators only => deterministic.
void cliff_refresh(const State&,double);
extern bool g_cliff_safe;   // SAFE contract active this frame
extern double g_cliff_sr;   // remaining pooled-gap budget (ms/gap); 0 = inactive
int cliff_srpt(const State&);  // SRPT pick over READY_D_PRE (-1 if none)
#ifndef PSGO_STATIC
extern long long g_drift_mismatch;
void drift_check(const State& s,double t);
#endif
}
#endif
