#include <bits/stdc++.h>
using namespace std;
// Minimal mirror of State/table for checking exP vs brute
struct PL {vector<int> bs; vector<double> vs; double get(int m) const{
 if(bs.empty()) return 1.0; if(m<=bs.front()) return vs.front(); if(m>=bs.back()) return vs.back();
 int lo=0,hi=bs.size(); while(lo<hi){int mid=(lo+hi)/2; if(bs[mid]<=m) lo=mid+1; else hi=mid;}
 int hi2=lo,lo2=lo-1; double f=double(m-bs[lo2])/double(bs[hi2]-bs[lo2]); return vs[lo2]+f*(vs[hi2]-vs[lo2]);}
};
struct Cfg{double S=1,lat=2,beta=0; int K=1;};
double roundP(int m,vector<int> sp,Cfg c,PL dpre,PL dproc,PL dpost){
 int k=0; double mx=0; bool h=false; for(int x:sp) if(x>0){++k; double v=c.S+dproc.get(x); if(!h||v>mx){mx=v;h=true;}}
 double tl=2*c.S+dpre.get(m)+dpost.get(m); double tx=k*c.lat+c.beta*m; double tc=h?mx:0;
 double p=tl; if(tc>p) p=tc; if(tx>p) p=tx; return p;
}
vector<vector<int>> enumSplits(int K,int m){
 vector<vector<int>> out; vector<int> cur(K,0);
 function<void(int,int)> rec=[&](int idx,int rem){ if(idx==K-1){cur[idx]=rem; out.push_back(cur); return;} for(int v=0;v<=rem;++v){cur[idx]=v; rec(idx+1,rem-v);} };
 rec(0,m); return out;
}
vector<int> bruteOpt(int m,Cfg c,PL dpre,PL dproc,PL dpost){
 auto all=enumSplits(c.K,m); double best=1e300; vector<int> ans; int bk=1e9;
 for(auto &sp:all){double p=roundP(m,sp,c,dpre,dproc,dpost); int k=0; for(int x:sp) if(x>0) ++k; if(p<best-1e-12 || (fabs(p-best)<=1e-12 && k<bk)){best=p; ans=sp; bk=k;}}
 return ans;
}
// our exP logic (copied from policies.cpp)
double exP(int m,Cfg c,PL dpre,PL dproc,PL dpost){
 if(m<=0) return 0; if(m>=4097) m=4096;
 double S=c.S,lat=c.lat,be=c.beta; int K=c.K; if(K<1)K=1;
 double tl=2*S+dpre.get(m)+dpost.get(m);
 // mono check via listed
 bool mono=true;
 // build listed from PL
 // PL already has bs/vs sorted
 for(size_t i=1;i<dproc.bs.size();++i) if(dproc.vs[i]+1e-12 < dproc.vs[i-1]) mono=false;
 if(mono||m>32){
  double b=1e300; int km=K<m?K:m; for(int k=1;k<=km;++k){int cc=(m+k-1)/k; double tc=S+dproc.get(cc); double tx=k*lat+be*m; double p=tl; if(tc>p)p=tc; if(tx>p)p=tx; if(p<b)b=p;} return b;
 }
 double vs[32]; int vn=0; for(int cc=1;cc<=m;++cc) vs[vn++]=S+dproc.get(cc);
 sort(vs,vs+vn); int un=0; for(int i=0;i<vn;++i) if(!i||fabs(vs[i]-vs[un-1])>1e-12) vs[un++]=vs[i];
 double best=1e300; int km=K<m?K:m;
 for(int k=1;k<=km;++k){double tx=k*lat+be*m; int lo=0,hi=un-1,ans=-1;
  while(lo<=hi){int mid=(lo+hi)/2; double T=vs[mid]; bool al[33]={0};
   for(int cc=1;cc<=m;++cc) if(S+dproc.get(cc)<=T+1e-12) al[cc]=true;
   bool dp[9][33]={0}; dp[0][0]=true;
   for(int i=0;i<k;++i) for(int su=0;su<=m;++su) if(dp[i][su]) for(int cc=1;cc+su<=m;++cc) if(al[cc]) dp[i+1][su+cc]=true;
   if(dp[k][m]){ans=mid;hi=mid-1;} else lo=mid+1;
  }
  if(ans<0) continue; double tc=vs[ans]; double p=tl; if(tc>p)p=tc; if(tx>p)p=tx; if(p<best) best=p;
 }
 if(best>1e299){double b=1e300; int km2=K<m?K:m; for(int k=1;k<=km2;++k){int cc=(m+k-1)/k; double tc=S+dproc.get(cc); double tx=k*lat+be*m; double p=tl; if(tc>p)p=tc; if(tx>p)p=tx; if(p<b)b=p;} return b;}
 return best;
}
int main(){
 mt19937 rng(20260823);
 int passed=0,failed=0;
 for(int i=0;i<1200;++i){
  Cfg c; c.K=1+rng()%4; int m=1+rng()%12; c.S=1+ (rng()%900)/100.0; c.lat=0.5+ (rng()%450)/100.0; c.beta=0.0005+ (rng()%2000)/100000.0;
  auto mkTbl=[&](int mm)->PL{
   int n=2+rng()%3; vector<int> bss; for(int j=0;j<n;++j) bss.push_back(1+rng()%(mm+3)); sort(bss.begin(),bss.end()); bss.erase(unique(bss.begin(),bss.end()),bss.end()); if(bss[0]!=1) bss[0]=1;
   vector<pair<int,double>> lst;
   int sh=rng()%6; double base;
   for(int idx=0;idx<(int)bss.size();++idx){
    int bs=bss[idx]; double v;
    if(sh==0){double a=0.1+ (rng()%150)/100.0, b0=1+ (rng()%400)/100.0; v=a*bs+b0;}
    else if(sh==1){v=sqrt(bs)*(2+ (rng()%300)/100.0);}
    else if(sh==2){v=pow(bs,1.5)*(0.05+ (rng()%150)/1000.0);}
    else if(sh==3){v=5+ (rng()%100)/1000.0;}
    else if(sh==4){
     if(idx==0) v=8+ (rng()%400)/100.0;
     else if(idx== (int)bss.size()-1) v=2+ (rng()%300)/100.0;
     else v=8+ (rng()%600)/100.0;
    } else v=1+ (rng()%900)/100.0;
    v=max(0.5,v); lst.emplace_back(bs,v);
   }
   sort(lst.begin(),lst.end()); PL p; for(auto &pr:lst){p.bs.push_back(pr.first); p.vs.push_back(pr.second);} return p;
  };
  PL dpre=mkTbl(m), dpost=mkTbl(m), dproc=mkTbl(m);
  // need dproc shape controlled for coverage
  double p1=exP(m,c,dpre,dproc,dpost);
  auto br=bruteOpt(m,c,dpre,dproc,dpost);
  double p2=roundP(m,br,c,dpre,dproc,dpost);
  if(fabs(p1-p2)>1e-9){printf("FAIL %d K=%d m=%d p1=%.6f p2=%.6f\n",i,c.K,m,p1,p2); ++failed; if(failed>5) break;}
  else ++passed;
 }
 printf("C++ exP vs brute: %d/1200 passed %d failed %s\n",passed,failed, failed==0?"ALL PASS":"FAIL");
 return failed==0?0:1;
}
