#!/usr/bin/env python3
"""tools/visualize_trace.py — PART 9.3 bottleneck visualizer.

Reads a PART 9 trace (CONFIG + TDN + XDN) and draws an ASCII Gantt of the
local computer, each cloud, and the UP/DOWN transfer FIFOs over time, plus
bottleneck heuristics (9.3 "what to look for"):

  - Cloud idle while UP queue long     -> transfer bottleneck (needs staggering)
  - Local idle while clouds busy       -> local scheduling issue
  - UP and DOWN both active            -> good overlap
  - One cloud overloaded, another idle -> load-balancing failure

Busy intervals are reconstructed exactly from the trace:
  - TDN event at t with dur d and setup S => task occupied its server during
    [t - S - d, t]  (srv "E" = local, "Ck" = cloud k).
  - XDN event at t with payload size => transfer occupied the link during
    [t - transfer(size), t]; transfer(size) = latency + 8*size*bpt/(bw*1e6).

Usage:
  python3 tools/visualize_trace.py --trace /tmp/trace.txt
  python3 tools/visualize_trace.py --trace /tmp/trace.txt --width 120
"""
import argparse
import sys


def transfer_dur(latency, bw, size):
    # `size` from the trace XDN line is already data_bytes (len * bpt), matching
    # the interactor's transfer(): latency + 8*length/(bw*1e6). Do NOT multiply
    # by bpt again here.
    return latency + 8.0 * size / (bw * 1e6)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trace", required=True)
    ap.add_argument("--width", type=int, default=80)
    args = ap.parse_args()

    cfg = {}
    busy = {}   # resource name -> list of (start, end, label)

    def res(name):
        return busy.setdefault(name, [])

    with open(args.trace) as f:
        for line in f:
            line = line.rstrip("\n")
            if not line:
                continue
            if line.startswith("CONFIG"):
                toks = line.split()
                cfg["K"] = int(toks[1]); cfg["S"] = float(toks[2])
                cfg["latency"] = float(toks[3]); cfg["bw"] = float(toks[4])
                cfg["bpt"] = int(toks[5]); cfg["layers"] = int(toks[6])
                continue
            if not line.startswith("EVENT"):
                continue
            parts = line.split()
            t = float(parts[1].split("=")[1])
            kind = parts[2]
            if kind == "TDN":
                rest = line.split("TDN ", 1)[1]
                specstr, durp = rest.rsplit(" dur=", 1)
                srv = specstr.split()[0]
                d = float(durp)
                start = t - cfg["S"] - d
                name = "LOCAL" if srv == "E" else srv
                res(name).append((start, t, specstr.split()[1]))
            elif kind == "XDN":
                # EVENT t=.. XDN <dir> <remote> <size> <tag> <count> <ids...>
                direction = parts[3]
                size = int(parts[5])
                d = transfer_dur(cfg["latency"], cfg["bw"], size)
                start = t - d
                res(direction).append((start, t, direction))

    if not cfg:
        sys.stderr.write("error: no CONFIG line in trace\n")
        return 1

    K = cfg["K"]
    resources = ["LOCAL"] + [f"C{k}" for k in range(K)] + ["UP", "DOWN"]

    # overall time span
    all_ends = [e for r in resources for (_, e, _) in busy.get(r, [])]
    if not all_ends:
        sys.stderr.write("error: no TDN/XDN intervals in trace\n")
        return 1
    T = max(all_ends)
    W = max(10, args.width)

    def col(x):
        if T <= 0:
            return 0
        c = int(x / T * W)
        if c < 0:
            c = 0
        if c >= W:
            c = W - 1
        return c

    print(f"=== PART 9.3 Gantt (time 0 .. {T:.3f}) ===")
    # time axis ticks
    ticks = [0.0, T / 2.0, T]
    axis = [" "] * (W + 8)
    for tk in ticks:
        p = col(tk)
        lbl = f"{tk:.1f}"
        for i, ch in enumerate(lbl):
            if p + 8 + i < len(axis):
                axis[p + 8 + i] = ch
    print(" " * 8 + "".join(axis[8:]))
    for r in resources:
        row = ["."] * W
        for (s, e, _) in busy.get(r, []):
            a, b = col(s), col(e)
            for c in range(a, max(a + 1, b + 1)):
                row[c] = "X"
        print(f"{r:>7} |" + "".join(row))

    # ---- bottleneck stats ----
    def busy_frac(name):
        ivs = busy.get(name, [])
        if not ivs or T <= 0:
            return 0.0, 0.0
        merged = []
        for s, e, _ in sorted(ivs):
            s, e = min(s, e), max(s, e)
            if merged and s <= merged[-1][1] + 1e-9:
                merged[-1] = (merged[-1][0], max(merged[-1][1], e))
            else:
                merged.append((s, e))
        tot = sum(max(0.0, e - s) for (s, e) in merged)
        return tot / T, tot

    def max_concurrency(name):
        ivs = sorted(busy.get(name, []))
        best = 0
        active = 0
        events = []
        for (s, e, _) in ivs:
            events.append((s, 1))
            events.append((e, -1))
        events.sort(key=lambda x: (x[0], x[1]))
        for _, d in events:
            active += d
            best = max(best, active)
        return best

    print("\n--- utilization (fraction of full timeline + absolute busy seconds) ---")
    local_frac, local_sec = busy_frac("LOCAL")
    print(f"LOCAL : {local_frac*100:5.2f}% busy ({local_sec:.1f}s of T={T:.1f}s)")
    cloud_fracs = []
    for k in range(K):
        f_, s_ = busy_frac(f"C{k}")
        cloud_fracs.append(f_)
        print(f"C{k:<5}: {f_*100:5.2f}% busy ({s_:.1f}s)")
    up_max = max_concurrency("UP")
    down_max = max_concurrency("DOWN")
    up_frac, up_sec = busy_frac("UP")
    down_frac, down_sec = busy_frac("DOWN")
    print(f"UP  : {up_frac*100:5.2f}% busy ({up_sec:.1f}s)   DOWN: {down_frac*100:5.2f}% busy ({down_sec:.1f}s)")
    print(f"UP  max concurrency: {up_max}   DOWN max concurrency: {down_max}")

    print("\n--- heuristics (9.3) ---")
    # load-balancing failure: spread of cloud busy fractions
    if K > 1 and cloud_fracs:
        lo, hi = min(cloud_fracs), max(cloud_fracs)
        if hi - lo > 0.25:
            print(f"  [!] LOAD IMBALANCE: cloud busy spread {lo*100:.0f}%-{hi*100:.0f}% "
                  f"(one cloud overloaded while another idle)")
        else:
            print("  [ok] clouds reasonably balanced")
    # transfer bottleneck: links saturated while clouds idle
    idle_clouds = [k for k in range(K) if cloud_fracs[k] < 0.5]
    link_sat = (up_frac > 0.3 or down_frac > 0.3) or up_max >= 5
    if link_sat and idle_clouds:
        print(f"  [!] TRANSFER BOTTLENECK: links saturated (UP {up_frac*100:.0f}%/max{up_max}, "
               f"DOWN {down_frac*100:.0f}%) while cloud(s) {idle_clouds} <50% busy "
               f"(consider staggering prefill / smarter chunking)")
    elif link_sat:
        print(f"  [i] links saturated (UP {up_frac*100:.0f}%/max{up_max}) but clouds also busy")
    else:
        print("  [ok] UP/DOWN links under-utilized (compute-bound)")
    # local idle while clouds busy
    busy_clouds = [k for k in range(K) if cloud_fracs[k] > 0.8]
    if local_frac < 0.5 and busy_clouds:
        print(f"  [!] LOCAL IDLE: local {local_frac*100:.0f}% busy while cloud(s) busy".replace("busy", "busy")
              + f" {busy_clouds} >80% (local scheduling issue)")
    elif local_frac > 0.8 and up_max >= 2:
        print("  [i] local + links both busy (tight pipeline)")
    else:
        print("  [ok] local/cloud overlap reasonable")
    return 0


if __name__ == "__main__":
    sys.exit(main())
