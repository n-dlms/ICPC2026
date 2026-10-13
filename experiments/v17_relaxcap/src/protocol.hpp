#ifndef ICPC2026_PROTOCOL_HPP
#define ICPC2026_PROTOCOL_HPP

// Layer 1: protocol I/O. No scheduling logic.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace icpc2026 {

constexpr double EPS = 1e-9;

struct Config {
    int K = 0;
    double S = 0.0;
    double latency_ms = 0.0;
    double bandwidth_gbps = 0.0;
    int bytes_per_token = 0;
    int num_layers = 0;
    double SLO1 = 0.0, SLO2 = 0.0;
    double tp_UB = 0.0, tp_base = 0.0, dist_base = 0.0;
    double w_tp = 0.0, w_c = 0.0;
};

inline double transfer_time(const Config& c, double len) {
    return c.latency_ms + 8.0 * len * c.bytes_per_token / (c.bandwidth_gbps * 1e6);
}

enum class EventKind : uint8_t { ARR, TDN, XDN, FIN };

struct Event {
    EventKind kind = EventKind::ARR;
    int rid = -1;   // ARR / FIN
    int lin = 0;    // ARR
    int server = -2;                 // TDN: -1 = E, [0,K) = Ck
    std::vector<std::string> spec;   // TDN: canonical spec tokens (SSO-owned)
    double dur = 0.0;                // TDN
    bool up = false;                 // XDN: UP vs DOWN
    int remote = -1;                 // XDN
    long long size = 0;              // XDN: bytes
    bool pre = false;                // XDN: input vs output stage
    std::vector<int> ids;            // XDN / TDN id members
};

// Zero-allocation, locale-free buffered scanner over stdin. All token
// views returned by next_token() are valid only until the next call to a
// reader method; callers must consume (copy) a token before reading the next.
// This is safe by construction in Layer 1: every token is immediately folded
// into an Event field (owned std::string / int) before the next token is read.
class FastScanner {
public:
    FastScanner();
    bool eof();                         // true if input exhausted and buffer empty
    std::string_view next_token();      // skip ws; view of next token (invalidated on next call)
    int next_int();                     // ws-skip + manual int parse
    double next_double();               // ws-skip + manual decimal parse (handles eE)

private:
    static constexpr size_t BUF = 1 << 22; // 4 MiB: frames are tiny (<4 MiB each)
    std::vector<char> buf_;
    size_t pos_;    // read cursor
    size_t valid_;  // bytes currently valid in buf_
    bool fill();    // compact unconsumed to front + read() more; false on EOF
};

class ProtocolReader {
public:
    explicit ProtocolReader() = default;
    bool read_config(Config& cfg);
    bool read_table(int& N, std::vector<std::vector<double>>& rows);
    bool next_frame(double& t, std::vector<Event>& events);
private:
    FastScanner in_;
};

enum class StepKind : uint8_t { P_PRE, P_PROC, P_POST, D_PRE, D_PROC, D_POST };

struct TaskSpec {
    StepKind step = StepKind::P_PRE;
    int cloud = 0;
    int ls = 0, le = 0;
    std::vector<int> ids;
    bool parse(const std::vector<std::string>& tokens, const Config& cfg);
    std::vector<std::string> tokens() const;

    // Standalone, allocation-free parsers over a token view (hot path).
    static int parse_int(std::string_view v);
    static double parse_double(std::string_view v);
};

}

#endif
