#!/usr/bin/env python3
"""SA06 probe solver: SA04 mysolver clone + knobs:
  --abandon=RID       never issue P PRE for rid (permanently refuses)
  --zero-frames=N     respond 0 for the first N frames, then run normally
  --hold-ppre=RID,MS  delay rid's P PRE by MS ms after ARR (needs wake-ups)
  --decode-batch      batch ALL decode-ready requests into one D PRE/D POST
Batch decode groups span clouds; D PROC per cloud is not needed here because
this scripted solver only issues D PRE groups of size m across clouds and
the judge splits per-cloud UPs automatically; D PROC must be per-cloud:
for --decode-batch we schedule one D PROC per cloud for its members.
"""
import os
import sys


def flush(acts):
    sys.stdout.write("%d\n" % len(acts))
    for a in acts:
        sys.stdout.write(a + "\n")
    sys.stdout.flush()


def main():
    abandon = set()
    zero_frames = 0
    stop_after = -1
    hold_ppre = {}
    hold_dpre1 = {}
    decode_batch = False
    for a in os.environ.get("SA06_ARGS", "").split():
        if a.startswith("--abandon="):
            abandon = set(int(x) for x in a.split("=", 1)[1].split(",") if x != "")
        elif a.startswith("--zero-frames="):
            zero_frames = int(a.split("=", 1)[1])
        elif a.startswith("--stop-after-frame="):
            stop_after = int(a.split("=", 1)[1])
        elif a.startswith("--hold-ppre="):
            kv = dict(s.split("=", 1) for s in a.split("=", 1)[1].split(","))
            hold_ppre[kv["rid"]] = float(kv["ms"])
        elif a.startswith("--hold-dpre1="):
            kv = dict(s.split("=", 1) for s in a.split("=", 1)[1].split(","))
            hold_dpre1[kv["rid"]] = float(kv["ms"])
        elif a == "--decode-batch":
            decode_batch = True

    K, S, lat, bw, bpt, layers = sys.stdin.readline().split()
    K, S, layers = int(K), float(S), int(layers)
    sys.stdin.readline()
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

    PP, PROC_R, PO_R, DPRE, DPROC, DPOST = range(6)
    RUN, DONE = -1, -2
    P = {}
    local_busy = 0.0
    cloud_busy = [0.0] * K
    rr = 0
    fno = 0
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
                P[rid] = dict(ph=PP, lin=lin, cloud=-1, itok=0, lout=None,
                              arr=t, dpre_ready=0.0, ppost_ready=0.0, ppre_ready=t)
            elif kind == "TDN":
                cat, sub = ev[2], ev[3]
                if cat == "P" and sub == "POST":
                    rid = int(ev[5])
                    if P[rid]["ph"] != DONE:
                        P[rid]["ph"] = DPRE
                        P[rid]["dpre_ready"] = t
                elif cat == "D" and sub == "POST":
                    for rid in [int(x) for x in ev[6:-1]]:
                        if P[rid]["ph"] != DONE:
                            P[rid]["ph"] = DPRE
                            P[rid]["dpre_ready"] = t
            elif kind == "XDN":
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

        fno += 1
        if fno <= zero_frames or (stop_after >= 0 and fno > stop_after):
            flush([])
            continue

        acts = []
        # 1. P PRE (abandon-gated, hold-gated)
        for rid in sorted(P):
            if len(acts) >= K + 1:
                break
            r = P[rid]
            if r["ph"] == PP and rid not in abandon and t >= local_busy - EPS:
                hold = hold_ppre.get(str(rid), 0.0)
                if t >= r["ppre_ready"] + hold - EPS:
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
        # 3. P POST
        for rid in sorted(P):
            if len(acts) >= K + 1:
                break
            r = P[rid]
            if r["ph"] == PO_R and t >= local_busy - EPS:
                acts.append("E P POST %d %d" % (r["cloud"], rid))
                local_busy = t + S + tdur(2, r["lin"])
                r["ph"] = RUN
        # 4. D PRE
        if decode_batch:
            grp = [rid for rid in sorted(P) if P[rid]["ph"] == DPRE
                   and (P[rid]["itok"] > 0 or t >= P[rid]["dpre_ready"]
                        + hold_dpre1.get(str(rid), hold_dpre1.get("*", 0.0)) - EPS)]
            if grp and t >= local_busy - EPS:
                m = len(grp)
                acts.append("E D PRE -1 %d %s" % (m, " ".join(str(x) for x in grp)))
                local_busy = t + S + tdur(3, m)
                for rid in grp:
                    P[rid]["ph"] = RUN
        else:
            for rid in sorted(P):
                if len(acts) >= K + 1:
                    break
                r = P[rid]
                if r["ph"] == DPRE and t >= local_busy - EPS:
                    acts.append("E D PRE -1 1 %d" % rid)
                    local_busy = t + S + tdur(3, 1)
                    r["ph"] = RUN
        # 5. D PROC: per cloud, group members currently in DPROC on that cloud
        bycloud = {}
        for rid in sorted(P):
            r = P[rid]
            if r["ph"] == DPROC:
                bycloud.setdefault(r["cloud"], []).append(rid)
        for c in sorted(bycloud):
            grp = bycloud[c]
            if t >= cloud_busy[c] - EPS:
                m = len(grp)
                acts.append("C%d D PROC %d %d %s" % (c, c, m, " ".join(str(x) for x in grp)))
                cloud_busy[c] = t + S + tdur(4, m)
                for rid in grp:
                    P[rid]["ph"] = RUN
        # 6. D POST: group all DPOST-ready
        if decode_batch:
            grp = [rid for rid in sorted(P) if P[rid]["ph"] == DPOST]
            if grp and t >= local_busy - EPS:
                m = len(grp)
                acts.append("E D POST -1 %d %s" % (m, " ".join(str(x) for x in grp)))
                local_busy = t + S + tdur(5, m)
                for rid in grp:
                    P[rid]["itok"] += 1
                    P[rid]["ph"] = RUN
        else:
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
