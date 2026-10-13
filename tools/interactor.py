#!/usr/bin/env python3
"""tools/interactor.py — local judge for ICPC 2026 Problem A.

Replicates the official judge exactly (statement: ICPC 2026 Online
Challenge 1 powered by Huawei, Codeforces contest 2251, problem A —
https://codeforces.com/contest/2251/problem/A):
  - case file: config lines, N + table rows, R, then arrival triples (t, Lin, Lout)
  - drives the solver subprocess frame by frame
  - validates every response (violation -> verdict 0 + reason)
  - simulates: S occupancy [t, t+S+dur], shared UP/DOWN FIFOs (independent,
    both active), transfer formula, chunk/batch legality, stuck detection
  - scores with the exact formula: Score = 1000*(w_tp*C_tp + w_c*C_w)

Usage:
  python3 tools/interactor.py --case cases/zoo/single_k1.txt --solver build/solution
  -> VERDICT ok|violation(<reason>)|stuck|tle|re | score 500.000 | runtime 0.02s
"""

import argparse
import heapq
import math
import re
import subprocess
import sys
import threading
import time
import fcntl
from pathlib import Path


class Case:
    def __init__(self, path):
        toks = []
        with open(path) as f:
            for line in f:
                line = line.split("#")[0].strip()
                if line:
                    toks.extend(line.split())
        i = 0
        self.K = int(toks[i]); i += 1
        self.S = float(toks[i]); i += 1
        self.latency = float(toks[i]); i += 1
        self.bw = float(toks[i]); i += 1
        self.bpt = int(toks[i]); i += 1
        self.layers = int(toks[i]); i += 1
        self.SLO1 = float(toks[i]); i += 1
        self.SLO2 = float(toks[i]); i += 1
        self.tp_UB = float(toks[i]); i += 1
        self.tp_base = float(toks[i]); i += 1
        self.dist_base = float(toks[i]); i += 1
        self.w_tp = float(toks[i]); i += 1
        self.w_c = float(toks[i]); i += 1
        self.N = int(toks[i]); i += 1
        self.rows = []
        for _ in range(self.N):
            self.rows.append([float(toks[i + j]) for j in range(7)])
            i += 7
        self.R = int(toks[i]); i += 1
        self.arrivals = []
        for _ in range(self.R):
            t = float(toks[i]); lin = int(toks[i + 1]); lout = int(toks[i + 2])
            self.arrivals.append((t, lin, lout))
            i += 3
        self.empties = []
        while i < len(toks):
            assert toks[i] == "EMPTY", f"unexpected token {toks[i]}"
            self.empties.append(float(toks[i + 1]))
            i += 2

    def table_dur(self, col, batch):
        """col 0..5 = PRE_PRE..DECODE_POST (table columns 1..6)."""
        pairs = sorted((int(r[0]), r[col + 1]) for r in self.rows if r[col + 1] != -1.0)
        assert pairs
        if batch <= pairs[0][0]:
            return pairs[0][1]
        if batch >= pairs[-1][0]:
            return pairs[-1][1]
        for (b1, v1), (b2, v2) in zip(pairs, pairs[1:]):
            if b1 == batch:
                return v1
            if b1 < batch < b2:
                return v1 + (v2 - v1) * (batch - b1) / (b2 - b1)
        raise AssertionError("table_dur unreachable")

    def transfer(self, length):
        return self.latency + 8.0 * length * self.bpt / (self.bw * 1e6)


# Request lifecycle phases (mirror of solver's 17 phases)
RP_PRE, PPRE_RUN, W_IN_UP, RP_PROC, PPROC_RUN, W_IN_DN, RP_POST, PPOST_RUN, \
RD_PRE, DPRE_RUN, W_DEC_UP, RD_PROC, DPROC_RUN, W_DEC_DN, RD_POST, DPOST_RUN, \
FINISHED = range(17)

READY_PHASES = {RP_PRE, RP_PROC, RP_POST, RD_PRE, RD_PROC, RD_POST}


class Sim:
    def __init__(self, case):
        self.c = case
        self.req = [None] * case.R
        self.local_busy_until = 0.0
        self.cloud_busy_until = [0.0] * case.K
        self.up = []   # FIFO of (finish_time, payload)
        self.down = []
        self.events = []  # min-heap (time, seq, kind, payload)
        self.seq = 0
        self.tok_out = 0
        self.last_finish = 0.0
        self.first_arrival = 1e300
        self.sum_tdr = 0.0
        self.n_ppost = 0
        self.sum_span = 0.0
        self.total_gaps = 0
        self.done = 0
        for i, (t, lin, lout) in enumerate(case.arrivals):
            self.push(t, "ARR", (i, lin, lout))

    def push(self, t, kind, payload):
        heapq.heappush(self.events, (t, self.seq, kind, payload))
        self.seq += 1

    def spawn_arr(self, t, rid, lin, lout):
        self.req[rid] = dict(id=rid, lin=lin, lout=lout, cloud=-1,
                             phase=RP_PRE, next_ls=0, iter=0,
                             first_token=None, last_token=None, finished=False)
        self.first_arrival = min(self.first_arrival, t)

    def enqueue_up(self, enter, length, payload):
        self.up.append((max(enter, self.up[-1][0] if self.up else 0.0) + self.c.transfer(length), payload))

    def enqueue_down(self, enter, length, payload):
        self.down.append((max(enter, self.down[-1][0] if self.down else 0.0) + self.c.transfer(length), payload))

    def tdns(self, t, payload):  # list of (spec_tokens, dur) -> TDN events
        for spec, dur in payload:
            self.push(t, "TDN", (spec, dur))

    def xdns(self, t, payload):  # list of XDN payloads -> events
        for p in payload:
            self.push(t, "XDN", p)

    def finish(self, rid, t):
        r = self.req[rid]
        r["finished"] = True
        r["phase"] = FINISHED
        self.done += 1
        self.sum_span += r["last_token"] - r["first_token"]
        # Statement §Waiting-Time Component: tpot pools consecutive-token gaps
        # (1 <= j < Lout[i]); a request with Lout=1 contributes NO gap.
        self.total_gaps += r["iter"] - 1
        self.tok_out += r["iter"]
        self.last_finish = max(self.last_finish, t)

    # ---- response application (validated) ----
    def apply(self, t, assigns):
        if len(assigns) > self.c.K + 1:
            return "n > K+1"
        for a in assigns:
            err = self.apply_one(t, a)
            if err:
                return err
        return None

    def apply_one(self, t, a):
        c = self.c
        srv, spec = a[0], a[1]
        if srv == "E":
            is_local = True
        elif srv.startswith("C"):
            k = int(srv[1:])
            if not (0 <= k < c.K):
                return "server Ck out of range"
            is_local = False
        else:
            return "bad server"

        step = spec[0] + " " + spec[1]

        if step == "P PRE":
            if len(spec) != 4:
                return "P PRE arity"
            remote, rid = int(spec[2]), int(spec[3])
            if not (0 <= rid < c.R):
                return "rid out of range"
            r = self.req[rid]
            if r is None:
                return "rid not arrived"
            if not (0 <= remote < c.K):
                return "P PRE remote out of range"
            if not is_local:
                return "P PRE server must be E"
            if t < self.local_busy_until - 1e-9:
                return "local busy"
            if r["phase"] != RP_PRE or r["finished"]:
                return "P PRE wrong phase"
            dur = c.table_dur(0, r["lin"])
            self.local_busy_until = t + c.S + dur
            r["cloud"] = remote
            r["phase"] = PPRE_RUN
            self.tdns(t + c.S + dur, [(["P", "PRE", str(remote), str(rid)], dur)])
            self.enqueue_up(t + c.S + dur, r["lin"], ("UP", remote, r["lin"] * c.bpt, True, [rid]))
            return None

        if step == "P PROC":
            if len(spec) != 6:
                return "P PROC arity"
            ls, le, remote, rid = int(spec[2]), int(spec[3]), int(spec[4]), int(spec[5])
            r = self.req[rid] if 0 <= rid < c.R else None
            if r is None:
                return "rid not arrived"
            if not is_local and srv[1:] != str(remote):
                return "P PROC server != remote"
            if r["cloud"] != remote:
                return "P PROC remote != assigned"
            if t < self.cloud_busy_until[remote] - 1e-9:
                return "cloud busy"
            if r["phase"] != RP_PROC:
                return "P PROC wrong phase"
            if ls != r["next_ls"] or le <= ls or le > c.layers:
                return "illegal piece"
            dur = (le - ls) / c.layers * c.table_dur(1, r["lin"])
            self.cloud_busy_until[remote] = t + c.S + dur
            r["next_ls"] = le
            if le == c.layers:
                r["phase"] = W_IN_DN
                self.tdns(t + c.S + dur, [(["P", "PROC", str(ls), str(le), str(remote), str(rid)], dur)])
                self.enqueue_down(t + c.S + dur, r["lin"], ("DOWN", remote, r["lin"] * c.bpt, True, [rid]))
            else:
                r["phase"] = RP_PROC
                self.tdns(t + c.S + dur, [(["P", "PROC", str(ls), str(le), str(remote), str(rid)], dur)])
            return None

        if step == "P POST":
            if len(spec) != 4:
                return "P POST arity"
            remote, rid = int(spec[2]), int(spec[3])
            r = self.req[rid] if 0 <= rid < c.R else None
            if r is None:
                return "rid not arrived"
            if not is_local:
                return "P POST server must be E"
            if r["cloud"] != remote:
                return "P POST remote != assigned"
            if t < self.local_busy_until - 1e-9:
                return "local busy"
            if r["phase"] != RP_POST:
                return "P POST wrong phase"
            dur = c.table_dur(2, r["lin"])
            self.local_busy_until = t + c.S + dur
            r["phase"] = RD_PRE
            self.sum_tdr += (t + c.S + dur) - self._arr_time(r)
            self.n_ppost += 1
            self.tdns(t + c.S + dur, [(["P", "POST", str(remote), str(rid)], dur)])
            return None

        if step in ("D PRE", "D POST"):
            if spec[2] != "-1" or len(spec) < 4:
                return "D %s arity" % spec[1]
            m = int(spec[3])
            ids = [int(x) for x in spec[4:]]
            if len(ids) != m or m < 1 or len(set(ids)) != m:
                return "malformed group"
            if not is_local:
                return "D %s server must be E" % spec[1]
            if t < self.local_busy_until - 1e-9:
                return "local busy"
            for rid in ids:
                r = self.req[rid] if 0 <= rid < c.R else None
                if r is None:
                    return "rid not arrived"
                want = RD_PRE if step == "D PRE" else RD_POST
                if r["phase"] != want or r["finished"]:
                    return "%s wrong phase (rid %d)" % (step, rid)
            if step == "D PRE":
                dur = c.table_dur(3, m)
                self.local_busy_until = t + c.S + dur
                for rid in ids:
                    self.req[rid]["phase"] = DPRE_RUN
                self.tdns(t + c.S + dur, [(["D", "PRE", "-1", str(m)] + [str(x) for x in ids], dur)])
                # one UP per distinct remote, increasing remote index
                groups = {}
                for rid in ids:
                    groups.setdefault(self.req[rid]["cloud"], []).append(rid)
                ups = []
                for remote in sorted(groups):
                    mem = groups[remote]
                    ups.append((remote, len(mem) * c.bpt, False, mem))
                for remote, size, pre, mem in ups:
                    self.enqueue_up(t + c.S + dur, len(mem), ("UP", remote, size, False, mem))
            else:
                dur = c.table_dur(5, m)
                self.local_busy_until = t + c.S + dur
                for rid in ids:
                    r = self.req[rid]
                    r["phase"] = DPOST_RUN
                    r["iter"] += 1
                    if r["first_token"] is None:
                        r["first_token"] = t + c.S + dur
                    r["last_token"] = t + c.S + dur
                    if r["iter"] == r["lout"]:
                        self.finish(rid, t + c.S + dur)
                        self.push(t + c.S + dur, "FIN", (rid,))
                    else:
                        r["phase"] = RD_PRE  # ready for next decode iteration
                self.tdns(t + c.S + dur, [(["D", "POST", "-1", str(m)] + [str(x) for x in ids], dur)])
            return None

        if step == "D PROC":
            if len(spec) < 4:
                return "D PROC arity"
            remote = int(spec[2])
            m = int(spec[3])
            ids = [int(x) for x in spec[4:]]
            if len(ids) != m or m < 1 or len(set(ids)) != m:
                return "malformed group"
            if is_local or srv[1:] != str(remote):
                return "D PROC server != remote"
            if t < self.cloud_busy_until[remote] - 1e-9:
                return "cloud busy"
            for rid in ids:
                r = self.req[rid] if 0 <= rid < c.R else None
                if r is None:
                    return "rid not arrived"
                if r["phase"] != RD_PROC or r["finished"]:
                    return "D PROC wrong phase (rid %d)" % rid
                if r["cloud"] != remote:
                    return "D PROC member wrong cloud"
            dur = c.table_dur(4, m)
            self.cloud_busy_until[remote] = t + c.S + dur
            for rid in ids:
                self.req[rid]["phase"] = W_DEC_DN
            self.tdns(t + c.S + dur, [(["D", "PROC", str(remote), str(m)] + [str(x) for x in ids], dur)])
            self.enqueue_down(t + c.S + dur, m, ("DOWN", remote, m * c.bpt, False, ids))
            return None

        return "unknown step"

    def _arr_time(self, r):
        return self.c.arrivals[r["id"]][0]

    def next_frame_time(self):
        """Earliest pending time across heap and both transfer FIFOs, or None."""
        best = None
        if self.events:
            best = self.events[0][0]
        for fifo in (self.up, self.down):
            if fifo and (best is None or fifo[0][0] < best - 1e-12):
                best = fifo[0][0]
        return best

    def drain_transfers(self, t, lines, trace_lines=None):
        """Transfer completing exactly at t -> XDN frame line + phase update.
        If trace_lines is given, also appends a PART 9 EVENT line for it."""
        while self.up and abs(self.up[0][0] - t) < 1e-9:
            _, p = self.up.pop(0)
            _, remote, size, pre, ids = p
            lines.append(f"XDN UP {remote} {size} {'PRE' if pre else 'DEC'} {len(ids)} {' '.join(map(str, ids))}")
            self_apply_xdn(self, "UP", remote, ids, pre)
            if trace_lines is not None:
                trace_lines.append(f"EVENT t={t:.9f} XDN UP {remote} {size} {'PRE' if pre else 'DEC'} {len(ids)} {' '.join(map(str, ids))}")
        while self.down and abs(self.down[0][0] - t) < 1e-9:
            _, p = self.down.pop(0)
            _, remote, size, pre, ids = p
            lines.append(f"XDN DOWN {remote} {size} {'PRE' if pre else 'DEC'} {len(ids)} {' '.join(map(str, ids))}")
            self_apply_xdn(self, "DOWN", remote, ids, pre)
            if trace_lines is not None:
                trace_lines.append(f"EVENT t={t:.9f} XDN DOWN {remote} {size} {'PRE' if pre else 'DEC'} {len(ids)} {' '.join(map(str, ids))}")

    def pop_frame(self, t):
        """All heap events with time == t; returns [(kind, payload)]."""
        out = []
        while self.events and abs(self.events[0][0] - t) < 1e-9:
            _, _, kind, payload = heapq.heappop(self.events)
            out.append((kind, payload))
        return out

    def stuck(self):
        if self.done == self.c.R:
            return False
        if any(r is not None and not r["finished"] and r["phase"] in
               (PPRE_RUN, PPROC_RUN, PPOST_RUN, DPRE_RUN, DPROC_RUN, DPOST_RUN)
               for r in self.req):
            return False
        if self.up or self.down:
            return False
        # any unfinished request that could still be issued? only if a future
        # arrival exists for an unarrived request... all arrived ones are stuck
        for r in self.req:
            if r is None:
                return False  # future arrivals still possible
            if not r["finished"] and r["phase"] in READY_PHASES:
                return False  # a ready task exists but solver refuses (still legal)
        # DEBUG: print stuck phases to file
        with open("/tmp/stuck_dbg.txt", "a") as f:
            f.write(f"[STUCK DBG] done={self.done} R={self.c.R} up={len(self.up)} down={len(self.down)}\n")
            for r in self.req:
                if r is not None and not r["finished"]:
                    f.write(f"  rid={r['id']} phase={r['phase']} cloud={r['cloud']} iter={r['iter']} lout={r['lout']}\n")
        return True

    def score(self):
        c = self.c
        if self.done != c.R:
            return 0.0
        tp = self.tok_out / (self.last_finish - self.first_arrival) if self.last_finish > self.first_arrival else 0.0
        c_tp = max(0.0, min(1.0, (tp - c.tp_base) / (c.tp_UB - c.tp_base)))
        mean_tdr = self.sum_tdr / c.R
        mean_tpot = self.sum_span / self.total_gaps if self.total_gaps > 0 else 0.0
        e_tdr = max(0.0, (mean_tdr - c.SLO1) / c.SLO1)
        e_tpot = max(0.0, (mean_tpot - c.SLO2) / c.SLO2)
        dist = math.sqrt(e_tdr * e_tdr + e_tpot * e_tpot)
        if c.dist_base > 0.0:
            c_w = max(0.0, 1.0 - dist / c.dist_base)
        else:
            c_w = 1.0 if dist == 0.0 else 0.0
        return 1000.0 * (c.w_tp * c_tp + c.w_c * c_w)


def read_response(proc, timeout_extra=0.0):
    """Read n then n assignment lines. Raises ValueError on protocol errors."""
    line = proc.stdout.readline()
    if not line:
        raise ValueError("solver exited (EOF) mid-protocol")
    n = int(line.split()[0])
    assigns = []
    for _ in range(n):
        line = proc.stdout.readline()
        if not line:
            raise ValueError("solver exited mid-response")
        toks = line.split()
        if len(toks) < 2:
            raise ValueError("bad assignment line")
        assigns.append((toks[0], toks[1:]))
    return assigns


def _marginal_values(c, mean_tdr, mean_tpot, tp):
    """D-070 judge-side: marginal values (0 outside the linear regions incl.
    the dist_base cliff), recoverable points, and the attack direction."""
    e_tdr = max(0.0, (mean_tdr - c.SLO1) / c.SLO1) if c.SLO1 > 0 else 0.0
    e_tpot = max(0.0, (mean_tpot - c.SLO2) / c.SLO2) if c.SLO2 > 0 else 0.0
    dist = math.sqrt(e_tdr * e_tdr + e_tpot * e_tpot)
    v_tp = 0.0
    if c.tp_UB > c.tp_base and c.tp_base < tp < c.tp_UB:
        v_tp = 1000.0 * c.w_tp / (c.tp_UB - c.tp_base)
    v_tdr = v_tpot = 0.0
    if c.w_c > 0.0 and c.dist_base > 0.0 and dist > 0.0 and dist < c.dist_base:
        if c.SLO1 > 0.0 and e_tdr > 0.0:
            v_tdr = 1000.0 * c.w_c * e_tdr / (c.dist_base * dist * c.SLO1)
        if c.SLO2 > 0.0 and e_tpot > 0.0:
            v_tpot = 1000.0 * c.w_c * e_tpot / (c.dist_base * dist * c.SLO2)
    c_tp = 0.0
    if c.tp_UB > c.tp_base:
        c_tp = max(0.0, min(1.0, (tp - c.tp_base) / (c.tp_UB - c.tp_base)))
    rem_tp = 1000.0 * c.w_tp * (1.0 - c_tp)
    if c.dist_base <= 0.0:
        rem_c = 1000.0 * c.w_c * (1.0 if dist > 0.0 else 0.0)
    else:
        rem_c = 1000.0 * c.w_c * min(1.0, dist / c.dist_base)
    dead = rem_tp < 1e-6 and rem_c < 1e-6
    if rem_tp > rem_c + 1e-9:
        attack = "tp"
    elif rem_c > rem_tp + 1e-9:
        attack = "tdr" if e_tdr >= e_tpot else "tpot"
    else:
        attack = "none"
    return v_tp, v_tdr, v_tpot, rem_tp, rem_c, attack, dead


def oracle_final_check(sim, err_lines):
    """PSGO Batch 2 hard rule: the solver's final [ORACLE FINAL] projected
    tdr/tpot must match this judge's exact mean_tdr/mean_tpot to < 1e-6.
    D-068: also report marginal values (solver vs judge) — informational
    only, they never flip the PASS/FAIL gate."""
    line = next((l for l in reversed(err_lines) if "[ORACLE FINAL" in l), None)
    if line is None:
        return "ORACLE CHECK MISSING | solver printed no [ORACLE FINAL] line"
    m = re.search(r"tdr=([-0-9.eE+]+) tpot=([-0-9.eE+]+)", line)
    if not m:
        return "ORACLE CHECK UNPARSABLE | %s" % line
    s_tdr, s_tpot = float(m.group(1)), float(m.group(2))
    j_tdr = sim.sum_tdr / sim.c.R if sim.c.R else 0.0
    j_tpot = sim.sum_span / sim.total_gaps if sim.total_gaps > 0 else 0.0
    d_tdr = abs(s_tdr - j_tdr)
    d_tpot = abs(s_tpot - j_tpot)
    ok = d_tdr <= 1e-6 * max(1.0, abs(j_tdr)) and d_tpot <= 1e-6 * max(1.0, abs(j_tpot))
    out = ("ORACLE CHECK %s | tdr solver=%.9f judge=%.9f |d|=%.3e | "
           "tpot solver=%.9f judge=%.9f |d|=%.3e | pooled_gaps=%d"
           % ("PASS" if ok else "FAIL", s_tdr, j_tdr, d_tdr,
              s_tpot, j_tpot, d_tpot, sim.total_gaps))
    mm = re.search(r"V_tp=([-0-9.eE+]+) V_tdr=([-0-9.eE+]+) V_tpot=([-0-9.eE+]+) "
                   r"rem_tp=([-0-9.eE+]+) rem_c=([-0-9.eE+]+) "
                   r"attack=(\w+) region=([\w,]+) dead=([01])", line)
    if mm is None:
        return out + " | marg UNPARSABLE"
    j_tp = sim.tok_out / (sim.last_finish - sim.first_arrival) if sim.last_finish > sim.first_arrival else 0.0
    sv = tuple(float(mm.group(i)) for i in range(1, 6))
    jv = _marginal_values(sim.c, j_tdr, j_tpot, j_tp)
    dm = [abs(sv[i] - jv[i]) for i in range(5)]
    marg_ok = all(dm[i] <= 1e-4 * max(1.0, abs(jv[i])) for i in range(5))
    return (out + " | marg %s | V_tp solver=%.6g judge=%.6g |d|=%.3e | "
            "V_tdr solver=%.6g judge=%.6g |d|=%.3e | "
            "V_tpot solver=%.6g judge=%.6g |d|=%.3e | "
            "rem_tp solver=%.6g judge=%.6g |d|=%.3e | "
            "rem_c solver=%.6g judge=%.6g |d|=%.3e | "
            "attack solver=%s judge=%s | region=%s | dead solver=%s judge=%s"
            % ("PASS" if marg_ok else "FAIL", sv[0], jv[0], dm[0],
               sv[1], jv[1], dm[1], sv[2], jv[2], dm[2],
               sv[3], jv[3], dm[3], sv[4], jv[4], dm[4],
               mm.group(6), jv[5], mm.group(7), mm.group(8), 1 if jv[6] else 0))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--case", required=True)
    ap.add_argument("--solver", required=True)
    ap.add_argument("--timeout", type=float, default=10.0)
    ap.add_argument("--trace", action="store_true")
    ap.add_argument("--trace-file", default=None,
                    help="write a structured PART 9 trace (CONFIG + DECISION + "
                         "EVENT lines) to FILE; self-contained for score_trace.py "
                         "and visualize_trace.py. The interactor is the trace source "
                         "because only it simulates the transfer FIFOs, but it logs "
                         "every solver decision as a DECISION line.")
    ap.add_argument("--capture", default=None,
                    help="write every frame sent to the solver to this file "
                         "(config+table+frames, no responses; solver must be "
                         "the same binary to replay its own choices)")
    ap.add_argument("--ghost", type=float, default=0.0,
                    help="FLOATING-POINT GHOST stress: shift every arrival "
                         "(and empty) timestamp by this constant EPS before "
                         "simulation. Use 1e-9 for '...000000001' tails and "
                         "(1-1e-9) for '...999999999' tails. Constant shift "
                         "keeps the simulation internally consistent while "
                         "forcing adversarial 9-decimal frame timestamps.")
    ap.add_argument("--pipe-bytes", type=int, default=0,
                    help="PIPE-DEADLOCK stress: set the solver's stdout pipe "
                         "buffer to N bytes via fcntl.F_SETPIPE_SZ (Linux). "
                         "With a small buffer and a large single-frame "
                         "response the solver must stream/flush correctly "
                         "or it blocks. 0 = leave default.")
    ap.add_argument("--measure-rss", action="store_true",
                    help="OOM stress: poll /proc/<pid>/status VmHWM (peak RSS, "
                         "kB) of the solver during the run and append "
                         "'| max_rss_kb <N>' to the final VERDICT line.")
    ap.add_argument("--tee-stdin", default=None,
                    help="write every line sent to the solver to FILE (the exact "
                         "input stream), so solver CPU time can be measured in "
                         "isolation via `time build/submission < FILE`.")
    ap.add_argument("--fin-first", action="store_true",
                    help="ESSAY 4 state-machine stress: within each coalesced "
                         "frame, emit FIN lines BEFORE the TDN that produced "
                         "them. Line order carries no priority per the spec, "
                         "so a solver must handle both orders (FIN-amnesia "
                         "race: scheduling a finished request = violation).")
    ap.add_argument("--oracle", action="store_true",
                    help="PSGO Batch 2 verification: relay the solver's "
                         "[ORACLE] stderr projections every 10000 frames "
                         "alongside this judge's own running means, and check "
                         "the solver's final [ORACLE FINAL] tdr/tpot against "
                         "the judge's mean_tdr/mean_tpot (error must be < 1e-6).")
    args = ap.parse_args()

    case = Case(args.case)
    if args.ghost:
        for i in range(len(case.arrivals)):
            a = case.arrivals[i]
            case.arrivals[i] = (a[0] + args.ghost, a[1], a[2])
        for i in range(len(case.empties)):
            e = case.empties[i]
            case.empties[i] = (e[0] + args.ghost, e[1], e[2])
    sim = Sim(case)
    tf = open(args.trace_file, "w") if args.trace_file else None
    tee_f = open(args.tee_stdin, "w") if args.tee_stdin else None
    if tf:
        def _g(x):
            return "%.9g" % x
        tf.write("CONFIG %d %s %s %s %d %d %s %s %s %s %s %s %s %d\n" % (
            case.K, _g(case.S), _g(case.latency), _g(case.bw), case.bpt,
            case.layers, _g(case.SLO1), _g(case.SLO2), _g(case.tp_UB),
            _g(case.tp_base), _g(case.dist_base), _g(case.w_tp), _g(case.w_c),
            case.N))
        tf.flush()
    t0 = time.time()
    proc = subprocess.Popen(
        [args.solver],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, bufsize=1,
    )
    # PIPE-DEADLOCK stress: shrink the solver's stdout pipe buffer so a single
    # large frame response must stream/flush or the solver blocks. Works on
    # Linux where the kernel honors F_SETPIPE_SZ.
    if args.pipe_bytes and hasattr(fcntl, "F_SETPIPE_SZ"):
        try:
            fcntl.fcntl(proc.stdout.fileno(), fcntl.F_SETPIPE_SZ, args.pipe_bytes)
        except OSError as ex:
            sys.stderr.write(f"[warn] F_SETPIPE_SZ failed: {ex}\n")
    # OOM stress: track the solver's peak RSS (VmHWM, kB) live. Use a list so
    # the background thread and the verdict printer share the same object.
    max_rss = [0]
    if args.measure_rss:
        def poll_rss():
            pid = proc.pid
            while proc.poll() is None:
                try:
                    with open(f"/proc/{pid}/status") as sf:
                        for line in sf:
                            if line.startswith("VmHWM:"):
                                kb = int(line.split()[1])
                                if kb > max_rss[0]:
                                    max_rss[0] = kb
                except OSError:
                    pass
                time.sleep(0.02)
        threading.Thread(target=poll_rss, daemon=True).start()
    err_lines = []
    err_lock = threading.Lock()

    def drain_stderr():
        for line in proc.stderr:
            with err_lock:
                err_lines.append(line.rstrip())

    err_thread = threading.Thread(target=drain_stderr, daemon=True)
    err_thread.start()

    def err_tail():
        with err_lock:
            return " | ".join(err_lines[-6:])

    try:
        # startup + table
        head = [f"{case.K} {case.S} {case.latency} {case.bw} {case.bpt} {case.layers}",
                f"{case.SLO1} {case.SLO2} {case.tp_UB} {case.tp_base} {case.dist_base} {case.w_tp} {case.w_c}",
                str(case.N)]
        for r in case.rows:
            head.append(" ".join("%.9f" % v if isinstance(v, float) else str(v) for v in r))
        proc.stdin.write("\n".join(head) + "\n")
        proc.stdin.flush()
        if tee_f:
            tee_f.write("\n".join(head) + "\n")
            tee_f.flush()
        capture = []
        if args.capture:
            capture.extend(head)

        frames = 0
        empties = list(case.empties)
        # Keep framing while requests remain OR TDN/FIN events are pending:
        # done==R is bookkept at the final D POST's ASSIGNMENT, but the real
        # judge still delivers that task's TDN with FIN beside it (statement
        # §protocol). Without the trailing frame the solver never sees the
        # final token, and any solver-side score projection (PSGO oracle)
        # misses the last consecutive-token gap.
        while sim.done < case.R or sim.events:
            trace_lines = []
            t = sim.next_frame_time()
            empty_t = empties[0] if empties else None
            if empty_t is not None and (t is None or empty_t < t - 1e-12):
                # e=0 frame (idle; no events at this timestamp)
                t = empty_t
                empties.pop(0)
                lines = ["%.9f" % t, "0"]
            else:
                if empty_t is not None and abs(empty_t - t) < 1e-9:
                    empties.pop(0)  # subsumed by the real frame (coalescing)
                if t is None:
                    # DEBUG: also check State's ready counts via solver's perspective?
                    # For now, just print Sim's ready counts
                    cnt_rp_pre = sum(1 for r in sim.req if r is not None and r["phase"] == RP_PRE)
                    cnt_rp_proc = sum(1 for r in sim.req if r is not None and r["phase"] == RP_PROC)
                    cnt_rp_post = sum(1 for r in sim.req if r is not None and r["phase"] == RP_POST)
                    cnt_rd_pre = sum(1 for r in sim.req if r is not None and r["phase"] == RD_PRE)
                    cnt_rd_proc = sum(1 for r in sim.req if r is not None and r["phase"] == RD_PROC)
                    cnt_rd_post = sum(1 for r in sim.req if r is not None and r["phase"] == RD_POST)
                    with open("/tmp/stuck_dbg2.txt", "a") as f:
                        f.write(f"[STUCK2] t is None done={sim.done} R={sim.c.R} up={len(sim.up)} down={len(sim.down)} events={len(sim.events)} ready_counts RP_PRE={cnt_rp_pre} RP_PROC={cnt_rp_proc} RP_POST={cnt_rp_post} RD_PRE={cnt_rd_pre} RD_PROC={cnt_rd_proc} RD_POST={cnt_rd_post}\n")
                        for r in sim.req:
                            if r is not None and not r["finished"]:
                                f.write(f"  rid={r['id']} phase={r['phase']} cloud={r['cloud']} iter={r['iter']} lout={r['lout']}\n")
                    if sim.stuck():
                        raise ValueError("stuck")
                    raise ValueError("stuck")
                lines = ["%.9f" % t]
                sim.drain_transfers(t, lines, trace_lines)
                for kind, payload in sim.pop_frame(t):
                    if kind == "ARR":
                        rid, lin, _ = payload
                        lines.append(f"ARR {rid} {lin}")
                        sim.spawn_arr(t, rid, lin, payload[2])
                        if tf:
                            trace_lines.append(f"EVENT t={t:.9f} ARR {rid} {lin} {payload[2]}")
                    elif kind == "TDN":
                        spec, dur = payload
                        srv = "E" if spec[0] == "P" and spec[1] in ("PRE", "POST") else \
                               "E" if spec[0] == "D" and spec[1] in ("PRE", "POST") else \
                               f"C{spec[2] if spec[0] == 'D' else spec[4]}"
                        lines.append(f"TDN {srv} {' '.join(spec)} {dur:.9f}")
                        self_apply_tdn(sim, spec)
                        if tf:
                            trace_lines.append(f"EVENT t={t:.9f} TDN {srv} {' '.join(spec)} dur={dur:.9f}")
                    elif kind == "XDN":
                        dir_, remote, size, pre, ids = payload
                        tag = "PRE" if pre else "DEC"
                        lines.append(f"XDN {dir_} {remote} {size} {tag} {len(ids)} {' '.join(map(str, ids))}")
                        self_apply_xdn(sim, dir_, remote, ids, pre)
                        if tf:
                            trace_lines.append(f"EVENT t={t:.9f} XDN {dir_} {remote} {size} {tag} {len(ids)} {' '.join(map(str, ids))}")
                    elif kind == "FIN":
                        rid = payload[0]
                        lines.append(f"FIN {rid}")
                        assert sim.req[rid]["finished"], "FIN for unfinished request"
                        if tf:
                            trace_lines.append(f"EVENT t={t:.9f} FIN {rid}")
                lines.insert(1, str(len(lines) - 1))
                if args.fin_first:
                    # ESSAY 4: FIN-before-TDN line order (legal per spec).
                    head = lines[:2]
                    evs = lines[2:]
                    fins = [l for l in evs if l.startswith("FIN ")]
                    rest = [l for l in evs if not l.startswith("FIN ")]
                    lines = head + fins + rest

            proc.stdin.write("\n".join(lines) + "\n")
            proc.stdin.flush()
            if tee_f:
                tee_f.write("\n".join(lines) + "\n")
                tee_f.flush()
            if args.trace:
                sys.stderr.write(">>> frame t=%s\n%s\n" % (t, "\n".join(lines)))
            if args.capture:
                capture.extend(lines)

            assigns = read_response(proc)
            if args.trace:
                sys.stderr.write("<<< %s\n" % assigns)
            if tf:
                for srv, spec in assigns:
                    tf.write("DECISION t=%.9f ASSIGN %s %s\n" % (t, srv, " ".join(spec)))
                for tl in trace_lines:
                    tf.write(tl + "\n")
                tf.flush()
            err = sim.apply(t, assigns)
            if err:
                print(f"VERDICT violation({err}) | score 0.000 | t={t:.3f}")
                return
            frames += 1
            if args.oracle and frames % 10000 == 0:
                with err_lock:
                    relay = next((l for l in reversed(err_lines)
                                  if l.startswith("[ORACLE") and "FINAL" not in l), "")
                run_tdr = sim.sum_tdr / max(1, sim.n_ppost)
                run_tpot = sim.sum_span / sim.total_gaps if sim.total_gaps > 0 else 0.0
                sys.stderr.write(
                    "[oracle-relay frame=%d judge_tdr=%.9f judge_tpot=%.9f] solver: %s\n"
                    % (frames, run_tdr, run_tpot, relay))

        # Drain any remaining events/transfers so the trace is complete. The
        # main loop exits the instant done==R, leaving the final FIN (and any
        # trailing TDN/XDN) unprocessed. These do not affect scoring (already
        # computed from finish() directly) but the trace needs them for the
        # scorer/visualizer. Already-processed events were popped from the heap,
        # so this only logs the not-yet-seen ones (no duplicates).
        if tf:
            while sim.events or sim.up or sim.down:
                t = sim.next_frame_time()
                if t is None:
                    break
                pl_trace = []
                sim.drain_transfers(t, [], pl_trace)
                for tl in pl_trace:
                    tf.write(tl + "\n")
                for kind, payload in sim.pop_frame(t):
                    if kind == "TDN":
                        spec, dur = payload
                        srv = "E" if spec[0] == "P" and spec[1] in ("PRE", "POST") else \
                               "E" if spec[0] == "D" and spec[1] in ("PRE", "POST") else \
                               f"C{spec[2] if spec[0] == 'D' else spec[4]}"
                        tf.write("EVENT t=%.9f TDN %s %s dur=%.9f\n" % (t, srv, " ".join(spec), dur))
                    elif kind == "XDN":
                        dir_, remote, size, pre, ids = payload
                        tag = "PRE" if pre else "DEC"
                        tf.write("EVENT t=%.9f XDN %s %s %s %d %s\n" % (
                            t, dir_, remote, size, tag, len(ids), " ".join(map(str, ids))))
                    elif kind == "FIN":
                        tf.write("EVENT t=%.9f FIN %d\n" % (t, payload[0]))
                tf.flush()

        # all done: send END
        proc.stdin.write("END\n")
        proc.stdin.flush()
        if tee_f:
            tee_f.write("END\n")
            tee_f.flush()
            tee_f.close()
        proc.wait(timeout=args.timeout)
        if args.oracle:
            # stderr EOF is guaranteed now that the solver has exited; join the
            # drain thread so [ORACLE FINAL] is fully collected before parsing.
            err_thread.join(timeout=5.0)
            print(oracle_final_check(sim, err_lines))
        if args.capture:
            capture.append("END")
            Path(args.capture).write_text("\n".join(capture) + "\n")
        score = sim.score()
        if tf:
            tf.write("SCORE %.3f\n" % score)
        wall = time.time() - t0
        rss = f" | max_rss_kb {max_rss[0]}" if args.measure_rss else ""
        print(f"VERDICT ok | score {score:.3f} | frames {frames} | runtime {wall:.2f}s{rss}")
    except ValueError as ve:
        rss = f" | max_rss_kb {max_rss[0]}" if args.measure_rss else ""
        print(f"VERDICT {ve} | score 0.000 | solver_stderr: {err_tail()}{rss}")
    except subprocess.TimeoutExpired:
        proc.kill()
        rss = f" | max_rss_kb {max_rss[0]}" if args.measure_rss else ""
        print(f"VERDICT tle | score 0.000 | solver_stderr: {err_tail()}{rss}")
    except Exception as ex:  # noqa: BLE001
        proc.kill()
        rss = f" | max_rss_kb {max_rss[0]}" if args.measure_rss else ""
        print(f"VERDICT re({type(ex).__name__}: {ex}) | score 0.000 | solver_stderr: {err_tail()}{rss}")
    finally:
        if tf:
            tf.close()


def self_apply_tdn(sim, spec):
    """Advance request phases on a TDN (mirrors solver; keeps sim consistent
    when XDNs/TDNs arrive before the solver's next response)."""
    step = spec[0] + " " + spec[1]
    if step == "P PRE":
        sim.req[int(spec[3])]["phase"] = W_IN_UP
    elif step == "P PROC":
        r = sim.req[int(spec[5])]
        r["next_ls"] = int(spec[3])
        r["phase"] = W_IN_DN if int(spec[3]) == sim.c.layers else RP_PROC
    elif step == "P POST":
        sim.req[int(spec[3])]["phase"] = RD_PRE
    elif step == "D PRE":
        for rid in map(int, spec[4:]):
            sim.req[rid]["phase"] = W_DEC_UP
    elif step == "D PROC":
        for rid in map(int, spec[4:]):
            sim.req[rid]["phase"] = W_DEC_DN
    elif step == "D POST":
        pass  # token bookkeeping already applied at assignment time


def self_apply_xdn(sim, direction, remote, ids, pre):
    for rid in ids:
        r = sim.req[rid]
        if r["finished"]:
            continue
        if pre:
            r["phase"] = RP_PROC if direction == "UP" else RP_POST
        else:
            r["phase"] = RD_PROC if direction == "UP" else RD_POST


if __name__ == "__main__":
    main()