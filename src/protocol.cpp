// =============================================================================
// protocol.cpp — Layer 1 implementation: buffered scanner + frame parser.
//
// FastScanner is a zero-allocation, locale-free reader over stdin with a 4 MiB
// window: tokens are returned as string_views (valid only until the next call),
// and a token that straddles the read boundary is compacted to the front and
// extended in place. TaskSpec::parse_double is a hand-rolled decimal parser
// (with hardened exponent handling) so parsing is deterministic, locale-free,
// and immune to iostream overhead in the 2·10^6-frame worst case.
// =============================================================================
#include "protocol.hpp"

#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <string>
#include <unistd.h>

namespace icpc2026 {

// ---------------------------------------------------------------------------
// FastScanner: buffered, locale-free, zero-allocation token reader over stdin.
// ---------------------------------------------------------------------------

FastScanner::FastScanner() : buf_(BUF), pos_(0), valid_(0) {}

bool FastScanner::fill() {
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
        if (r == 0) return false;           // EOF
        if (errno != EINTR && errno != EAGAIN) return false; // error
        // else retry on EINTR/EAGAIN
    }
}

bool FastScanner::eof() {
    if (pos_ < valid_) return false;
    return !fill();
}

std::string_view FastScanner::next_token() {
    // Skip whitespace.
    for (;;) {
        while (pos_ < valid_) {
            char c = buf_[pos_];
            if (c != ' ' && c != '\n' && c != '\t' && c != '\r') break;
            ++pos_;
        }
        if (pos_ < valid_) break;
        if (!fill()) return {};               // EOF, no more tokens
    }
    size_t start = pos_;
    for (;;) {
        while (pos_ < valid_) {
            char c = buf_[pos_];
            if (c == ' ' || c == '\n' || c == '\t' || c == '\r')
                return std::string_view(buf_.data() + start, pos_ - start);
            ++pos_;
        }
        // Token runs to the end of the valid window with no separator yet.
        // Compact the partial token bytes to the front and read more data.
        size_t keep = valid_ - start;
        if (keep > 0) std::memmove(buf_.data(), buf_.data() + start, keep);
        pos_ = keep;
        valid_ = keep;
        size_t cap = BUF - valid_;
        if (cap == 0) return std::string_view(buf_.data(), valid_);
        ssize_t r = ::read(STDIN_FILENO, buf_.data() + valid_, cap);
        if (r <= 0) return std::string_view(buf_.data(), valid_);  // EOF or error
        valid_ += static_cast<size_t>(r);
        start = 0;
        // continue scanning from pos_ (== keep == 0 after compaction)
    }
}

int FastScanner::next_int() {
    std::string_view v = next_token();
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

long long FastScanner::next_long_long() {
    std::string_view v = next_token();
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

double FastScanner::next_double() {
    std::string_view tok = next_token();
    if (tok.empty()) return 0.0;
    return TaskSpec::parse_double(tok);
}

// ---------------------------------------------------------------------------
// ProtocolReader
// ---------------------------------------------------------------------------

bool ProtocolReader::read_config(Config& cfg) {
    cfg.K = in_.next_int();
    cfg.S = in_.next_double();
    cfg.latency_ms = in_.next_double();
    cfg.bandwidth_gbps = in_.next_double();
    cfg.bytes_per_token = in_.next_int();
    cfg.num_layers = in_.next_int();
    cfg.SLO1 = in_.next_double();
    cfg.SLO2 = in_.next_double();
    cfg.tp_UB = in_.next_double();
    cfg.tp_base = in_.next_double();
    cfg.dist_base = in_.next_double();
    cfg.w_tp = in_.next_double();
    cfg.w_c = in_.next_double();
    return true;
}

bool ProtocolReader::read_table(int& N, std::vector<std::vector<double>>& rows) {
    N = in_.next_int();
    if (N <= 0) return false;
    rows.assign(static_cast<size_t>(N), std::vector<double>(7));
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < 7; ++j)
            rows[i][j] = in_.next_double();
    return true;
}

bool ProtocolReader::next_frame(double& t, std::vector<Event>& events) {
    // First token is either the timestamp (decimal) or "END".
    std::string_view first = in_.next_token();
    if (first.empty()) return false;            // EOF
    if (first == "END") return false;
    t = TaskSpec::parse_double(first);

    int e = in_.next_int();
    assert(e >= 0);
    events.clear();
    events.reserve(static_cast<size_t>(e));

    if (e == 0) {
        // No events in this frame, but we still need to return true
        return true;
    }

    for (int i = 0; i < e; ++i) {
        Event ev;
        std::string_view kind = in_.next_token();
        if (kind == "ARR") {
            ev.kind = EventKind::ARR;
            ev.rid = in_.next_int();
            ev.lin = in_.next_int();
        } else if (kind == "TDN") {
            ev.kind = EventKind::TDN;
            std::string_view server_s = in_.next_token();
            if (server_s == "E") {
                ev.server = -1;
            } else {
                assert(server_s.size() >= 2 && server_s[0] == 'C');
                ev.server = TaskSpec::parse_int(std::string_view(server_s).substr(1));
            }
            std::string_view w1 = in_.next_token();
            std::string_view w2 = in_.next_token();
            ev.spec = {std::string(w1), std::string(w2)};
            if (w1 == "P") {
                if (w2 == "PROC") {
                    ev.spec.push_back(std::string(in_.next_token())); // a
                    ev.spec.push_back(std::string(in_.next_token())); // b
                    ev.spec.push_back(std::string(in_.next_token())); // c
                    ev.spec.push_back(std::string(in_.next_token())); // d
                } else {
                    // PRE or POST
                    ev.spec.push_back(std::string(in_.next_token())); // a
                    ev.spec.push_back(std::string(in_.next_token())); // b
                }
            } else {
                // D PRE / D POST / D PROC
                std::string_view x = in_.next_token();
                std::string_view ms = in_.next_token();
                ev.spec.push_back(std::string(x));
                ev.spec.push_back(std::string(ms));
                const int m = TaskSpec::parse_int(ms);
                assert(m >= 1);
                for (int j = 0; j < m; ++j)
                    ev.spec.push_back(std::string(in_.next_token()));
            }
            std::string_view dur_token = in_.next_token();
            ev.dur = TaskSpec::parse_double(dur_token);
            assert(ev.dur > 0.0);
        } else if (kind == "XDN") {
            ev.kind = EventKind::XDN;
            std::string_view dir = in_.next_token();
            ev.remote = in_.next_int();
            ev.size = in_.next_long_long();
            std::string_view tag = in_.next_token();
            ev.up = (dir == "UP");
            ev.pre = (tag == "PRE");
            int m = in_.next_int();
            assert(m >= 1);
            ev.ids.resize(static_cast<size_t>(m));
            for (int j = 0; j < m; ++j) ev.ids[static_cast<size_t>(j)] = in_.next_int();
        } else if (kind == "FIN") {
            ev.kind = EventKind::FIN;
            ev.rid = in_.next_int();
        } else {
            std::fprintf(stderr, "[DEBUG] Unknown event kind: '%s'\n", std::string(kind).c_str());
            std::fflush(stderr);
            assert(false);
        }
        events.push_back(std::move(ev));
    }
    return true;
}

bool TaskSpec::parse(const std::vector<std::string>& tokens, const Config& cfg) {
    (void)cfg;
    if (tokens.size() < 2) return false;
    const std::string& w1 = tokens[0];
    const std::string& w2 = tokens[1];
    auto atoi_ = [&](size_t i) { return TaskSpec::parse_int(tokens[i]); };

    ids.clear();
    if (w1 == "P" && w2 == "PRE") {
        if (tokens.size() != 4) return false;
        step = StepKind::P_PRE;
        cloud = atoi_(2);
        ids = {atoi_(3)};
        ls = le = 0;
    } else if (w1 == "P" && w2 == "PROC") {
        if (tokens.size() != 6) return false;
        step = StepKind::P_PROC;
        ls = atoi_(2);
        le = atoi_(3);
        cloud = atoi_(4);
        ids = {atoi_(5)};
    } else if (w1 == "P" && w2 == "POST") {
        if (tokens.size() != 4) return false;
        step = StepKind::P_POST;
        cloud = atoi_(2);
        ids = {atoi_(3)};
        ls = le = 0;
    } else if (w1 == "D" && (w2 == "PRE" || w2 == "POST")) {
        if (tokens.size() < 4) return false;
        const int m = atoi_(3);
        if (static_cast<int>(tokens.size()) != 4 + m) return false;
        step = (w2 == "PRE") ? StepKind::D_PRE : StepKind::D_POST;
        cloud = -1;
        for (int j = 0; j < m; ++j) ids.push_back(atoi_(4 + static_cast<size_t>(j)));
        ls = le = 0;
    } else if (w1 == "D" && w2 == "PROC") {
        if (tokens.size() < 4) return false;
        const int m = atoi_(3);
        if (static_cast<int>(tokens.size()) != 4 + m) return false;
        step = StepKind::D_PROC;
        cloud = atoi_(2);
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
        v.push_back(std::to_string(cloud));
        v.push_back(std::to_string(ids[0]));
    } else if (step == StepKind::P_PROC) {
        v.push_back(std::to_string(ls));
        v.push_back(std::to_string(le));
        v.push_back(std::to_string(cloud));
        v.push_back(std::to_string(ids[0]));
    } else {
        v.push_back(step == StepKind::D_PROC ? std::to_string(cloud) : std::string("-1"));
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
            // Harden: a token like "1e1000000" would spin the multiplicative
            // loop ~10^6+ times per token (potential TLE). Exact multiplication
            // by 10.0 is only used for the small, realistically-printed
            // exponents (the judge emits plain 9-decimal notation anyway);
            // everything else goes through pow, which is deterministic and
            // correct to <2 ULP here.
            const double p = std::pow(10.0, static_cast<double>(eneg ? -e : e));
            x *= p;
        } else {
            while (e > 0) { if (eneg) x /= 10.0; else x *= 10.0; --e; }
        }
    }
    return neg ? -x : x;
}

}
