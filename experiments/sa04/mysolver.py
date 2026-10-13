#!/usr/bin/env python3
"""SA04 scripted micro-solver: full-protocol, tiny-case, hold-knob controlled.

Knobs (argv):
  --hold-dpre rid=<r|*>,round=<k>,ms=<x>  hold request r's k-th decode round
      (round 1 = first decode) for x ms after it becomes decode-ready.
  --hold-ppost rid=<r|*>,ms=<x>           hold request r's P POST x ms after
      it becomes P POST-ready (XDN DOWN PRE).

Batch decode = 1 always. One P PROC chunk. Dense EMPTY wake-ups in the case
file make holds fire exactly. Responds to every frame, flushes.
"""
import os
import sys


def flush(acts):
    sys.stdout.write("%d\n" % len(acts))
    for a in acts:
        sys.stdout.write(a + "\n")
    sys.stdout.flush()


def main():
    hold_dpre = {}   # (rid_or_'*', round) -> ms
    hold_ppost = {}  # rid_or_'*' -> ms
    for a in os.environ.get("SA04_ARGS", "").split():
        if a.startswith("--hold-dpre="):
            kv = dict(s.split("=", 1) for s in a.split("=", 1)[1].split(","))
            hold_dpre[(kv["rid"], int(kv["round"]))] = float(kv["ms"])
        elif a.startswith("--hold-ppost="):
            kv = dict(s.split("=", 1) for s in a.split("=", 1)[1].split(","))
            hold_ppost[kv["rid"]] = float(kv["ms"])

    K, S, lat, bw, bpt, layers = sys.stdin.readline().split()
    K, S, layers = int(K), float(S), int(layers)
    sys.stdin.readline()  # scoring line (SLO1..w_c) — consumed, unused
    N = int(sys.stdin.readline())
    rows = [[float(x) for x in sys.stdin.readline().split()] for _ in range(N)]

    def tdur(col, batch):
        pairs = sorted((int(r[0]), r[col + 1]) for r in rows if r[col + 1] != -1.0)
        if batch <= pairs[0][0]:
            return pairs[0][1]
        if batch >= pairs[-1][0]:
            return pairs[-1][1]
        for (b1, v1), (b2, v2) in zip(pairs, pairs[1:]):
            if b1 == batch:
                return v1
            if b1 < batch < b2:
                return v1 + (v2 - v1) * (batch - b1) / (b2 - b1)
        raise AssertionError

    PP, PROC_R, PO_R, DPRE, DPROC, DPOST = range(6)  # ready states
    RUN, DONE = -1, -2
    P = {}
    local_busy = 0.0
    cloud_busy = [0.0] * K
    rr = 0
    EPS = 1e-9

    while True:
        line = sys.stdin.readline()
        if not line or line.strip() == "END":
            break
        t = float(line)
        e = int(sys.stdin.readline())
        for _ in range(e):
            ev = sys.stdin.readline().split()
            kind = ev[0]
            if kind == "ARR":
                rid, lin = int(ev[1]), int(ev[2])
                P[rid] = dict(ph=PP, lin=lin, cloud=-1, itok=0,
                              dpre_ready=0.0, ppost_ready=0.0)
            elif kind == "TDN":
                # TDN <srv> <cat> <sub> ... dur  (dur = last token)
                cat, sub = ev[2], ev[3]
                if cat == "P" and sub == "POST":
                    rid = int(ev[5])
                    if P[rid]["ph"] != DONE:
                        P[rid]["ph"] = DPRE
                        P[rid]["dpre_ready"] = t
                elif cat == "D" and sub == "POST":
                    # TDN E D POST -1 m ids... dur
                    for rid in [int(x) for x in ev[6:-1]]:
                        if P[rid]["ph"] != DONE:
                            P[rid]["ph"] = DPRE
                            P[rid]["dpre_ready"] = t
                # other TDNs: readiness arrives via XDN
            elif kind == "XDN":
                # XDN <dir> <remote> <size> <tag> <cnt> <ids...>
                d, tag = ev[1], ev[4]
                ids = [int(x) for x in ev[6:]]
                if tag == "PRE":
                    for rid in ids:
                        if d == "UP":
                            P[rid]["ph"] = PROC_R
                        else:
                            P[rid]["ph"] = PO_R
                            P[rid]["ppost_ready"] = t
                else:
                    for rid in ids:
                        if d == "UP":
                            P[rid]["ph"] = DPROC
                        else:
                            P[rid]["ph"] = DPOST
            elif kind == "FIN":
                rid = int(ev[1])
                P[rid]["ph"] = DONE

        acts = []
        # 1. P PRE
        for rid in sorted(P):
            if len(acts) >= K + 1:
                break
            r = P[rid]
            if r["ph"] == PP and t >= local_busy - EPS:
                c = rr % K
                rr += 1
                acts.append("E P PRE %d %d" % (c, rid))
                local_busy = t + S + tdur(0, r["lin"])
                r["cloud"] = c
                r["ph"] = RUN
        # 2. P PROC (single chunk)
        for rid in sorted(P):
            if len(acts) >= K + 1:
                break
            r = P[rid]
            if r["ph"] == PROC_R and t >= cloud_busy[r["cloud"]] - EPS:
                acts.append("C%d P PROC 0 %d %d %d" % (r["cloud"], layers, r["cloud"], rid))
                cloud_busy[r["cloud"]] = t + S + tdur(1, r["lin"])
                r["ph"] = RUN
        # 3. P POST (hold-gated)
        for rid in sorted(P):
            if len(acts) >= K + 1:
                break
            r = P[rid]
            if r["ph"] == PO_R:
                hold = hold_ppost.get(str(rid), hold_ppost.get("*", 0.0))
                if t >= local_busy - EPS and t >= r["ppost_ready"] + hold - EPS:
                    acts.append("E P POST %d %d" % (r["cloud"], rid))
                    local_busy = t + S + tdur(2, r["lin"])
                    r["ph"] = RUN
        # 4. D PRE batch=1 (hold-gated per round)
        for rid in sorted(P):
            if len(acts) >= K + 1:
                break
            r = P[rid]
            if r["ph"] == DPRE:
                rnd = r["itok"] + 1
                hold = hold_dpre.get((str(rid), rnd), hold_dpre.get(("*", rnd), 0.0))
                if t >= local_busy - EPS and t >= r["dpre_ready"] + hold - EPS:
                    acts.append("E D PRE -1 1 %d" % rid)
                    local_busy = t + S + tdur(3, 1)
                    r["ph"] = RUN
        # 5. D PROC batch=1
        for rid in sorted(P):
            if len(acts) >= K + 1:
                break
            r = P[rid]
            if r["ph"] == DPROC and t >= cloud_busy[r["cloud"]] - EPS:
                acts.append("C%d D PROC %d 1 %d" % (r["cloud"], r["cloud"], rid))
                cloud_busy[r["cloud"]] = t + S + tdur(4, 1)
                r["ph"] = RUN
        # 6. D POST batch=1
        for rid in sorted(P):
            if len(acts) >= K + 1:
                break
            r = P[rid]
            if r["ph"] == DPOST and t >= local_busy - EPS:
                acts.append("E D POST -1 1 %d" % rid)
                local_busy = t + S + tdur(5, 1)
                r["itok"] += 1
                r["ph"] = RUN
        flush(acts)


if __name__ == "__main__":
    main()
