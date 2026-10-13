#include <bits/stdc++.h>
using namespace std;
// Simplified decode table: piecewise linear get(m)
struct PL { vector<int> bs; vector<double> vs;
 double get(int m) const{
  if(bs.empty()) return 1;
  if(m<=bs.front()) return vs.front();
  if(m>=bs.back()) return vs.back();
  int lo=0,hi=bs.size();
  while(lo<hi){int mid=(lo+hi)/2; if(bs[mid]<=m) lo=mid+1; else hi=mid;}
  int i_hi=lo,i_lo=lo-1;
  if(bs[i_lo]==m) return vs[i_lo];
  double f=double(m-bs[i_lo])/double(bs[i_hi]-bs[i_lo]);
  return vs[i_lo]+f*(vs[i_hi]-vs[i_lo]);
 }
};
// Our P2 logic: balanced per k
double Popt_balanced(int m,double S,double lat,double beta,int K,const PL& dpre,const PL& dproc,const PL& dpost){
 double best=1e300;
 int kmax=min(K,m);
 for(int k=1;k<=kmax;++k){
   double tl=2*S+dpre.get(m)+dpost.get(m);
   int c=(m+k-1)/k;
   double tc=S+dproc.get(c);
   double tx=k*lat+beta*m;
   double p=max({tl,tc,tx});
   if(p<best) best=p;
 }
 return best;
}
// Exhaustive logic: enumerate all splits (ordered compositions) compute P as max over clouds
void enum_rec(int K,int m,int idx,vector<int>& cur,vector<vector<int>>& out){
 if(idx==K-1){cur[idx]=m; out.push_back(cur); return;}
 for(int v=0;v<=m;++v){cur[idx]=v; enum_rec(K,m-v,idx+1,cur,out);}
}
double Popt_brute(int m,double S,double lat,double beta,int K,const PL& dpre,const PL& dproc,const PL& dpost){
 if(K==1){
   double tl=2*S+dpre.get(m)+dpost.get(m);
   double tc=S+dproc.get(m);
   double tx=lat+beta*m;
   return max({tl,tc,tx});
 }
 vector<vector<int>> all; vector<int> cur(K,0);
 enum_rec(K,m,0,cur,all);
 double best=1e300;
 for(auto &sp:all){
   int k=0; double maxc=0; bool has=false;
   for(int c:sp) if(c>0){++k; double v=S+dproc.get(c); if(!has||v>maxc){maxc=v; has=true;}}
   if(k==0) continue;
   double tl=2*S+dpre.get(m)+dpost.get(m);
   double tc=has?maxc:0;
   double tx=k*lat+beta*m;
   double p=max({tl,tc,tx});
   if(p<best) best=p;
 }
 return best;
}
// m_cap via Pgap = ceil(pool/m)*Popt
int mcap_bal(int pool,double slo2,double S,double lat,double beta,int K,const PL& dpre,const PL& dproc,const PL& dpost){
 if(pool<=0) return 1;
 int best=0;
 for(int m=1;m<=pool;++m){
   double popt=Popt_balanced(m,S,lat,beta,K,dpre,dproc,dpost);
   double reps=ceil((double)pool/(double)m);
   double gap=reps*popt;
   if(gap<=slo2+1e-9) best=m;
 }
 return best?best:1;
}
int mcap_brute(int pool,double slo2,double S,double lat,double beta,int K,const PL& dpre,const PL& dproc,const PL& dpost){
 if(pool<=0) return 1;
 int best=0;
 for(int m=1;m<=pool;++m){
   double popt=Popt_brute(m,S,lat,beta,K,dpre,dproc,dpost);
   double reps=ceil((double)pool/(double)m);
   double gap=reps*popt;
   if(gap<=slo2+1e-9) best=m;
 }
 return best?best:1;
}
int main(){
 mt19937 rng(20260823);
 int N=10000; int fails=0;
 for(int t=0;t<N;++t){
   double S=uniform_real_distribution<double>(1,10)(rng);
   double lat=uniform_real_distribution<double>(0.5,5)(rng);
   double beta=uniform_real_distribution<double>(0.0005,0.02)(rng);
   int K=uniform_int_distribution<int>(1,4)(rng);
   int pool=uniform_int_distribution<int>(1,12)(rng);
   double slo2=uniform_real_distribution<double>(5,100)(rng);
   // random tables: 2-4 breakpoints, families linear etc. generate piecewise linear via random points
   auto genPL=[&]()->PL{
     int nb=uniform_int_distribution<int>(2,4)(rng);
     PL p;
     vector<int> bs; vector<double> vs;
     int cur=1;
     for(int i=0;i<nb;++i){
       int nxt=cur+uniform_int_distribution<int>(1,5)(rng);
       double v=uniform_real_distribution<double>(0.5,10)(rng);
       bs.push_back(cur); vs.push_back(v);
       cur=nxt;
       if(cur>50) break;
     }
     // sort
     p.bs=bs; p.vs=vs;
     return p;
   };
   PL dpre=genPL(), dproc=genPL(), dpost=genPL();
   // Also test V shape: make dproc V with valley at large m
   if(t%5==0){
     // V: 10 at 1, 2 at 20, 8 at 40
     dproc.bs={1,20,40}; dproc.vs={10,2,8};
   }
   int mc_b=mcap_bal(pool,slo2,S,lat,beta,K,dpre,dproc,dpost);
   int mc_r=mcap_brute(pool,slo2,S,lat,beta,K,dpre,dproc,dpost);
   if(mc_b!=mc_r){
     fails++;
     if(fails<5){
       cerr<<"mismatch t="<<t<<" pool="<<pool<<" K="<<K<<" S="<<S<<" lat="<<lat<<" beta="<<beta<<" slo2="<<slo2<<" bal="<<mc_b<<" brute="<<mc_r<<endl;
       for(int m=1;m<=pool;++m){
         double pb=Popt_balanced(m,S,lat,beta,K,dpre,dproc,dpost);
         double pr=Popt_brute(m,S,lat,beta,K,dpre,dproc,dpost);
         if(fabs(pb-pr)>1e-9) cerr<<" m="<<m<<" pb="<<pb<<" pr="<<pr<<endl;
       }
     }
   }
 }
 cout<<"Brute 1e4: fails="<<fails<<" "<<(fails==0?"PASS":"FAIL")<<endl;
 return fails?1:0;
}
