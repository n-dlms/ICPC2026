#!/usr/bin/env python3
"""SA09 probe solver: minimal protocol-correct Python scheduler.
(a) validates SA09 ceiling bounds (no policy may exceed them);
(b) probes how close alternative structures (lockstep mega-groups, cohort
pipelining, threshold holds) get to the E floor on wave-pipelining candidates.

Policies (--policy):
  greedy    : D PRE with ALL currently-ready requests whenever local free
  mega      : lockstep — round fires only when EVERY unfinished request is ready
  cohorts:C : requests split into C cohorts (arrival order mod C); a cohort's
              round fires when all of that cohort's unfinished members are ready
  hold:T    : fire when >= T ready or all unfinished ready
Local priority: D POST > D PRE > P POST > P PRE. Prefill: single chunk.
Cloud choice at P PRE: least estimated load. All waits event-backed.
"""
import argparse
import sys

NEW, P_WUP, P_RPROC, P_WDN, P_RPOST = 0, 1, 2, 3, 4
DEC_R, DEC_WUP, DEC_RPROC, DEC_WDN, DEC_RPOST, DONE = 5, 6, 7, 8, 9, 10


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--policy", default="greedy")
    args = ap.parse_args()
    pol = args.policy

    inp = sys.stdin
    out = sys.stdout

    def rdline():
        line = inp.readline()
        if not line:
            sys.exit(0)
        return line.split()

    hdr = rdline()
    K, S, layers = int(hdr[0]), float(hdr[1]), int(hdr[5])
    import os
    force_cloud = int(os.environ.get("PROBE_CLOUD", "-1"))  # -1 = auto EFT
    rdline()  # scoring line
    N = int(float(rdline()[0]))
    rows = []
    for _ in range(N):
        t = rdline()
        rows.append((int(float(t[0])), float(t[1]), float(t[2]), float(t[3]),
                     float(t[4]), float(t[5]), float(t[6])))
    cols = {}
    for col in range(1, 7):
        rs = sorted((r for r in rows if r[col] >= 0), key=lambda r: r[0])
        cols[col] = rs

    def dur(col, size):
        rs = cols[col]
        if not rs:
            return 1.0
        if size <= rs[0][0]:
            return rs[0][col]
        if size >= rs[-1][0]:
            return rs[-1][col]
        for a, b in zip(rs, rs[1:]):
            if a[0] <= size <= b[0]:
                if b[0] == a[0]:
                    return a[col]
                f = (size - a[0]) / (b[0] - a[0])
                return a[col] + f * (b[col] - a[col])
        return rs[-1][col]

    # ---------------- state
    cloud_of = {}       # rid -> cloud
    lin_of = {}
    phase_of = {}
    NC = int(pol.split(":")[1]) if pol.startswith("cohorts:") else 1
    hold_T = int(pol.split(":")[1]) if pol.startswith("hold:") else None
    cohort_of = {}
    cohort_unfin = [0] * NC
    cohort_ready = [0] * NC
    n_arrived = 0
    n_unfin = 0

    local_busy = False
    cloud_busy = [False] * K
    cloud_load = [0.0] * K
    q_new = []                      # rids awaiting P PRE (kept SJF by lin)
    q_pproc = []                    # rids awaiting P PROC (cloud chosen)
    q_ppost = []                    # rids awaiting P POST
    dec_r = set()                   # ready for next D PRE
    dec_rproc = [set() for _ in range(K)]   # UP arrived, awaiting D PROC
    dec_rpost = set()               # DOWN arrived, awaiting D POST

    def respond(assigns):
        out.write(f"{len(assigns)}\n")
        for a in assigns:
            out.write(a + "\n")
        out.flush()

    while True:
        tline = rdline()
        if tline and tline[0] == "END":
            sys.exit(0)
        eline = rdline()
        e = int(float(eline[0]))
        for _ in range(e):
            ev = rdline()
            kind = ev[0]
            if kind == "ARR":
                rid, lin = int(ev[1]), int(ev[2])
                lin_of[rid] = lin
                phase_of[rid] = NEW
                ci = n_arrived % NC
                n_arrived += 1
                cohort_of[rid] = ci
                cohort_unfin[ci] += 1
                n_unfin += 1
                q_new.append(rid)
            elif kind == "TDN":
                spec = ev[2:]
                step = spec[1]
                if step == "PRE" and spec[0] == "P":
                    local_busy = False
                elif step == "PROC" and spec[0] == "P":
                    cloud_busy[int(spec[4])] = False
                    phase_of[int(spec[5])] = P_WDN
                elif step == "POST" and spec[0] == "P":
                    local_busy = False
                    rid = int(spec[3])
                    phase_of[rid] = DEC_R
                    dec_r.add(rid)
                    cohort_ready[cohort_of[rid]] += 1
                elif step == "PRE" and spec[0] == "D":
                    local_busy = False
                elif step == "PROC" and spec[0] == "D":
                    cloud_busy[int(spec[2])] = False
                elif step == "POST" and spec[0] == "D":
                    local_busy = False
                    for rid in (int(x) for x in spec[4:-1]):
                        if phase_of[rid] == DONE:
                            continue  # FIN line precedes the final D POST TDN
                        phase_of[rid] = DEC_R
                        dec_r.add(rid)
                        cohort_ready[cohort_of[rid]] += 1
            elif kind == "XDN":
                dirn, pre = ev[1], (ev[4] == "PRE")
                ids = [int(x) for x in ev[6:]]
                if pre and dirn == "UP":
                    for rid in ids:
                        phase_of[rid] = P_RPROC
                        q_pproc.append(rid)
                elif pre and dirn == "DOWN":
                    for rid in ids:
                        phase_of[rid] = P_RPOST
                        q_ppost.append(rid)
                elif (not pre) and dirn == "UP":
                    for rid in ids:
                        phase_of[rid] = DEC_RPROC
                        dec_rproc[cloud_of[rid]].add(rid)
                else:
                    for rid in ids:
                        phase_of[rid] = DEC_RPOST
                        dec_rpost.add(rid)
            elif kind == "FIN":
                rid = int(ev[1])
                phase_of[rid] = DONE
                ci = cohort_of[rid]
                cohort_unfin[ci] -= 1
                n_unfin -= 1
                if rid in dec_r:
                    dec_r.discard(rid)
                    cohort_ready[ci] -= 1

        assigns = []

        # ---- cloud tasks
        live = [rid for rid in q_pproc if phase_of[rid] == P_RPROC]
        q_pproc = live
        for rid in sorted(live, key=lambda r: lin_of[r]):
            k = cloud_of[rid]
            if not cloud_busy[k]:
                cloud_busy[k] = True
                cloud_load[k] += S + dur(2, lin_of[rid])
                assigns.append(f"C{k} P PROC 0 {layers} {k} {rid}")
                q_pproc.remove(rid)
        for k in range(K):
            mem = dec_rproc[k]
            if mem and not cloud_busy[k]:
                cloud_busy[k] = True
                cloud_load[k] += S + dur(5, len(mem))
                lst = sorted(mem)
                mem.clear()
                assigns.append(f"C{k} D PROC {k} {len(lst)} " + " ".join(map(str, lst)))

        # ---- one local task
        if not local_busy:
            if dec_rpost:
                g = sorted(dec_rpost)
                dec_rpost.clear()
                local_busy = True
                assigns.append(f"E D POST -1 {len(g)} " + " ".join(map(str, g)))
            else:
                go = None
                if pol == "greedy":
                    go = sorted(dec_r)
                elif pol == "mega":
                    if n_unfin and len(dec_r) == n_unfin:
                        go = sorted(dec_r)
                elif pol.startswith("cohorts:"):
                    for ci in range(NC):
                        if cohort_unfin[ci] and cohort_ready[ci] == cohort_unfin[ci]:
                            go = sorted(r for r in dec_r if cohort_of[r] == ci)
                            break
                elif pol.startswith("hold:"):
                    if n_unfin and (len(dec_r) >= hold_T or len(dec_r) == n_unfin):
                        go = sorted(dec_r)
                if go:
                    local_busy = True
                    for rid in go:
                        dec_r.discard(rid)
                        cohort_ready[cohort_of[rid]] -= 1
                    assigns.append(f"E D PRE -1 {len(go)} " + " ".join(map(str, go)))
                elif q_ppost:
                    q_ppost = [r for r in q_ppost if phase_of[r] == P_RPOST]
                    if q_ppost:
                        rid = min(q_ppost, key=lambda r: lin_of[r])
                        q_ppost.remove(rid)
                        local_busy = True
                        assigns.append(f"E P POST {cloud_of[rid]} {rid}")
                else:
                    live_new = [r for r in q_new if phase_of[r] == NEW]
                    q_new = live_new
                    if live_new:
                        live_new.sort(key=lambda r: lin_of[r])
                        rid = live_new[0]
                        k = (force_cloud % K) if force_cloud >= 0 else \
                            min(range(K), key=lambda j: cloud_load[j])
                        cloud_of[rid] = k
                        cloud_load[k] += S + dur(2, lin_of[rid])
                        phase_of[rid] = P_WUP
                        q_new = live_new[1:]
                        local_busy = True
                        assigns.append(f"E P PRE {k} {rid}")

        respond(assigns)


if __name__ == "__main__":
    main()
