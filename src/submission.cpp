// ICPC 2026 — single-file submission (strategy flags baked in).
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cmath>
#include <deque>
#include <queue>
#include <utility>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <chrono>
#define PART7_ADAPTIVE_REGIME
#define V11_PPRECAP
#define V12_MIRROR
#define V15_LINORDER
#define DPRE_ACCUMULATE
#define PSGO_STATIC
#define MECH_PRESSURE_PICK
#define MECH_DPRE_UNGATE
#define MECH_ROUNDTIME
namespace icpc2026 {
 constexpr double EPS = 1e-9;
 struct Cg {
  int K = 0;
  double S = 0.0;
  double lm = 0.0;
  double bandwidth_gbps = 0.0;
  int bpt = 0;
  int nl = 0;
  double s1 = 0.0, s2 = 0.0;
  double tp_UB = 0.0, tp_base = 0.0, v0 = 0.0;
  double w_tp = 0.0, w_c = 0.0;
 };
 inline double q4(const Cg& c, double len) {
  return c.lm + 8.0 * len * c.bpt / (c.bandwidth_gbps * 1e6);
 }
 enum class v2 : uint8_t { ARR, TDN, XDN, FIN };
 struct Event {
  v2 kind = v2::ARR;
  int rid = -1;
  int lin = 0;
  int server = -2;
  std::vector<std::string> spec;
  double dur = 0.0;
  bool up = false;
  int remote = -1;
  long long size = 0;
  bool pre = false;
  std::vector<int> ids;
 };
 class qe {
  public:
  qe();
  bool eof();
  std::string_view v1();
  int q9y();
  long long next_long_long();
  double q9x();
  private:
  static constexpr size_t BUF = 1 << 22;
  std::vector<char> buf_;
  size_t pos_;
  size_t valid_;
  bool fill();
 };
 class qf {
  public:
  explicit qf() = default;
  bool read_config(Cg& cfg);
  bool read_table(int& N, std::vector<std::vector<double>>& rows);
  bool next_frame(double& t, std::vector<Event>& events);
  private:
  qe in_;
 };
 enum class StepKind : uint8_t { P_PRE, P_PROC, P_POST, D_PRE, D_PROC, D_POST };
 struct TaskSpec {
  StepKind step = StepKind::P_PRE;
  int q13 = 0;
  int ls = 0, le = 0;
  std::vector<int> ids;
  bool parse(const std::vector<std::string>& tokens, const Cg& cfg);
  std::vector<std::string> tokens() const;
  static int parse_int(std::string_view v);
  static double parse_double(std::string_view v);
 };
 enum class Step : uint8_t {
  PP = 0,
  PR = 1,
  PQ = 2,
  DP = 3,
  DR = 4,
  DQ = 5
 };
 class qj {
  public:
  void load(int N, const std::vector<std::vector<double>>& rows);
  double get(Step step, int batch_size) const;
  int row_count() const { return static_cast<int>(rows_.size()); }
  int qb(Step step) const;
  const std::vector<std::pair<int, double>>& vz(Step step) const {
   return cols_[static_cast<size_t>(step)];
  }
  int decode_m_star(double S) const {
   if (m_star_S_ == S && m_star_ > 0) return m_star_;
   double best = 1e300;
   int star = 1;
   for (int m = 1; m <= 4096; ++m) {
    const double f = (2.0 * S + get(Step::DP, m)
    + get(Step::DQ, m)) / static_cast<double>(m);
    if (f < best) { best = f; star = m; }
   }
   m_star_S_ = S;
   m_star_ = star;
   return star;
  }
  private:
  mutable double m_star_S_ = -1.0;
  mutable int m_star_ = 0;
  std::vector<std::vector<double>> rows_;
  std::vector<std::vector<std::pair<int, double>>> cols_;
 };
 Step q14(StepKind k);
 StepKind kind_of(Step s);
 enum class Phase : uint8_t {
  ql,
  P_PRE_RUNNING,
  WAIT_INPUT_UP,
  qn,
  qy,
  qx,
  qr,
  P_POST_RUNNING,
  qk,
  D_PRE_RUNNING,
  WAIT_DEC_UP,
  qo,
  D_PROC_RUNNING,
  WAIT_DEC_DOWN,
  qs,
  D_POST_RUNNING,
  FINISHED
 };
 constexpr int v4 = 17;
 struct Request {
  int id = -1;
  int lin = 0;
  int q13 = -1;
  Phase phase = Phase::ql;
  int iter = 0;
  int next_ls = 0;
  int lout = 0;
  double arrival = 0.0;
  double ppost_t = -1.0;
  double first_token = -1.0, lt = -1.0;
  double ppre_start = -1.0, ppre_end = -1.0;
  double up_done = -1.0;
  double pproc_start = -1.0, pproc_end = -1.0;
  double down_done = -1.0;
  double ppost_start = -1.0;
  bool finished = false;
  bool stats_recorded = false;
  bool q5 = false;
  int pr = -1;
  int q11 = -1;
 };
 struct qc {
  bool q15 = false;
  double q10 = 0.0;
  std::vector<std::string> spec;
  StepKind step = StepKind::P_PRE;
  std::vector<int> ids;
 };
 struct qd {
  std::vector<double> qa;
  double tail = 0.0;
  void reserve(size_t n) { qa.reserve(n); }
  void push(double enter_t, double dur);
  void drain(double t);
  double head_finish() const { return qa.empty() ? 0.0 : qa.front(); }
  size_t in_flight() const { return qa.size(); }
 };
 class St {
  public:
  void init(const Cg& cfg);
  void apply_event(const Event& ev, double t);
  void end_frame();
  void issue(const TaskSpec& spec, double t);
  long long frames() const { return frames_; }
  Request& q12(int id) { return reqs_[id]; }
  const Request& q12(int id) const { return reqs_[id]; }
  bool has_request(int id) const { return id >= 0 && id < static_cast<int>(reqs_.size()) && seen_[id]; }
  int num_requests() const { return static_cast<int>(reqs_.size()); }
  const Cg& cfg() const { return qV; }
  const qj& tb() const { return qW; }
  void set_table(const qj& t) { qW = t; }
  qc& local() { return local_; }
  const qc& local() const { return local_; }
  qc& q13(int k) { return clouds_[k]; }
  const qc& q13(int k) const { return clouds_[k]; }
  int qY() const { return q7; }
  int qZ(int k) const { return cloud_load_[static_cast<size_t>(k)]; }
  double decode_est(int k) const { return cloud_decode_est_[static_cast<size_t>(k)]; }
  double vy() const { return el; }
  double vx() const { return el; }
  int predict_lout(int id) const { (void)id; return (int)std::lround(el); }
  double util_c(double t) const { (void)t; return 0.0; }
  double util_e(double t) const { (void)t; return 0.0; }
  static constexpr int qu = 4096;
  static constexpr int qm = qu + 1;
  struct vi {
   int head[qm];
   int tail[qm];
   int count[qm];
   int v9;
   int v8;
   void reset() {
    for (int i = 0; i < qm; ++i) { head[i] = tail[i] = -1; count[i] = 0; }
    v9 = -1;
    v8 = qm;
   }
  };
  vi& ppre() { return ppre_; }
  const vi& ppre() const { return ppre_; }
  inline static bool q8(Phase p) {
   return p == Phase::qn ||
   p == Phase::qk ||
   p == Phase::qo;
  }
  static int v7(Phase p) { return static_cast<int>(p); }
  int  qR(Phase p, int q13);
  int  qQ(Phase p, int q13) const;
  void vj(Phase p, int q13, int rid);
  void rr(int rid);
  int  multi_token_dpre_count() const { return q0; }
  int  qS(Phase p, int q13) const;
  bool qE0(Phase p, int q13) const { return qS(p, q13) == 0; }
  int  qT();
  void qU(int rid);
  int  q6(Phase p) const;
  qd& up() { return up_; }
  qd& down() { return down_; }
  const qd& up() const { return up_; }
  const qd& down() const { return down_; }
  size_t up_inflight_pre() const {
   const long long v = up_proj_pre_ - up_deq_pre_;
   return static_cast<size_t>(v < 0 ? 0 : v);
  }
  int dynamic_max_batch() const { return dynamic_max_batch_; }
  void set_dynamic_max_batch(int m) { dynamic_max_batch_ = m; }
  double e_busy() const { return e_busy_; }
  double c_busy() const { return c_busy_; }
  double up_busy() const { return up_busy_; }
  double down_busy() const { return down_busy_; }
  double last_arr() const { return last_arr_; }
  bool flat_decode_table() const { return flat_decode_table_; }
  void set_flat_decode_table(bool f) { flat_decode_table_ = f; }
  struct Oracle {
   double sum_tdr = 0.0;
   int ct = 0;
   double sum_gaps = 0.0;
   long long tg = 0;
   double q2 = 1e18;
   double q9 = 0.0;
   long long tokens_out = 0;
   double projected_tdr() const { return ct > 0 ? sum_tdr / ct : 0.0; }
   double projected_tpot() const { return tg > 0 ? sum_gaps / tg : 0.0; }
   double ptp() const {
    if (q9 <= q2) return 0.0;
    return tokens_out / (q9 - q2);
   }
   double et(double s1) const {
    if (s1 <= 0) return 0.0;
    return std::max(0.0, (projected_tdr() - s1) / s1);
   }
   double qAt(double s2) const {
    if (s2 <= 0) return 0.0;
    return std::max(0.0, (projected_tpot() - s2) / s2);
   }
   double ed(double s1, double s2) const {
    const double e_tdr = et(s1);
    const double e_tpot = qAt(s2);
    return std::sqrt(e_tdr * e_tdr + e_tpot * e_tpot);
   }
   double projected_cw_exact(double s1, double s2, double v0) const {
    const double dist = ed(s1, s2);
    if (v0 <= 0.0) return (dist == 0.0) ? 1.0 : 0.0;
    return std::max(0.0, 1.0 - dist / v0);
   }
   double normalized_tp(double tp_base, double tp_UB) const {
    if (tp_UB <= tp_base) return 0.0;
    return std::clamp((ptp() - tp_base) / (tp_UB - tp_base), 0.0, 1.0);
   }
   double projected_score(double w_tp, double w_c, double tp_base, double tp_UB,
   double s1, double s2, double v0) const {
    const double c_tp = normalized_tp(tp_base, tp_UB);
    const double c_w = projected_cw_exact(s1, s2, v0);
    return 1000.0 * (w_tp * c_tp + w_c * c_w);
   }
   double qB(double w_tp, double tp_base, double tp_UB) const {
    const double tp = ptp();
    if (tp_UB <= tp_base || tp <= tp_base || tp >= tp_UB) return 0.0;
    return 1000.0 * w_tp / (tp_UB - tp_base);
   }
   double qz(double w_c, double v0, double s1, double s2) const {
    const double e = et(s1);
    const double d = ed(s1, s2);
    if (w_c <= 0.0 || v0 <= 0.0 || d <= 0.0 || d >= v0 ||
    e <= 0.0 || s1 <= 0.0) return 0.0;
    return 1000.0 * w_c * e / (v0 * d * s1);
   }
   double qA(double w_c, double v0, double s1, double s2) const {
    const double e = qAt(s2);
    const double d = ed(s1, s2);
    if (w_c <= 0.0 || v0 <= 0.0 || d <= 0.0 || d >= v0 ||
    e <= 0.0 || s2 <= 0.0) return 0.0;
    return 1000.0 * w_c * e / (v0 * d * s2);
   }
   double rem_tp(double w_tp, double tp_base, double tp_UB) const {
    return 1000.0 * w_tp * (1.0 - normalized_tp(tp_base, tp_UB));
   }
   double rem_c(double w_c, double v0, double s1, double s2) const {
    const double d = ed(s1, s2);
    if (v0 <= 0.0) return 1000.0 * w_c * ((d > 0.0) ? 1.0 : 0.0);
    return 1000.0 * w_c * std::min(1.0, d / v0);
   }
   double delay_cost_ppost(double w_c, double v0, double s1, double s2, int ready, double D, int denom) const { (void)w_c;(void)v0;(void)s1;(void)s2;(void)ready;(void)D;(void)denom; return 0.0; }
   double delay_cost_span(double w_c, double v0, double s1, double s2, int ready, double D, int gaps) const { (void)w_c;(void)v0;(void)s1;(void)s2;(void)ready;(void)D;(void)gaps; return 0.0; }
  };
  Oracle& oracle() { return oracle_; }
  const Oracle& oracle() const { return oracle_; }
  double get_excess_tdr() const { return oracle_.et(qV.s1); }
  double get_excess_tpot() const { return oracle_.qAt(qV.s2); }
  struct Stats {
   double sum_tdr = 0.0;
   double sum_span = 0.0;
   long long tg = 0;
   double latest_token = 0.0, q2 = 1e18;
   long long tokens = 0;
  };
  Stats& stats() { return stats_; }
  void check_invariants(double t) const;
  private:
  Cg qV;
  qj qW;
  int q7 = 0;
  std::vector<Request> reqs_;
  long long frames_ = 0;
  std::vector<char> seen_;
  qc local_;
  std::vector<qc> clouds_;
  std::vector<int> cloud_load_;
  std::vector<double> cloud_decode_est_;
  std::vector<int> finish_pending_;
  qd up_, down_;
  long long up_proj_pre_ = 0, up_deq_pre_ = 0;
  long long up_proj_dec_ = 0, up_deq_dec_ = 0;
  vi ppre_;
  std::vector<int> g_head_, g_tail_, g_count_;
  std::vector<std::vector<int>> c_head_, c_tail_, c_count_;
  int q0 = 0;
  Stats stats_;
  Oracle oracle_;
  int dynamic_max_batch_ = -1;
  double e_busy_ = 0.0;
  double c_busy_ = 0.0;
  double up_busy_ = 0.0;
  double down_busy_ = 0.0;
  double last_arr_ = 0.0;
  bool flat_decode_table_ = false;
  double el = 10.0;
 };
 class vh {
  public:
  virtual ~vh() = default;
  virtual int choose(const St& s, const Request& r, double t) const = 0;
 };
 class PrefillLoadBalanceCloud : public vh {
  public:
  int choose(const St& s, const Request& r, double t) const override;
 };
 class qi {
  public:
  virtual ~qi() = default;
  virtual std::pair<int,int> np(const St& s, const Request& r, double t) const = 0;
 };
 class OnePieceChunk : public qi {
  public:
  std::pair<int,int> np(const St& s, const Request& r, double t) const override;
 };
 class AdaptiveChunk : public qi {
  public:
  std::pair<int,int> np(const St& s, const Request& r, double t) const override;
 };
 class qh {
  public:
  virtual ~qh() = default;
  virtual bool hd(const St& s, double t) const = 0;
 };
 class NoDelay : public qh {
  public:
  bool hd(const St& s, double t) const override;
 };
 class SL : public qh {
  public:
  bool hd(const St& s, double t) const override;
  void set_alpha(double a) { vD = a; }
  private:
  double vD = 1.0;
 };
 class vd {
  public:
  virtual ~vd() = default;
  virtual int gs(StepKind step, int vg, const St& s, double t) const = 0;
 };
 class SizeOneBatch : public vd {
  public:
  int gs(StepKind step, int vg, const St& s, double t) const override;
 };
 class qg : public vd {
  public:
  void precompute(const struct Cg& cfg, const class qj& tbl);
  int  gs(StepKind step, int vg, const St& s, double t) const override;
  void set_alpha(double a) { vD = a; }
  private:
  int vC[6] = {1, 1, 1, 1, 1, 1};
  double vD = 1.0;
 };
 struct Policies {
  const vh* q13 = nullptr;
  const qi* chunk = nullptr;
  const vd* batch = nullptr;
  const qh* delay = nullptr;
  bool qO = false;
  int qP = 3;
  double alpha = 1.0;
 };
 struct Assignment {
  TaskSpec spec;
 };
 class Se {
  public:
  Se(const Cg& cfg, const qj& tb, const Policies& policies)
  : qV(cfg), qW(tb), qX(policies) {}
  std::vector<Assignment> decide(St& st, double t);
  private:
  TaskSpec pick_local(St& st, double t);
  TaskSpec pick_cloud(St& st, int c, double t);
  const Cg& qV;
  const qj& qW;
  const Policies& qX;
 };
 qe::qe() : buf_(BUF), pos_(0), valid_(0) {}
 bool qe::fill() {
  if (pos_ > 0) {
   size_t left = valid_ - pos_;
   if (left > 0) std::memmove(buf_.data(), buf_.data() + pos_, left);
   valid_ -= pos_;
   pos_ = 0;
  }
  size_t cap = BUF - valid_;
  if (cap == 0) return false;
  for (;;) {
   ssize_t r = ::read(STDIN_FILENO, buf_.data() + valid_, cap);
   if (r > 0) { valid_ += static_cast<size_t>(r); return true; }
   if (r == 0) return false;
   if (errno != EINTR && errno != EAGAIN) return false;
  }
 }
 bool qe::eof() {
  if (pos_ < valid_) return false;
  return !fill();
 }
 std::string_view qe::v1() {
  for (;;) {
   while (pos_ < valid_) {
    char c = buf_[pos_];
    if (c != ' ' && c != '\n' && c != '\t' && c != '\r') break;
    ++pos_;
   }
   if (pos_ < valid_) break;
   if (!fill()) return {};
  }
  size_t start = pos_;
  for (;;) {
   while (pos_ < valid_) {
    char c = buf_[pos_];
    if (c == ' ' || c == '\n' || c == '\t' || c == '\r')
    return std::string_view(buf_.data() + start, pos_ - start);
    ++pos_;
   }
   size_t keep = valid_ - start;
   if (keep > 0) std::memmove(buf_.data(), buf_.data() + start, keep);
   pos_ = keep;
   valid_ = keep;
   size_t cap = BUF - valid_;
   if (cap == 0) return std::string_view(buf_.data(), valid_);
   ssize_t r = ::read(STDIN_FILENO, buf_.data() + valid_, cap);
   if (r <= 0) return std::string_view(buf_.data(), valid_);
   valid_ += static_cast<size_t>(r);
   start = 0;
  }
 }
 int qe::q9y() {
  std::string_view v = v1();
  int x = 0;
  size_t i = 0;
  bool neg = false;
  if (i < v.size() && v[i] == '-') { neg = true; ++i; }
  while (i < v.size() && v[i] >= '0' && v[i] <= '9') {
   x = x * 10 + (v[i] - '0');
   ++i;
  }
  return neg ? -x : x;
 }
 long long qe::next_long_long() {
  std::string_view v = v1();
  long long x = 0;
  size_t i = 0;
  bool neg = false;
  if (i < v.size() && v[i] == '-') { neg = true; ++i; }
  while (i < v.size() && v[i] >= '0' && v[i] <= '9') {
   x = x * 10 + (v[i] - '0');
   ++i;
  }
  return neg ? -x : x;
 }
 double qe::q9x() {
  std::string_view tok = v1();
  if (tok.empty()) return 0.0;
  return TaskSpec::parse_double(tok);
 }
 bool qf::read_config(Cg& cfg) {
  cfg.K = in_.q9y();
  cfg.S = in_.q9x();
  cfg.lm = in_.q9x();
  cfg.bandwidth_gbps = in_.q9x();
  cfg.bpt = in_.q9y();
  cfg.nl = in_.q9y();
  cfg.s1 = in_.q9x();
  cfg.s2 = in_.q9x();
  cfg.tp_UB = in_.q9x();
  cfg.tp_base = in_.q9x();
  cfg.v0 = in_.q9x();
  cfg.w_tp = in_.q9x();
  cfg.w_c = in_.q9x();
  return true;
 }
 bool qf::read_table(int& N, std::vector<std::vector<double>>& rows) {
  N = in_.q9y();
  if (N <= 0) return false;
  rows.assign(static_cast<size_t>(N), std::vector<double>(7));
  for (int i = 0; i < N; ++i)
  for (int j = 0; j < 7; ++j)
  rows[i][j] = in_.q9x();
  return true;
 }
 bool qf::next_frame(double& t, std::vector<Event>& events) {
  std::string_view first = in_.v1();
  if (first.empty()) return false;
  if (first == "END") return false;
  t = TaskSpec::parse_double(first);
  int e = in_.q9y();
  assert(e >= 0);
  events.clear();
  events.reserve(static_cast<size_t>(e));
  if (e == 0) {
   return true;
  }
  for (int i = 0; i < e; ++i) {
   Event ev;
   std::string_view kind = in_.v1();
   if (kind == "ARR") {
    ev.kind = v2::ARR;
    ev.rid = in_.q9y();
    ev.lin = in_.q9y();
   } else if (kind == "TDN") {
    ev.kind = v2::TDN;
    std::string_view server_s = in_.v1();
    if (server_s == "E") {
     ev.server = -1;
    } else {
     assert(server_s.size() >= 2 && server_s[0] == 'C');
     ev.server = TaskSpec::parse_int(std::string_view(server_s).substr(1));
    }
    std::string_view w1 = in_.v1();
    std::string_view w2 = in_.v1();
    ev.spec = {std::string(w1), std::string(w2)};
    if (w1 == "P") {
     if (w2 == "PROC") {
      ev.spec.push_back(std::string(in_.v1()));
      ev.spec.push_back(std::string(in_.v1()));
      ev.spec.push_back(std::string(in_.v1()));
      ev.spec.push_back(std::string(in_.v1()));
     } else {
      ev.spec.push_back(std::string(in_.v1()));
      ev.spec.push_back(std::string(in_.v1()));
     }
    } else {
     std::string_view x = in_.v1();
     std::string_view ms = in_.v1();
     ev.spec.push_back(std::string(x));
     ev.spec.push_back(std::string(ms));
     const int m = TaskSpec::parse_int(ms);
     assert(m >= 1);
     for (int j = 0; j < m; ++j)
     ev.spec.push_back(std::string(in_.v1()));
    }
    std::string_view dur_token = in_.v1();
    ev.dur = TaskSpec::parse_double(dur_token);
    assert(ev.dur > 0.0);
   } else if (kind == "XDN") {
    ev.kind = v2::XDN;
    std::string_view dir = in_.v1();
    ev.remote = in_.q9y();
    ev.size = in_.next_long_long();
    std::string_view tag = in_.v1();
    ev.up = (dir == "UP");
    ev.pre = (tag == "PRE");
    int m = in_.q9y();
    assert(m >= 1);
    ev.ids.resize(static_cast<size_t>(m));
    for (int j = 0; j < m; ++j) ev.ids[static_cast<size_t>(j)] = in_.q9y();
   } else if (kind == "FIN") {
    ev.kind = v2::FIN;
    ev.rid = in_.q9y();
   } else {
    std::fprintf(stderr, "[DEBUG] Unknown event kind: '%s'\n", std::string(kind).c_str());
    std::fflush(stderr);
    assert(false);
   }
   events.push_back(std::move(ev));
  }
  return true;
 }
 bool TaskSpec::parse(const std::vector<std::string>& tokens, const Cg& cfg) {
  (void)cfg;
  if (tokens.size() < 2) return false;
  const std::string& w1 = tokens[0];
  const std::string& w2 = tokens[1];
  auto atoi_ = [&](size_t i) { return TaskSpec::parse_int(tokens[i]); };
  ids.clear();
  if (w1 == "P" && w2 == "PRE") {
   if (tokens.size() != 4) return false;
   step = StepKind::P_PRE;
   q13 = atoi_(2);
   ids = {atoi_(3)};
   ls = le = 0;
  } else if (w1 == "P" && w2 == "PROC") {
   if (tokens.size() != 6) return false;
   step = StepKind::P_PROC;
   ls = atoi_(2);
   le = atoi_(3);
   q13 = atoi_(4);
   ids = {atoi_(5)};
  } else if (w1 == "P" && w2 == "POST") {
   if (tokens.size() != 4) return false;
   step = StepKind::P_POST;
   q13 = atoi_(2);
   ids = {atoi_(3)};
   ls = le = 0;
  } else if (w1 == "D" && (w2 == "PRE" || w2 == "POST")) {
   if (tokens.size() < 4) return false;
   const int m = atoi_(3);
   if (static_cast<int>(tokens.size()) != 4 + m) return false;
   step = (w2 == "PRE") ? StepKind::D_PRE : StepKind::D_POST;
   q13 = -1;
   for (int j = 0; j < m; ++j) ids.push_back(atoi_(4 + static_cast<size_t>(j)));
   ls = le = 0;
  } else if (w1 == "D" && w2 == "PROC") {
   if (tokens.size() < 4) return false;
   const int m = atoi_(3);
   if (static_cast<int>(tokens.size()) != 4 + m) return false;
   step = StepKind::D_PROC;
   q13 = atoi_(2);
   for (int j = 0; j < m; ++j) ids.push_back(atoi_(4 + static_cast<size_t>(j)));
   ls = le = 0;
  } else {
   return false;
  }
  return true;
 }
 std::vector<std::string> TaskSpec::tokens() const {
  std::vector<std::string> v;
  const char* n1 = (step == StepKind::P_PRE || step == StepKind::P_PROC ||
  step == StepKind::P_POST) ? "P" : "D";
  const char* n2 = nullptr;
  switch (step) {
   case StepKind::P_PRE: n2 = "PRE"; break;
   case StepKind::P_PROC: n2 = "PROC"; break;
   case StepKind::P_POST: n2 = "POST"; break;
   case StepKind::D_PRE: n2 = "PRE"; break;
   case StepKind::D_PROC: n2 = "PROC"; break;
   case StepKind::D_POST: n2 = "POST"; break;
  }
  v.push_back(n1);
  v.push_back(n2);
  if (step == StepKind::P_PRE || step == StepKind::P_POST) {
   v.push_back(std::to_string(q13));
   v.push_back(std::to_string(ids[0]));
  } else if (step == StepKind::P_PROC) {
   v.push_back(std::to_string(ls));
   v.push_back(std::to_string(le));
   v.push_back(std::to_string(q13));
   v.push_back(std::to_string(ids[0]));
  } else {
   v.push_back(step == StepKind::D_PROC ? std::to_string(q13) : std::string("-1"));
   v.push_back(std::to_string(static_cast<int>(ids.size())));
   for (int id : ids) v.push_back(std::to_string(id));
  }
  return v;
 }
 int TaskSpec::parse_int(std::string_view v) {
  int x = 0;
  size_t i = 0;
  bool neg = false;
  if (i < v.size() && v[i] == '-') { neg = true; ++i; }
  while (i < v.size() && v[i] >= '0' && v[i] <= '9') { x = x * 10 + (v[i] - '0'); ++i; }
  return neg ? -x : x;
 }
 double TaskSpec::parse_double(std::string_view v) {
  size_t i = 0;
  bool neg = false;
  if (i < v.size() && v[i] == '-') { neg = true; ++i; }
  double x = 0.0;
  while (i < v.size() && v[i] >= '0' && v[i] <= '9') { x = x * 10.0 + (v[i] - '0'); ++i; }
  if (i < v.size() && v[i] == '.') {
   ++i;
   double f = 0.0;
   double d = 0.1;
   while (i < v.size() && v[i] >= '0' && v[i] <= '9') { f += (v[i] - '0') * d; d *= 0.1; ++i; }
   x += f;
  }
  if (i < v.size() && (v[i] == 'e' || v[i] == 'E')) {
   ++i;
   bool eneg = false;
   if (i < v.size() && v[i] == '-') { eneg = true; ++i; }
   else if (i < v.size() && v[i] == '+') { ++i; }
   long long e = 0;
   while (i < v.size() && v[i] >= '0' && v[i] <= '9') { e = e * 10 + (v[i] - '0'); ++i; }
   if (e > 22) {
    const double p = std::pow(10.0, static_cast<double>(eneg ? -e : e));
    x *= p;
   } else {
    while (e > 0) { if (eneg) x /= 10.0; else x *= 10.0; --e; }
   }
  }
  return neg ? -x : x;
 }
 void qj::load([[maybe_unused]] int N, const std::vector<std::vector<double>>& rows) {
  assert(N >= 1);
  rows_ = rows;
  assert(static_cast<int>(rows_.size()) == N);
  for (const auto& r : rows_) {
   assert(r.size() == 7);
   assert(r[0] >= 1.0);
   for (int c = 1; c <= 6; ++c) {
    [[maybe_unused]] const double v = r[static_cast<size_t>(c)];
    assert(v == -1.0 || v > 0.0);
   }
  }
  std::sort(rows_.begin(), rows_.end(),
  [](const std::vector<double>& a, const std::vector<double>& b) {
   return a[0] < b[0];
  });
  for (size_t i = 1; i < rows_.size(); ++i) {
   assert(rows_[i][0] > rows_[i - 1][0]);
  }
  cols_.assign(6, {});
  for (const auto& r : rows_) {
   const int bs = static_cast<int>(r[0]);
   for (int c = 0; c < 6; ++c) {
    const double v = r[static_cast<size_t>(c) + 1];
    if (v != -1.0) {
     cols_[static_cast<size_t>(c)].emplace_back(bs, v);
    }
   }
  }
 }
 double qj::get(Step step, int batch_size) const {
  const std::vector<std::pair<int, double>>& col = cols_[static_cast<size_t>(step)];
  if (col.empty()) {
   return 1.0;
  }
  auto it = std::upper_bound(col.begin(), col.end(), batch_size,
  [](int val, const std::pair<int, double>& p) {
   return val < p.first;
  });
  if (it == col.end()) {
   return col.back().second;
  }
  if (it == col.begin()) {
   return col.front().second;
  }
  const auto& hi = *it;
  const auto& lo = *(it - 1);
  if (lo.first == batch_size) {
   return lo.second;
  }
  const double f = (batch_size - lo.first) / static_cast<double>(hi.first - lo.first);
  const double res = lo.second + f * (hi.second - lo.second);
  assert(res > 0.0);
  return res;
 }
 int qj::qb(Step step) const {
  const std::vector<std::pair<int, double>>& col = cols_[static_cast<size_t>(step)];
  if (col.empty()) return 0;
  return col.back().first;
 }
 Step q14(StepKind k) {
  switch (k) {
   case StepKind::P_PRE:  return Step::PP;
   case StepKind::P_PROC: return Step::PR;
   case StepKind::P_POST: return Step::PQ;
   case StepKind::D_PRE:  return Step::DP;
   case StepKind::D_PROC: return Step::DR;
   case StepKind::D_POST: return Step::DQ;
  }
  assert(false);
  return Step::PP;
 }
 StepKind kind_of(Step s) {
  switch (s) {
   case Step::PP:  return StepKind::P_PRE;
   case Step::PR: return StepKind::P_PROC;
   case Step::PQ: return StepKind::P_POST;
   case Step::DP:   return StepKind::D_PRE;
   case Step::DR:  return StepKind::D_PROC;
   case Step::DQ:  return StepKind::D_POST;
  }
  assert(false);
  return StepKind::P_PRE;
 }
 namespace {
 }
 void qd::push(double enter_t, double dur) {
  const double start = std::max(enter_t, tail);
  qa.push_back(start + dur);
  tail = start + dur;
 }
 void qd::drain(double t) {
  while (!qa.empty() && qa.front() <= t + 1e-9) {
   qa.erase(qa.begin());
  }
  tail = qa.empty() ? 0.0 : qa.back();
 }
 void St::init(const Cg& cfg) {
  qV = cfg;
  q7 = cfg.K;
  reqs_.clear();
  seen_.clear();
  local_ = qc{};
  clouds_.assign(static_cast<size_t>(cfg.K), qc{});
  cloud_load_.assign(static_cast<size_t>(cfg.K), 0);
  cloud_decode_est_.assign(static_cast<size_t>(cfg.K), 0.0);
  ppre_.reset();
  g_head_.assign(static_cast<size_t>(v4), -1);
  g_tail_.assign(static_cast<size_t>(v4), -1);
  g_count_.assign(static_cast<size_t>(v4), 0);
  c_head_.assign(static_cast<size_t>(v4), std::vector<int>(static_cast<size_t>(cfg.K), -1));
  c_tail_.assign(static_cast<size_t>(v4), std::vector<int>(static_cast<size_t>(cfg.K), -1));
  c_count_.assign(static_cast<size_t>(v4), std::vector<int>(static_cast<size_t>(cfg.K), 0));
  q0 = 0;
  up_ = qd{};
  down_ = qd{};
  up_.reserve(static_cast<size_t>(cfg.K) + 8);
  down_.reserve(static_cast<size_t>(cfg.K) + 8);
  finish_pending_.clear();
  stats_ = Stats{};
  oracle_ = Oracle{};
  dynamic_max_batch_ = -1;
 }
 void St::apply_event(const Event& ev, double t) {
  switch (ev.kind) {
   case v2::ARR: {
    last_arr_ = t;
    const int rid = ev.rid;
    assert(rid >= 0);
    if (rid >= static_cast<int>(reqs_.size())) {
     reqs_.resize(static_cast<size_t>(rid) + 1);
    }
    if (rid >= static_cast<int>(seen_.size())) {
     seen_.resize(static_cast<size_t>(rid) + 1, 0);
    }
    assert(!seen_[static_cast<size_t>(rid)]);
    Request& req = reqs_[static_cast<size_t>(rid)];
    req = Request{};
    req.id = rid;
    req.lin = ev.lin;
    req.arrival = t;
    req.phase = Phase::ql;
    seen_[static_cast<size_t>(rid)] = 1;
    req.q5 = true;
    qU(rid);
    stats_.q2 = std::min(stats_.q2, t);
    oracle_.q2 = std::min(oracle_.q2, t);
    break;
   }
   case v2::TDN: {
    assert(ev.server >= -1 && ev.server < q7);
    TaskSpec spec;
    [[maybe_unused]] const bool parsed = spec.parse(ev.spec, qV);
    assert(parsed);
    qc* ct = (ev.server == -1) ? &local_ : &clouds_[static_cast<size_t>(ev.server)];
    assert(ct->q15);
    ct->q15 = false;
    if (ev.server == -1) e_busy_ += qV.S + ev.dur;
    else c_busy_ += qV.S + ev.dur;
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
      req.ppre_end = t;
      break;
     }
     case StepKind::P_PROC: {
      assert(spec.ids.size() == 1);
      const int rid = spec.ids[0];
      Request& req = reqs_[static_cast<size_t>(rid)];
      assert(req.phase == Phase::qy);
      assert(spec.q13 == req.q13);
      req.next_ls = spec.le;
      req.pproc_end = t;
      if (spec.le == qV.nl) {
       req.phase = Phase::qx;
      } else {
       req.phase = Phase::qn;
       req.q5 = true;
       vj(Phase::qn, req.q13, rid);
      }
      break;
     }
     case StepKind::P_POST: {
      assert(spec.ids.size() == 1);
      const int rid = spec.ids[0];
      Request& req = reqs_[static_cast<size_t>(rid)];
      assert(req.phase == Phase::P_POST_RUNNING);
      assert(spec.q13 == req.q13);
      req.phase = Phase::qk;
      req.ppost_t = t;
      stats_.sum_tdr += t - req.arrival;
      oracle_.sum_tdr += t - req.arrival;
      oracle_.ct += 1;
      req.q5 = true;
      vj(Phase::qk, req.q13, rid);
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
       assert(req.q13 == spec.q13);
       req.phase = Phase::WAIT_DEC_DOWN;
      }
      break;
     }
     case StepKind::D_POST: {
      for (const int rid : spec.ids) {
       Request& req = reqs_[static_cast<size_t>(rid)];
       ++req.iter;
       if (req.first_token < 0.0) req.first_token = t;
       if (req.iter > 1) {
        const double gap = t - req.lt;
        oracle_.sum_gaps += gap;
        oracle_.tg += 1;
       }
       req.lt = t;
       if (req.lt > oracle_.q9) oracle_.q9 = req.lt;
       if (req.finished) oracle_.tokens_out += 1;
       if (req.finished) continue;
       assert(req.phase == Phase::D_POST_RUNNING);
       if (req.q5) rr(rid);
       req.phase = Phase::qk;
       req.q5 = true;
       vj(Phase::qk, req.q13, rid);
      }
      break;
     }
    }
    break;
   }
   case v2::XDN: {
    const double xlen = static_cast<double>(ev.size) /
    std::max<double>(1.0, static_cast<double>(qV.bpt));
    if (ev.up) up_busy_ += q4(qV, xlen);
    else down_busy_ += q4(qV, xlen);
    assert(ev.remote >= 0 && ev.remote < q7);
#if defined(V12_MIRROR) || defined(DEBUG)
    if (ev.up) up_.drain(t); else down_.drain(t);
#endif
    if (ev.up) {
     if (ev.pre) ++up_deq_pre_;
     else ++up_deq_dec_;
    }
    if (ev.pre) {
     assert(ev.ids.size() == 1);
     const int rid = ev.ids[0];
     Request& req = reqs_[static_cast<size_t>(rid)];
     if (ev.up) {
      assert(req.phase == Phase::WAIT_INPUT_UP);
      assert(req.q13 == ev.remote);
      req.phase = Phase::qn;
      req.q5 = true;
      req.up_done = t;
      vj(Phase::qn, req.q13, rid);
     } else {
      assert(req.phase == Phase::qx);
      req.phase = Phase::qr;
      req.q5 = true;
      req.down_done = t;
      vj(Phase::qr, -1, rid);
     }
    } else {
     for (const int rid : ev.ids) {
      Request& req = reqs_[static_cast<size_t>(rid)];
      if (ev.up) {
       assert(req.phase == Phase::WAIT_DEC_UP);
       assert(req.q13 == ev.remote);
       req.phase = Phase::qo;
       req.q5 = true;
       vj(Phase::qo, req.q13, rid);
      } else {
       assert(req.phase == Phase::WAIT_DEC_DOWN);
       req.phase = Phase::qs;
       req.q5 = true;
       vj(Phase::qs, -1, rid);
      }
     }
    }
    break;
   }
   case v2::FIN: {
    Request& req = reqs_[static_cast<size_t>(ev.rid)];
    assert(!req.finished);
    if (req.q5) {
     rr(ev.rid);
    }
    req.finished = true;
    req.lout = req.iter;
    req.phase = Phase::FINISHED;
    el = 0.05 * static_cast<double>(req.iter) + 0.95 * el;
    assert(req.q13 >= 0 && req.q13 < q7);
    --cloud_load_[static_cast<size_t>(req.q13)];
    cloud_decode_est_[static_cast<size_t>(req.q13)] -= static_cast<double>(req.lin);
    finish_pending_.push_back(ev.rid);
    oracle_.tokens_out += req.iter;
    if (req.lt > oracle_.q9) oracle_.q9 = req.lt;
    break;
   }
  }
 }
 void St::end_frame() {
  ++frames_;
  for (const int rid : finish_pending_) {
   Request& req = reqs_[static_cast<size_t>(rid)];
   assert(req.finished);
   if (req.stats_recorded) continue;
   assert(req.first_token >= 0.0);
   stats_.sum_span += req.lt - req.first_token;
   stats_.tg += req.iter - 1;
   stats_.tokens += req.iter;
   stats_.latest_token = std::max(stats_.latest_token, req.lt);
   req.stats_recorded = true;
  }
  finish_pending_.clear();
 }
 void St::issue(const TaskSpec& spec, double t) {
  assert(!spec.ids.empty());
  double dur = 0.0;
  switch (spec.step) {
   case StepKind::P_PROC: {
    const Request& r = reqs_[static_cast<size_t>(spec.ids[0])];
    const double frac = static_cast<double>(spec.le - spec.ls) /
    static_cast<double>(qV.nl);
    dur = frac * qW.get(q14(spec.step), r.lin);
    break;
   }
   case StepKind::P_PRE:
   case StepKind::P_POST: {
    const Request& r = reqs_[static_cast<size_t>(spec.ids[0])];
    dur = qW.get(q14(spec.step), r.lin);
    break;
   }
   case StepKind::D_PRE:
   case StepKind::D_PROC:
   case StepKind::D_POST:
   dur = qW.get(q14(spec.step), static_cast<int>(spec.ids.size()));
   break;
  }
  const double q10 = t + qV.S + dur;
  const bool local_step = spec.step == StepKind::P_PRE || spec.step == StepKind::P_POST ||
  spec.step == StepKind::D_PRE || spec.step == StepKind::D_POST;
  qc* ct = nullptr;
  if (local_step) {
   assert(!local_.q15);
   ct = &local_;
  } else {
   assert(spec.q13 >= 0 && spec.q13 < q7);
   assert(!clouds_[static_cast<size_t>(spec.q13)].q15);
   ct = &clouds_[static_cast<size_t>(spec.q13)];
  }
  ct->q15 = true;
  ct->q10 = q10;
  ct->step = spec.step;
  ct->ids = spec.ids;
  if (spec.step == StepKind::P_PRE) {
   reqs_[static_cast<size_t>(spec.ids[0])].ppre_start = t;
  } else if (spec.step == StepKind::P_POST) {
   reqs_[static_cast<size_t>(spec.ids[0])].ppost_start = t;
  } else if (spec.step == StepKind::P_PROC && spec.ls == 0) {
   reqs_[static_cast<size_t>(spec.ids[0])].pproc_start = t;
  }
  if (spec.step == StepKind::P_PRE) {
   ++up_proj_pre_;
  } else if (spec.step == StepKind::D_PRE) {
   int distinct_c = 0;
   std::vector<char> seen(static_cast<size_t>(q7), 0);
   for (const int rid : spec.ids) {
    const int c = reqs_[static_cast<size_t>(rid)].q13;
    if (!seen[static_cast<size_t>(c)]) { seen[static_cast<size_t>(c)] = 1; ++distinct_c; }
   }
   up_proj_dec_ += distinct_c;
  }
#if defined(V12_MIRROR) || defined(DEBUG)
  if (spec.step == StepKind::P_PRE) {
   const Request& r = reqs_[static_cast<size_t>(spec.ids[0])];
   up_.push(q10, q4(qV, static_cast<double>(r.lin)));
  } else if (spec.step == StepKind::P_PROC && spec.le == qV.nl) {
   const Request& r = reqs_[static_cast<size_t>(spec.ids[0])];
   down_.push(q10, q4(qV, static_cast<double>(r.lin)));
  } else if (spec.step == StepKind::D_PRE) {
   std::vector<int> cnt(static_cast<size_t>(q7), 0);
   for (const int rid : spec.ids) ++cnt[static_cast<size_t>(reqs_[static_cast<size_t>(rid)].q13)];
   for (int c = 0; c < q7; ++c) {
    if (cnt[static_cast<size_t>(c)] > 0) {
     up_.push(q10, q4(qV, static_cast<double>(cnt[static_cast<size_t>(c)])));
    }
   }
  } else if (spec.step == StepKind::D_PROC) {
   down_.push(q10, q4(qV, static_cast<double>(spec.ids.size())));
  }
#endif
  for (const int rid : spec.ids) {
   Request& req = reqs_[static_cast<size_t>(rid)];
   assert(!req.finished);
   req.q5 = false;
   if (spec.step == StepKind::P_PRE) {
    assert(req.phase == Phase::ql);
    req.q13 = spec.q13;
    ++cloud_load_[static_cast<size_t>(spec.q13)];
    cloud_decode_est_[static_cast<size_t>(spec.q13)] += static_cast<double>(req.lin);
    req.phase = Phase::P_PRE_RUNNING;
   } else if (spec.step == StepKind::P_PROC) {
    assert(req.phase == Phase::qn);
    assert(spec.q13 == req.q13);
    req.phase = Phase::qy;
   } else if (spec.step == StepKind::P_POST) {
    assert(req.phase == Phase::qr);
    assert(spec.q13 == req.q13);
    req.phase = Phase::P_POST_RUNNING;
   } else if (spec.step == StepKind::D_PRE) {
    assert(req.phase == Phase::qk);
    req.phase = Phase::D_PRE_RUNNING;
   } else if (spec.step == StepKind::D_PROC) {
    assert(req.phase == Phase::qo);
    assert(spec.q13 == req.q13);
    req.phase = Phase::D_PROC_RUNNING;
   } else if (spec.step == StepKind::D_POST) {
    assert(req.phase == Phase::qs);
    req.phase = Phase::D_POST_RUNNING;
   }
  }
 }
 void St::qU(int rid) {
  Request& r = reqs_[static_cast<size_t>(rid)];
  int lin = r.lin;
  if (lin < 0) lin = 0;
  if (lin > qu) lin = qu;
  int& h = ppre_.head[lin];
  int& t = ppre_.tail[lin];
  r.q11 = -1;
  r.pr = t;
  if (t != -1) {
   reqs_[static_cast<size_t>(t)].q11 = rid;
  } else {
   h = rid;
  }
  t = rid;
  if (ppre_.count[lin] == 0) {
   if (lin > ppre_.v9) ppre_.v9 = lin;
   if (lin < ppre_.v8) ppre_.v8 = lin;
  }
  ++ppre_.count[lin];
  r.q5 = true;
 }
 int St::qT() {
  while (ppre_.v8 < qm && ppre_.head[ppre_.v8] == -1) ++ppre_.v8;
  if (ppre_.v8 >= qm) return -1;
  int lin = ppre_.v8;
  int rid = ppre_.head[lin];
  Request& r = reqs_[static_cast<size_t>(rid)];
  ppre_.head[lin] = r.q11;
  if (ppre_.head[lin] == -1) {
   ppre_.tail[lin] = -1;
  } else {
   reqs_[static_cast<size_t>(ppre_.head[lin])].pr = -1;
  }
  r.q11 = r.pr = -1;
  --ppre_.count[lin];
  r.q5 = false;
  return rid;
 }
 void St::vj(Phase p, int q13, int rid) {
  Request& r = reqs_[static_cast<size_t>(rid)];
  r.pr = -1; r.q11 = -1;
  int pi = v7(p);
  if (p == Phase::ql) { qU(rid); return; }
  const bool mt = (p == Phase::qk) && (r.iter > 0);
  if (q8(p)) {
   int& h = c_head_[pi][q13];
   int& t = c_tail_[pi][q13];
   r.q11 = -1;
   if (h == -1) { h = rid; }
   else { reqs_[static_cast<size_t>(t)].q11 = rid; r.pr = t; }
   t = rid;
   ++c_count_[pi][q13];
  } else {
   int& h = g_head_[pi];
   int& t = g_tail_[pi];
   r.q11 = -1;
   if (h == -1) { h = rid; }
   else { reqs_[static_cast<size_t>(t)].q11 = rid; r.pr = t; }
   t = rid;
   ++g_count_[pi];
  }
  if (mt) ++q0;
  r.q5 = true;
 }
 int St::qR(Phase p, int q13) {
  int pi = v7(p);
  if (p == Phase::ql) return qT();
  int h = q8(p) ? c_head_[pi][q13] : g_head_[pi];
  if (h == -1) return -1;
  Request& r = reqs_[static_cast<size_t>(h)];
  int next = r.q11;
  if (q8(p)) {
   c_head_[pi][q13] = next;
   if (next == -1) c_tail_[pi][q13] = -1;
   if (next != -1) reqs_[static_cast<size_t>(next)].pr = -1;
   --c_count_[pi][q13];
  } else {
   g_head_[pi] = next;
   if (next == -1) g_tail_[pi] = -1;
   if (next != -1) reqs_[static_cast<size_t>(next)].pr = -1;
   --g_count_[pi];
  }
  if ((p == Phase::qk) && (r.iter > 0)) --q0;
  r.q11 = r.pr = -1;
  r.q5 = false;
  return h;
 }
 int St::qS(Phase p, int q13) const {
  int pi = v7(p);
  if (p == Phase::ql) {
   int s = 0;
   for (int b = 0; b < qm; ++b) s += ppre_.count[b];
   return s;
  }
  return q8(p) ? c_count_[pi][q13] : g_count_[pi];
 }
 int St::qQ(Phase p, int q13) const {
  int pi = v7(p);
  if (p == Phase::ql) {
   if (ppre_.v8 >= qm) return -1;
   return ppre_.head[ppre_.v8];
  }
  return q8(p) ? c_head_[pi][q13] : g_head_[pi];
 }
 void St::rr(int rid) {
  Request& r = reqs_[static_cast<size_t>(rid)];
  if (!r.q5) return;
  Phase p = r.phase;
  int pi = v7(p);
  if (p == Phase::ql) {
   int lin = r.lin; if (lin < 0) lin = 0; if (lin > qu) lin = qu;
   int& h = ppre_.head[lin]; int& ta = ppre_.tail[lin]; int& cnt = ppre_.count[lin];
   if (h == rid) h = r.q11;
   if (ta == rid) ta = r.pr;
   if (r.pr != -1) reqs_[static_cast<size_t>(r.pr)].q11 = r.q11;
   if (r.q11 != -1) reqs_[static_cast<size_t>(r.q11)].pr = r.pr;
   --cnt;
   r.pr = r.q11 = -1;
   r.q5 = false;
   if (cnt == 0) {
    if (lin == ppre_.v9) {
     while (ppre_.v9 >= 0 && ppre_.head[ppre_.v9] == -1) --ppre_.v9;
    }
    if (lin == ppre_.v8) {
     while (ppre_.v8 < qm && ppre_.head[ppre_.v8] == -1) ++ppre_.v8;
    }
   }
   return;
  }
  int* h = q8(p) ? &c_head_[pi][r.q13] : &g_head_[pi];
  int* ta = q8(p) ? &c_tail_[pi][r.q13] : &g_tail_[pi];
  int* ct = q8(p) ? &c_count_[pi][r.q13] : &g_count_[pi];
  if (*h == rid) *h = r.q11;
  if (*ta == rid) *ta = r.pr;
  if (r.pr != -1) reqs_[static_cast<size_t>(r.pr)].q11 = r.q11;
  if (r.q11 != -1) reqs_[static_cast<size_t>(r.q11)].pr = r.pr;
  --(*ct);
  if ((p == Phase::qk) && (r.iter > 0)) --q0;
  r.pr = r.q11 = -1;
  r.q5 = false;
 }
 int St::q6(Phase p) const {
  int s = 0;
  if (p == Phase::ql) {
   for (int b = 0; b < qm; ++b) s += ppre_.count[b];
   return s;
  }
  int pi = v7(p);
  if (q8(p)) {
   for (int c = 0; c < q7; ++c) s += c_count_[pi][c];
  } else {
   s = g_count_[pi];
  }
  return s;
 }
 namespace {
 }
 std::vector<Assignment> Se::decide(St& st, double t) {
  std::vector<Assignment> out;
  out.reserve(static_cast<size_t>(st.qY()) + 1);
  TaskSpec local_task = pick_local(st, t);
  if (local_task.ids.size() > 0) {
   st.issue(local_task, t);
   out.push_back(Assignment{std::move(local_task)});
  }
  for (int c = 0; c < st.qY(); ++c) {
   TaskSpec cloud_task = pick_cloud(st, c, t);
   if (cloud_task.ids.size() > 0) {
    st.issue(cloud_task, t);
    out.push_back(Assignment{std::move(cloud_task)});
   }
  }
  return out;
 }
 TaskSpec Se::pick_local(St& st, double t) {
  (void)t;
  TaskSpec spec;
  if (st.local().q15) return spec;
  int id = -1;
#define PPRE_MAX_INFLIGHT 2
#ifdef V11_PPRECAP
  const double q1 = q4(qV, static_cast<double>(St::qu));
  const int base_inflight = std::max(2, st.qY());
  const int max_inflight = (q1 < 1.0) ? 100 : base_inflight;
  const bool dist_headroom = (qV.v0 > 0.0) &&
  (st.oracle().ed(qV.s1, qV.s2) < 0.5 * qV.v0);
  int max_inflight_eff = max_inflight;
  if (!dist_headroom && qV.w_c > 0.0) {
   max_inflight_eff = std::max(base_inflight, max_inflight / 2 + 1);
  } else {
   max_inflight_eff = std::max(max_inflight, 2 + 2 * st.qY());
  }
  const bool up_full = st.up_inflight_pre() >= static_cast<size_t>(max_inflight_eff);
#endif
  {
   const int qL = st.qQ(Phase::ql, -1);
   const int qM = st.qQ(Phase::qr, -1);
   const bool tdr_urgent = (qV.v0 > 0.0 && qV.w_c > 0.0 &&
   st.oracle().et(qV.s1) > 0.3 * qV.v0);
   bool run_ppost = false;
   if (qM >= 0) {
    if (qL < 0 || tdr_urgent) {
     run_ppost = true;
    } else {
     const Request& r_pre = st.q12(qL);
     const Request& r_post = st.q12(qM);
     const double a_i = qW.get(Step::PP, r_pre.lin);
     const double q_j = qW.get(Step::PQ, r_post.lin);
     const qc& cc = st.q13(r_post.q13);
     const bool cloud_starved = cc.q15 && (cc.q10 > t + q_j);
     const bool link_busy = st.up().tail > t + 1.0;
     if (q_j < a_i && (!cloud_starved || link_busy)) run_ppost = true;
    }
   }
   if (!run_ppost) {
#ifdef V11_PPRECAP
    if (!up_full)
#endif
    {
     id = st.qT();
     if (id >= 0) {
      const Request& r = st.q12(id);
      spec.step = StepKind::P_PRE;
      spec.q13 = qX.q13->choose(st, r, t);
      spec.ids = {id};
      return spec;
     }
    }
   }
   if (qM >= 0) {
    id = st.qR(Phase::qr, -1);
    const Request& r = st.q12(id);
    spec.step = StepKind::P_POST;
    spec.q13 = r.q13;
    spec.ids = {id};
    return spec;
   }
  }
  int mstar_target = -1;
  if (qV.w_c < 0.1) {
   const int m_star = qW.decode_m_star(qV.S);
   const int mcap = std::min(m_star, std::max(1, st.num_requests()));
   const double f_star = (2.0 * qV.S + qW.get(Step::DP, mcap)
   + qW.get(Step::DQ, mcap)) / static_cast<double>(mcap);
   const double f_ref = (2.0 * qV.S + qW.get(Step::DP, 1)
   + qW.get(Step::DQ, 1));
   if (2.0 * f_star < f_ref) mstar_target = mcap;
  }
  bool hold = qX.delay != nullptr && qX.delay->hd(st, t);
  const bool prefill_priority = (qV.w_tp == 0.0 && qV.w_c > 0.0) &&
  (st.qS(Phase::ql, -1) > 0 ||
  st.qS(Phase::qr, -1) > 0);
  if (!hold && !prefill_priority) {
   {
    int hc = -1;
    size_t bestsz = 0;
    for (int c = 0; c < st.qY(); ++c) {
     const int sz = st.qS(Phase::qk, c);
     if (static_cast<size_t>(sz) > bestsz) { bestsz = static_cast<size_t>(sz); hc = c; }
    }
    int hid = -1;
    if (hc >= 0) hid = st.qQ(Phase::qk, hc);
    bool qN = false;
#ifdef DPRE_ACCUMULATE
    if (hid >= 0) {
     const int dpost_n = st.qS(Phase::qs, -1);
     const int dpre_n = st.q6(Phase::qk);
     int target = (qV.w_c > 0.0)
     ? qX.batch->gs(StepKind::D_PRE, 1 << 20, st, t)
     : 0;
     if (mstar_target > target) target = mstar_target;
     if (target > 0 && dpre_n < target) {
      if (dpost_n > 0) {
       qN = true;
      } else if (st.multi_token_dpre_count() == 0) {
       const int pipe = st.q6(Phase::qx)
       + st.q6(Phase::qy);
       if (pipe > 0 && dpre_n + pipe >= target) {
        qN = true;
       }
      }
     }
    }
#endif
    if (hid >= 0 && !qN) {
#ifdef V11_PPRECAP
     if (st.up_inflight_pre() < static_cast<size_t>(max_inflight_eff))
#endif
     {
      st.qR(Phase::qk, hc);
      spec.step = StepKind::D_PRE;
      spec.q13 = -1;
      spec.ids = {hid};
      int total = 1;
      for (int c = 0; c < st.qY(); ++c)
      total += st.qS(Phase::qk, c);
      const int m = qX.batch->gs(StepKind::D_PRE, total, st, t);
      const int m_eff = (mstar_target > m)
      ? std::min(mstar_target, total) : m;
      if (m_eff > 1 && qX.qO) {
       const double xfer_1 = q4(qV, 1.0);
       const double dpre_1 = st.tb().get(Step::DP, 1);
       const double dpost_1 = st.tb().get(Step::DQ, 1);
       const double decode_ref = std::max({dpre_1, dpost_1, 1e-9});
       const double xfer_ratio = xfer_1 / decode_ref;
       int dynamic_cap;
       if (xfer_ratio < 0.05) {
        dynamic_cap = st.qY();
       } else if (xfer_ratio < 0.3) {
        dynamic_cap = std::max(2, st.qY());
       } else if (xfer_ratio < 1.0) {
        dynamic_cap = std::max(2, st.qY() / 2);
       } else if (xfer_ratio < 5.0) {
        dynamic_cap = std::min(qX.qP, 2);
       } else {
        dynamic_cap = 1;
       }
       if (st.up().in_flight() > 2 &&
       st.up().head_finish() > t + 2.0 * xfer_1) {
        dynamic_cap = 1;
       }
       int eff_cap = dynamic_cap;
       while (static_cast<int>(spec.ids.size()) < m_eff &&
       !st.qE0(Phase::qk, hc)) {
        spec.ids.push_back(st.qR(Phase::qk, hc));
       }
       int clouds_used = 1;
       if (static_cast<int>(spec.ids.size()) < m_eff && eff_cap > 1) {
        std::vector<std::pair<int, int>> others;
        for (int c = 0; c < st.qY(); ++c) {
         if (c == hc) continue;
         int sz = st.qS(Phase::qk, c);
         if (sz > 0) others.push_back({sz, c});
        }
        std::sort(others.rbegin(), others.rend());
        for (auto [sz, c] : others) {
         if (clouds_used >= eff_cap ||
         static_cast<int>(spec.ids.size()) >= m_eff) break;
         while (static_cast<int>(spec.ids.size()) < m_eff &&
         !st.qE0(Phase::qk, c)) {
          spec.ids.push_back(st.qR(Phase::qk, c));
         }
         ++clouds_used;
        }
       }
      } else if (m_eff > 1) {
       int want = m_eff - 1;
       int got = 0;
       while (got < want && !st.qE0(Phase::qk, hc)) {
        spec.ids.push_back(st.qR(Phase::qk, hc));
        ++got;
       }
      }
      return spec;
     }
    }
   }
  }
  {
   const int n = st.qS(Phase::qs, -1);
   if (n > 0) {
    const int m = qX.batch->gs(StepKind::D_POST, n, st, t);
    spec.step = StepKind::D_POST;
    spec.q13 = -1;
    spec.ids.reserve(static_cast<size_t>(m));
    for (int i = 0; i < m && i < n; ++i)
    spec.ids.push_back(st.qR(Phase::qs, -1));
    return spec;
   }
  }
  return spec;
 }
 TaskSpec Se::pick_cloud(St& st, int c, double t) {
  (void)t;
  TaskSpec spec;
  if (st.q13(c).q15) return spec;
  int id = -1;
  if (st.frames() >= 1200000LL) {
   id = st.qQ(Phase::qn, c);
   if (id >= 0) st.rr(id);
  } else {
   int best_rid = -1;
   double best_lin = 1e300;
   for (int rid = st.qQ(Phase::qn, c); rid != -1;
   rid = st.q12(rid).q11) {
    if (st.q12(rid).lin < best_lin) {
     best_lin = static_cast<double>(st.q12(rid).lin);
     best_rid = rid;
    }
   }
   if (best_rid >= 0) {
    st.rr(best_rid);
    id = best_rid;
   }
  }
  if (id >= 0) {
   const Request& r = st.q12(id);
   auto [ls, le] = qX.chunk->np(st, r, t);
   spec.step = StepKind::P_PROC;
   spec.ls = ls;
   spec.le = le;
   spec.q13 = c;
   spec.ids = {id};
   return spec;
  }
  {
   const int n = st.qS(Phase::qo, c);
   if (n > 0) {
    const int m = qX.batch->gs(StepKind::D_PROC, n, st, t);
    spec.step = StepKind::D_PROC;
    spec.q13 = c;
    spec.ids.reserve(static_cast<size_t>(m));
    for (int i = 0; i < m && i < n; ++i)
    spec.ids.push_back(st.qR(Phase::qo, c));
    return spec;
   }
  }
  return spec;
 }
 static double tg(const St& s, StepKind k, int m) {
  return s.tb().get(q14(k), m);
 }
 int PrefillLoadBalanceCloud::choose(const St& s, const Request& r, double t) const {
  if (r.lin >= 256 && s.qY() > 1) {
   int min_active = 1 << 30;
   int isolated = 0;
   for (int c = 0; c < s.qY(); ++c) {
    const int act = s.qZ(c);
    if (act < min_active) {
     min_active = act;
     isolated = c;
    }
   }
   return isolated;
  }
  int best = 0;
  double best_eft = 1e300;
  const double lout_avg = s.vy();
  const double this_lout = std::max(static_cast<double>(r.lin), lout_avg);
  for (int c = 0; c < s.qY(); ++c) {
   double t_pre = s.q13(c).q15 ? s.q13(c).q10 : 0.0;
   if (t_pre < t) t_pre = t;
   for (int rid = s.qQ(Phase::qn, c); rid != -1;
   rid = s.q12(rid).q11) {
    const Request& q = s.q12(rid);
    t_pre += s.tb().get(q14(StepKind::P_PROC), q.lin);
   }
   t_pre += s.tb().get(q14(StepKind::P_PROC), r.lin);
   const int active = s.qZ(c) + 1;
   int m_cap = 1;
   for (int m = 2; m <= active; ++m) {
    const double dur_m = tg(s, StepKind::D_PROC, m);
    if (tg(s, StepKind::D_PROC, m + 1) - dur_m > dur_m / m) break;
    m_cap = m;
   }
   const int m_actual = m_cap < active ? m_cap : active;
   const int batches = (active + m_actual - 1) / m_actual;
   const double t_decode = this_lout * std::max(batches * tg(s, StepKind::D_PROC, m_actual),
   tg(s, StepKind::D_PROC, 1) * active);
   const double eft = t_pre + t_decode;
   if (eft < best_eft - 1e-12) {
    best_eft = eft;
    best = c;
   }
  }
  return best;
 }
#define PPA_ALPHA 1.0
#define PPA_BETA 1.0
 std::pair<int, int> OnePieceChunk::np(const St& s, const Request& r, double t) const {
  (void)s; (void)t;
  assert(r.next_ls < s.cfg().nl);
  return {r.next_ls, s.cfg().nl};
 }
 std::pair<int, int> AdaptiveChunk::np(const St& s, const Request& r, double t) const {
  (void)t;
  const int L = s.cfg().nl;
  if (r.next_ls > 0) return {r.next_ls, L};
  if (L <= 1) return {0, 1};
  const double full_dur = tg(s, StepKind::P_PROC, r.lin);
  if (s.local().q10 <= t + EPS) {
   return {0, L};
  }
  if (s.up().head_finish() > t + EPS) {
   return {0, L};
  }
  if (r.q13 < 0 || s.qS(Phase::qn, r.q13) > 0 ||
  s.qS(Phase::qo, r.q13) > 0) {
   return {0, L};
  }
  const double S = s.cfg().S;
  double local_avg = 2.0 * S + tg(s, StepKind::D_PRE, 1) + tg(s, StepKind::D_POST, 1);
  if (local_avg < 1e-9) local_avg = 1e-9;
  int k_opt = std::max(1, static_cast<int>(std::ceil(full_dur / local_avg)));
  int k_max_s = (S > 1e-9) ? std::max(1, static_cast<int>(std::floor(full_dur / S))) : L;
  int k = std::min({k_opt, k_max_s, L, 2});
  int chunk_size = std::max(1, L / k);
  int le = r.next_ls + chunk_size;
  if (le > L || L - le < chunk_size) le = L;
  return {r.next_ls, le};
 }
 int SizeOneBatch::gs(StepKind step, int vg, const St& s, double t) const {
  (void)step; (void)vg; (void)s; (void)t;
  return 1;
 }
 void qg::precompute(const Cg& cfg, const qj& tbl) {
  for (int col = static_cast<int>(Step::DP);
  col <= static_cast<int>(Step::DQ); ++col) {
   const Step scol = static_cast<Step>(col);
   const int max_table_m = tbl.qb(scol);
   if (max_table_m <= 0) {
    vC[col] = 1 << 20;
    continue;
   }
   const bool cross = (scol == Step::DP) && (cfg.K > 1);
   const double lat = cross ? cfg.lm : 0.0;
   const double S = cfg.S;
   int m = 1;
   while (m < max_table_m) {
    const double dur_m = tbl.get(scol, m);
    const double dur_m1 = tbl.get(scol, m + 1);
    const double avg = (S + 2.0 * lat + dur_m) / static_cast<double>(m);
    const double marg = dur_m1 - dur_m;
    if (marg <= avg) ++m; else break;
   }
   int argmin_m = 1;
   double best_avg = std::numeric_limits<double>::max();
   for (const auto& bd : tbl.vz(scol)) {
    const double a = (S + 2.0 * lat + bd.second) / static_cast<double>(bd.first);
    if (a < best_avg - 1e-12) { best_avg = a; argmin_m = bd.first; }
   }
   int cap = m > argmin_m ? m : argmin_m;
   if (cap >= max_table_m) cap = 1 << 20;
   vC[col] = cap;
  }
 }
 int qg::gs(StepKind step, int vg, const St& s, double t) const {
  (void)t; (void)vD;
  const Step col = q14(step);
  const int mmax = vC[static_cast<int>(col)];
  if (mmax < 1) return 1;
  int cap = mmax;
  if (s.cfg().w_c > 0.0 && s.cfg().s2 > 0.0) {
   auto cost = [&](int m) -> double {
    double t_local = 2.0 * s.cfg().S
    + s.tb().get(q14(StepKind::D_PRE), m)
    + s.tb().get(q14(StepKind::D_POST), m);
    double t_cloud = s.cfg().S
    + s.tb().get(q14(StepKind::D_PROC), std::max(1, m / s.qY()));
    double t_transfer = q4(s.cfg(), static_cast<double>(m));
    return std::max({t_local, t_cloud, t_transfer});
   };
   int mms = 1;
   const bool low_lout_regime = (s.vy() < 2.5);
   const double slo_thresh = low_lout_regime ? s.cfg().s1 * 0.8 : s.cfg().s2;
   const int M = std::max({s.tb().qb(q14(StepKind::D_PRE)),
    s.tb().qb(q14(StepKind::D_PROC)),
    s.tb().qb(q14(StepKind::D_POST))});
   const int scan_hi = std::min(cap, M);
   for (int m = 2; m <= scan_hi; ++m) {
    if (cost(m) <= slo_thresh + 1e-9) mms = m;
   }
   if (cap > M) {
    int lo = M + 1, hi = cap;
    while (lo <= hi) {
     const int mid = (lo + hi) / 2;
     if (cost(mid) <= slo_thresh + 1e-9) {
      if (mid > mms) mms = mid;
      lo = mid + 1;
     } else hi = mid - 1;
    }
   }
   if (cap >= mms) {
    const Cg& cg = s.cfg();
    const auto& oc = s.oracle();
    double V_tp = oc.qB(cg.w_tp, cg.tp_base, cg.tp_UB);
    double V_tpot = (cg.K > 1)
    ? oc.qA(cg.w_c, cg.v0, cg.s1, cg.s2)
    : cg.w_c / (cg.s2 * std::max(cg.v0, 1.0));
    if (V_tpot <= 1e-9 && cg.v0 <= 0.0 && oc.qAt(cg.s2) > 0.0)
    V_tpot = 1000.0 * cg.w_c;
    if (V_tpot > 1e-9) {
     double tpot_cap = cost(cap);
     double tpot_slo = cost(mms);
     double delta_tpot = std::max(0.0, tpot_cap - cg.s2) - std::max(0.0, tpot_slo - cg.s2);
     double delta_tp = (cap / tpot_cap) - (mms / tpot_slo);
     double tdr_loss = 0.0;
     const int pipe = s.q6(Phase::qx) + s.q6(Phase::qy);
     if (pipe > 0 && oc.ct > 0) {
      const double dt_local =
      s.tb().get(q14(StepKind::D_PRE),  cap)
      + s.tb().get(q14(StepKind::D_POST), cap)
      - s.tb().get(q14(StepKind::D_PRE),  mms)
      - s.tb().get(q14(StepKind::D_POST), mms);
      if (dt_local > 0.0) {
       const double V_tdr = oc.qz(cg.w_c, cg.v0, cg.s1, cg.s2);
       tdr_loss = V_tdr * dt_local * static_cast<double>(pipe) / oc.ct;
      }
     }
     if (delta_tp * V_tp <= delta_tpot * V_tpot + tdr_loss) {
      cap = mms;
     }
    }
   }
  }
  return vg < cap ? vg : cap;
 }
 bool NoDelay::hd(const St& s, double t) const {
  (void)s; (void)t;
  return false;
 }
 bool SL::hd(const St& s, double t) const {
  (void)vD;
  if (s.multi_token_dpre_count() > 0) return false;
  const bool tdr_critical = (s.cfg().v0 > 0.0 && s.cfg().w_c > 0.0 &&
  s.oracle().et(s.cfg().s1) > 0.15 * s.cfg().v0);
  if (tdr_critical) return false;
  const int ready_now = s.q6(Phase::qk);
  if (ready_now >= 2) return false;
  if (s.cfg().v0 <= 0.0) {
   const int imminent = s.q6(Phase::qx)
   + s.q6(Phase::qy);
   if (imminent > 0 && s.cfg().w_c > 0.0) {
    if (ready_now == 0 && imminent == 1) return true;
    return false;
   }
   return false;
  }
  double next = 1e300;
  if (s.local().q15) next = std::min(next, s.local().q10);
  for (int c = 0; c < s.qY(); ++c) {
   if (s.q13(c).q15) next = std::min(next, s.q13(c).q10);
  }
  if (next > 1e290) return false;
  const int pipe = s.q6(Phase::qx) + s.q6(Phase::qy);
  if (pipe == 0) return false;
  const int k = s.q6(Phase::qk);
  if (k == 0) return false;
  const Cg& cg = s.cfg();
  const auto& tb = s.tb();
  auto true_period = [&](int m) -> double {
   double t_local = 2.0 * cg.S + tb.get(q14(StepKind::D_PRE), m)
   + tb.get(q14(StepKind::D_POST), m);
   double t_cloud = cg.S + tb.get(q14(StepKind::D_PROC),
   std::max(1, m / s.qY()));
   double t_transfer = q4(cg, static_cast<double>(m));
   return std::max({t_local, t_cloud, t_transfer});
  };
  double P_k  = true_period(k);
  double P_1  = true_period(1);
  double P_k1 = true_period(k + 1);
  double delta_P = P_k + P_1 - P_k1;
  if (delta_P <= 0.0) return false;
  double delta_future = P_k1 - P_k;
  double V_tp = s.oracle().qB(cg.w_tp, cg.tp_base, cg.tp_UB);
  double V_tpot = s.oracle().qA(cg.w_c, cg.v0, cg.s1, cg.s2);
  double V_tdr = s.oracle().qz(cg.w_c, cg.v0, cg.s1, cg.s2);
  if (V_tpot <= 1e-9 && V_tdr <= 1e-9) return true;
  const double tp = s.oracle().ptp();
  const double makespan = s.oracle().q9 - s.oracle().q2;
  if (tp <= 0.0 || makespan <= 1e-9) return false;
  double wait_time = next - t;
  double gain = V_tp * tp * delta_P / makespan;
  double tdr_loss = 0.0;
  if (V_tdr > 1e-9 && s.oracle().ct > 0) {
   tdr_loss = V_tdr * wait_time * static_cast<double>(pipe) / s.oracle().ct;
  }
  double loss = V_tp * tp * wait_time / makespan + V_tpot * delta_future + tdr_loss;
  return gain > loss;
 }
}
using namespace icpc2026;
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
 static constexpr size_t BUF = (1 << 17);
 std::vector<char> buf_;
 size_t pos_ = 0;
};
static double compute_alpha(const Cg& cfg, const qj& tb) {
 if (cfg.v0 == 0.0) {
  const double tproc = q4(cfg, 1.0);
  const double min_TDR = 3.0 * cfg.S
  + tb.get(Step::PP, 1)
  + tb.get(Step::PR, 1)
  + tb.get(Step::PQ, 1)
  + 2.0 * tproc;
  const double min_TPOT = 3.0 * cfg.S
  + tb.get(Step::DP, 1)
  + tb.get(Step::DR, 1)
  + tb.get(Step::DQ, 1)
  + 2.0 * tproc;
  if (min_TDR > cfg.s1 || min_TPOT > cfg.s2) {
   return 1.0;
  }
 }
 {
  const double Rmax = 2000.0;
  const double pre = tb.get(Step::PP, 4096);
  const double proc = tb.get(Step::PR, 4096);
  const double post = tb.get(Step::PQ, 4096);
  const double tr4k = q4(cfg, 4096.0);
  const double worst_tdr = Rmax * (3.0 * cfg.S + pre + proc + post + 2.0 * tr4k);
  const double dpre = std::max(tb.get(Step::DP, 1), tb.get(Step::DP, 2000));
  const double dpost = std::max(tb.get(Step::DQ, 1), tb.get(Step::DQ, 2000));
  const double tr2k = static_cast<double>(cfg.K) * cfg.lm
  + 8.0 * 2000.0 * cfg.bpt / (cfg.bandwidth_gbps * 1e6);
  const double dproc_wc = tb.get(Step::DR, std::max(1, 2000 / std::max(1, cfg.K)));
  const double worst_tpot = Rmax * (3.0 * cfg.S + dpre + dproc_wc + dpost + 2.0 * tr2k);
  if (worst_tdr <= cfg.s1 && worst_tpot <= cfg.s2) {
   return 1.0;
  }
 }
 const double sum_w = cfg.w_tp + cfg.w_c;
 if (sum_w <= 0.0) return 0.5;
 double alpha = cfg.w_tp / sum_w;
 if (alpha < 0.0) alpha = 0.0;
 if (alpha > 1.0) alpha = 1.0;
 return alpha;
}
int main() {
 BufWriter w;
 qf reader;
 Cg cfg;
 qj tb;
 int N = 0;
 std::vector<std::vector<double>> rows;
 if (reader.read_config(cfg) && reader.read_table(N, rows)) {
  tb.load(N, rows);
  St st;
  st.init(cfg);
  st.set_table(tb);
  const double alpha = compute_alpha(cfg, tb);
  std::cerr << "[alpha] " << alpha
  << " (w_tp=" << cfg.w_tp << " w_c=" << cfg.w_c
  << " v0=" << cfg.v0
  << " s1=" << cfg.s1 << " s2=" << cfg.s2 << ")\n";
  PrefillLoadBalanceCloud balance;
  qg marginal;
  SL slo_aware;
  NoDelay no_delay;
  Policies policies;
  policies.alpha = alpha;
  policies.q13 = &balance;
  OnePieceChunk onepiece;
  AdaptiveChunk achunk;
  policies.chunk = (cfg.S < 1.0 && cfg.nl > 8)
  ? static_cast<qi*>(&achunk)
  : static_cast<qi*>(&onepiece);
  policies.batch = &marginal;
  policies.delay = (alpha < 0.8) ? static_cast<qh*>(&slo_aware)
  : static_cast<qh*>(&no_delay);
  policies.qO = (alpha > 0.2);
  policies.qP = 1 + static_cast<int>(alpha * (cfg.K - 1));
  marginal.set_alpha(alpha);
  slo_aware.set_alpha(alpha);
  Se scheduler(cfg, tb, policies);
  qg* mb = dynamic_cast<qg*>(const_cast<vd*>(policies.batch));
  if (mb) {
   mb->precompute(cfg, tb);
  }
  std::vector<Event> events;
  double t = 0.0;
  bool cliff_feasible = false;
  bool cliff_flipped = false;
  long long frame_count = 0;
  const long long MAX_FRAMES = 10000000LL;
  while (reader.next_frame(t, events)) {
   ++frame_count;
   if (frame_count > MAX_FRAMES) {
    w.c_str("0\n");
    w.flush();
    break;
   }
   for (const Event& ev : events) st.apply_event(ev, t);
   st.end_frame();
   if (cfg.v0 == 0.0 && !cliff_flipped && !cliff_feasible) {
    bool any_arr = false;
    for (const Event& ev : events) {
     if (ev.kind != v2::ARR) continue;
     any_arr = true;
     const double fl = 3.0 * cfg.S
     + tb.get(Step::PP, ev.lin)
     + tb.get(Step::PR, ev.lin)
     + tb.get(Step::PQ, ev.lin)
     + 2.0 * q4(cfg, static_cast<double>(ev.lin));
     if (fl <= cfg.s1 + 1e-9) { cliff_feasible = true; break; }
    }
    if (any_arr && !cliff_feasible) {
     cliff_flipped = true;
     policies.delay = &no_delay;
     std::cerr << "[cliff] traffic-aware all-or-nothing flip: "
     "every arrived q12's min TDR > s1; "
     "SLO-driven D PRE holds disabled\n";
    }
   }
   const std::vector<Assignment> assigns = scheduler.decide(st, t);
   w.i64(assigns.size());
   w.nl();
   for (const Assignment& a : assigns) {
    const TaskSpec& sp = a.spec;
    switch (sp.step) {
     case StepKind::P_PRE:
     w.c_str("E P PRE "); w.i64(sp.q13); w.space(); w.i64(sp.ids[0]); w.nl();
     break;
     case StepKind::P_PROC:
     w.c_str("C"); w.i64(sp.q13); w.c_str(" P PROC ");
     w.i64(sp.ls); w.space(); w.i64(sp.le); w.space();
     w.i64(sp.q13); w.space(); w.i64(sp.ids[0]); w.nl();
     break;
     case StepKind::P_POST:
     w.c_str("E P POST "); w.i64(sp.q13); w.space(); w.i64(sp.ids[0]); w.nl();
     break;
     case StepKind::D_PRE:
     w.c_str("E D PRE -1 "); w.i64(sp.ids.size());
     for (int rid : sp.ids) { w.space(); w.i64(rid); }
     w.nl();
     break;
     case StepKind::D_PROC:
     w.c_str("C"); w.i64(sp.q13); w.c_str(" D PROC ");
     w.i64(sp.q13); w.space(); w.i64(sp.ids.size());
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
  const auto& o = st.oracle();
  std::cerr << std::setprecision(12)
  << "[ORACLE FINAL frame=" << frame_count
  << " tdr=" << o.projected_tdr()
  << " tpot=" << o.projected_tpot()
  << "]\n";
  const double mk = o.q9 - o.q2;
  long long ntok = 0;
  double e_min = 0, up_min = 0, e_prefill = 0;
  const int nr = st.num_requests();
  const int mbar = std::max(1, nr);
  for (int i = 0; i < nr; ++i) {
   if (!st.has_request(i)) continue;
   const auto& r = st.q12(i);
   ntok += r.iter;
   e_min += 2.0 * cfg.S + tb.get(Step::PP, r.lin) + tb.get(Step::PQ, r.lin);
   e_prefill += 2.0 * cfg.S + tb.get(Step::PP, r.lin) + tb.get(Step::PQ, r.lin);
   up_min += q4(cfg, static_cast<double>(r.lin));
  }
  e_min += (static_cast<double>(ntok) / mbar) *
  (2.0 * cfg.S + tb.get(Step::DP, mbar) + tb.get(Step::DQ, mbar));
  up_min += (static_cast<double>(ntok) / mbar) *
  static_cast<double>(std::max(1, cfg.K)) *
  q4(cfg, static_cast<double>(mbar) / std::max(1, cfg.K));
  const int m_star = tb.decode_m_star(cfg.S);
  const double f_star = (2.0 * cfg.S + tb.get(Step::DP, m_star)
  + tb.get(Step::DQ, m_star)) / static_cast<double>(m_star);
  const double e_decode = st.e_busy() - e_prefill;
  const double floor_decode = f_star * static_cast<double>(ntok);
  std::cerr << "[METER] mk=" << mk << " E=" << st.e_busy() << "/" << e_min
  << " UP=" << st.up_busy() << "/" << up_min << " C=" << st.c_busy()
  << " tokens=" << ntok << " R=" << nr << " last_arr=" << st.last_arr()
  << " 2*tdr*R=" << 2.0 * o.projected_tdr() * nr << "\n";
  std::cerr << "[ED] Ep=" << e_prefill << " Ed=" << e_decode
  << " fl(m*=" << m_star << ")=" << floor_decode << "\n";
 }
 return 0;
}
