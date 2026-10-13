#!/usr/bin/env python3
"""SB10 correctness harness — written fresh from problem.md (statement only).

SCOPE AND INDEPENDENCE
  * This file shares ZERO code with tools/interactor.py or any repository
    judging tooling. Every rule below was implemented directly from the
    public statement (problem.md, lines cited inline).
  * It exists to prove PROTOCOL SAFETY ONLY: no protocol violations, no stuck
    states, no deadlocks, no CPU overrun, deterministic transcripts.
  * Any score it prints at the end of a stream is DEBUG INFORMATION ONLY and
    is NEVER evidence for a scoring decision.

Modes
  replay        Feed the statement's Example 1 interactor column to the solver
                and diff every response against the statement's response column.
  loop -n N     N randomized closed-loop streams: random legal configs, random
                table shapes (rising/falling/flat/V/step/concave/random, -1
                gaps, unordered rows), random arrivals (burst/dribble/poisson/
                lull/tie-heavy/mixed), hidden Lout draws. Asserts: zero protocol
                violations, zero stuck states, 100% completion, exit code 0.
  det -n N      Determinism: re-run streams twice, require identical transcripts.
  stress        Max-density LEGAL stream (R=2000, sum(Lout)<=2e5 per
                problem.md:270, chunked prefills + singleton decode => maximum
                realistic frame count). Measures CHILD CPU precisely via
                getrusage(RUSAGE_CHILDREN) and documents linear extrapolation
                toward the statement's 2e6-frame cap (a fully legal 2e6-frame
                test is not constructible under the sum(Lout) cap; see doc).

Usage
  python3 research/techniques/SB10_fuzz.py replay  --solver build/submission
  python3 research/techniques/SB10_fuzz.py loop -n 10000 --solver build/submission
  python3 research/techniques/SB10_fuzz.py det  -n 200   --solver build/submission
  python3 research/techniques/SB10_fuzz.py stress --solver build/submission
"""

import argparse
import math
import os
import random
import resource
import select
import subprocess
import sys
import time

EPS = 1e-9


class Violation(Exception):
    pass


# ---------------------------------------------------------------------------
# Token reader over the solver's stdout (responses allow arbitrary whitespace
# between tokens — problem.md:300).
#
# Implementation note: reads the RAW fd via os.read under select(). Wrapping
# the pipe in a buffered readline() + select() is UNSOUND here — one readline
# slurps the whole pipe write into userspace and every later select() then
# starves while tokens sit in the Python buffer.
# ---------------------------------------------------------------------------
class TokenReader:
    def __init__(self, pipe, deadline):
        self.fd = pipe.fileno()
        self.deadline = deadline
        self.buf = b""
        self.pos = 0

    def _refill(self):
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise Violation("deadline exceeded waiting for solver response")
        r, _, _ = select.select([self.fd], [], [], remaining)
        if not r:
            raise Violation("timeout: solver produced no response "
                            "(possible deadlock / stuck)")
        chunk = os.read(self.fd, 65536)
        if chunk == b"":
            raise Violation("EOF on solver stdout before END (solver exited?)")
        self.buf = self.buf[self.pos:] + chunk
        self.pos = 0

    def token(self):
        while True:
            i = self.pos
            n = len(self.buf)
            while i < n and self.buf[i:i + 1].isspace():
                i += 1
            if i >= n:
                self.pos = n
                self._refill()
                continue
            j = i
            while j < n and not self.buf[j:j + 1].isspace():
                j += 1
            tok = self.buf[i:j]
            self.pos = j
            return tok.decode()

    def int_tok(self):
        tok = self.token()
        try:
            return int(tok)
        except ValueError:
            raise Violation(f"non-integer token in response: {tok!r}")


def read_response(rd, deadline=None):
    """Parse one solver response into a flat normalized token list.
    Shapes (problem.md:309-319):
      E P PRE <cl> <rid>          | E P POST <cl> <rid>
      C<k> P PROC <ls> <le> <cl> <rid>
      E D PRE -1 <m> <ids..>      | E D POST -1 <m> <ids..>
      C<k> D PROC <cl> <m> <ids..>
    """
    n = rd.int_tok()
    if n < 0 or n > 9:  # n <= K+1 <= 9 checked by caller too
        raise Violation(f"assignment count out of range: {n}")
    toks = [str(n)]
    for _ in range(n):
        srv = rd.token()
        if srv != "E" and not (srv.startswith("C") and srv[1:].isdigit()):
            raise Violation(f"bad server token {srv!r}")
        w1 = rd.token()
        if w1 not in ("P", "D"):
            raise Violation(f"bad task-class token {w1!r}")
        w2 = rd.token()
        toks += [srv, w1, w2]
        if w1 == "P":
            if w2 == "PROC":
                toks += [rd.token() for _ in range(4)]
            elif w2 in ("PRE", "POST"):
                toks += [rd.token(), rd.token()]
            else:
                raise Violation(f"bad P step {w2!r}")
        else:
            if w2 in ("PRE", "POST"):
                neg = rd.token()
                if neg != "-1":
                    raise Violation(f"D {w2} marker must be -1, got {neg!r}")
                m = rd.int_tok()
                if m < 1:
                    raise Violation("malformed group m<1")
                toks += [neg, str(m)] + [rd.token() for _ in range(m)]
            elif w2 == "PROC":
                toks += [rd.token(), rd.int_tok().__str__()]
                m = int(toks[-1])
                if m < 1:
                    raise Violation("malformed group m<1")
                toks += [rd.token() for _ in range(m)]
            else:
                raise Violation(f"bad D step {w2!r}")
    return toks


# ---------------------------------------------------------------------------
# Statement-derived judge model.
# ---------------------------------------------------------------------------
P_PRE, P_PROC, P_POST, D_PRE, D_PROC, D_POST = range(6)

# Request phases (dependency summary, problem.md:323-334).
PH_ARR = 0          # arrived, READY_P_PRE
PH_PPRE_RUN = 1     # P PRE running
PH_UP = 2           # input UP in flight (WAIT_INPUT_UP)
PH_PPROC_READY = 3  # READY_P_PROC (first piece or between pieces)
PH_PPROC_RUN = 4    # P PROC piece running
PH_DOWN_IN = 5      # input DOWN in flight (WAIT_INPUT_DOWN)
PH_PPOST_READY = 6  # READY_P_POST
PH_PPOST_RUN = 7    # P POST running
PH_DPRE_READY = 8   # READY_D_PRE
PH_DPRE_RUN = 9     # D PRE running
PH_DECUP = 10       # decode UP in flight
PH_DPROC_READY = 11 # READY_D_PROC
PH_DPROC_RUN = 12   # D PROC running
PH_DECDOWN = 13     # decode DOWN in flight
PH_DPOST_READY = 14 # READY_D_POST
PH_DPOST_RUN = 15   # D POST running
PH_FIN = 16


class Req:
    __slots__ = ("id", "lin", "arr", "lout", "cloud", "ph", "iter", "next_ls",
                 "ppost_t", "first_tok", "last_tok", "finished", "in_flight")

    def __init__(self, rid, lin, arr, lout):
        self.id = rid
        self.lin = lin
        self.arr = arr
        self.lout = lout          # judge-hidden until FIN (problem.md:103)
        self.cloud = -1
        self.ph = PH_ARR
        self.iter = 0
        self.next_ls = 0
        self.ppost_t = None
        self.first_tok = None
        self.last_tok = None
        self.finished = False
        self.in_flight = False


class Task:
    __slots__ = ("server", "step", "finish", "dur", "ids", "spec_line",
                 "cloud", "ls", "le", "seq")

    def __init__(self, server, step, dur, ids, spec_line, cloud, ls, le):
        self.server = server      # -1 local, else cloud index
        self.step = step
        self.finish = None
        self.dur = dur
        self.ids = ids
        self.spec_line = spec_line
        self.cloud = cloud
        self.ls = ls
        self.le = le
        self.seq = 0


class Xfer:
    __slots__ = ("up", "remote", "size", "dec", "ids", "finish", "seq")

    def __init__(self, up, remote, size_bytes, dec, ids, finish):
        self.up = up
        self.remote = remote
        self.size = size_bytes
        self.dec = dec
        self.ids = list(ids)
        self.finish = finish
        self.seq = 0


def interp(col, b):
    """Table lookup (problem.md:233): exact hit else linear between sorted
    neighbours; clamp below/above; result strictly positive guaranteed."""
    if not col:
        return 1.0
    if b <= col[0][0]:
        return col[0][1]
    if b >= col[-1][0]:
        return col[-1][1]
    lo, hi = 0, len(col) - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if col[mid][0] <= b:
            lo = mid
        else:
            hi = mid
    b1, v1 = col[lo]
    b2, v2 = col[hi]
    if b1 == b:
        return v1
    return v1 + (v2 - v1) * (b - b1) / (b2 - b1)


class Judge:
    """Interactive judge side built clause-by-clause from problem.md."""

    def __init__(self, cfg, table_rows, arrivals, rng, lout_fn=None):
        self.cfg = cfg
        self.K = cfg["K"]
        self.S = cfg["S"]
        self.lat = cfg["lat"]
        self.bw = cfg["bw"]
        self.bpt = cfg["bpt"]
        self.layers = cfg["layers"]
        self.cols = [[] for _ in range(6)]
        for r in table_rows:
            for c in range(6):
                if r[c + 1] != -1.0:
                    self.cols[c].append((int(r[0]), float(r[c + 1])))
        for c in range(6):
            self.cols[c].sort()
        self.arrivals = sorted(arrivals, key=lambda a: a[0])
        self.lout_fn = lout_fn or (lambda: rng.randint(1, 512))
        self.rng = rng
        self.reqs = {}
        self.n_arrived = 0
        self.local_until = None
        self.local_task = None
        self.cloud_until = [None] * self.K
        self.cloud_task = [None] * self.K
        self.up_q = []
        self.dn_q = []
        self.seqc = 0
        self.now = 0.0

    # -- cost model ----------------------------------------------------------
    def dur(self, step, b):
        return interp(self.cols[step], b)

    def xfer_time(self, ln):
        # latency_ms + 8*data_bytes/(bw*1e6); bytes = len*bpt (problem.md:208)
        return self.lat + 8.0 * ln * self.bpt / (self.bw * 1e6)

    def enqueue(self, up, enter, ln, dec, remote, ids, seq_key):
        q = self.up_q if up else self.dn_q
        tail = q[-1].finish if q else None
        start = enter if tail is None else max(enter, tail)
        x = Xfer(up, remote, ln * self.bpt, dec, ids,
                 start + self.xfer_time(ln))
        x.seq = seq_key
        q.append(x)

    def all_done(self):
        return (self.n_arrived == len(self.arrivals)
                and all(r.finished for r in self.reqs.values()))

    # -- next event frame -----------------------------------------------------
    def pending_events(self):
        """Earliest internal event time; exactly-equal timestamps coalesce
        (problem.md:242). Returns (tau, lines) or None."""
        cand = []
        if self.n_arrived < len(self.arrivals):
            cand.append(("ARR", self.arrivals[self.n_arrived][0]))
        if self.local_task is not None:
            cand.append(("TDN", self.local_task.finish))
        for i in range(self.K):
            if self.cloud_task[i] is not None:
                cand.append(("TDN", self.cloud_task[i].finish))
        for q in (self.up_q, self.dn_q):
            if q:
                cand.append(("XDN", q[0].finish))
        if not cand:
            return None
        tau = min(t for _, t in cand)

        lines = []
        # ARRs first (all sharing tau)
        while (self.n_arrived < len(self.arrivals)
               and abs(self.arrivals[self.n_arrived][0] - tau) < EPS):
            _, lin = self.arrivals[self.n_arrived]
            rid = self.n_arrived
            self.n_arrived += 1
            self.reqs[rid] = Req(rid, lin, tau, self.lout_fn())
            lines.append(f"ARR {rid} {lin}")

        # TDNs ordered by assignment-line seq (problem.md:206)
        tdns = []
        if self.local_task is not None and abs(self.local_task.finish - tau) < EPS:
            tdns.append(self.local_task)
        for i in range(self.K):
            ct = self.cloud_task[i]
            if ct is not None and abs(ct.finish - tau) < EPS:
                tdns.append(ct)
        tdns.sort(key=lambda x: x.seq)
        for tsk in tdns:
            self._complete_task(tsk, tau, lines)

        # XDNs in FIFO order (entry order fixed at enqueue)
        while self.up_q and abs(self.up_q[0].finish - tau) < EPS:
            self._deliver(self.up_q.pop(0), lines)
        while self.dn_q and abs(self.dn_q[0].finish - tau) < EPS:
            self._deliver(self.dn_q.pop(0), lines)
        return tau, lines

    def _complete_task(self, tsk, tau, lines):
        if tsk.server == -1:
            self.local_task = None
            self.local_until = None
        else:
            self.cloud_task[tsk.server] = None
            self.cloud_until[tsk.server] = None
        r0 = self.reqs[tsk.ids[0]]
        srv = "E" if tsk.server == -1 else f"C{tsk.server}"
        lines.append(f"TDN {srv} {tsk.spec_line} {tsk.dur:.9f}")
        if tsk.step == P_PRE:
            # transfer queued immediately on completion, busy or not (:133)
            r0.ph = PH_UP
            self.enqueue(True, tau, r0.lin, False, r0.cloud, [r0.id], tsk.seq)
        elif tsk.step == P_PROC:
            r0.next_ls = tsk.le
            r0.in_flight = False
            if tsk.le == self.layers:
                r0.ph = PH_DOWN_IN
                self.enqueue(False, tau, r0.lin, False, r0.cloud, [r0.id],
                             tsk.seq)
            else:
                r0.ph = PH_PPROC_READY
        elif tsk.step == P_POST:
            r0.ph = PH_DPRE_READY
            r0.in_flight = False
            r0.ppost_t = tau
        elif tsk.step == D_PRE:
            per = {}
            for rid in tsk.ids:
                rr = self.reqs[rid]
                rr.ph = PH_DECUP
                per.setdefault(rr.cloud, []).append(rid)
            # one UP per distinct remote, increasing remote index (:164/:331)
            for c in sorted(per):
                self.enqueue(True, tau, len(per[c]), True, c, per[c], tsk.seq)
        elif tsk.step == D_PROC:
            for rid in tsk.ids:
                self.reqs[rid].ph = PH_DECDOWN
            self.enqueue(False, tau, len(tsk.ids), True, tsk.cloud, tsk.ids,
                         tsk.seq)
        elif tsk.step == D_POST:
            fins = []
            for rid in tsk.ids:
                rr = self.reqs[rid]
                rr.iter += 1
                if rr.first_tok is None:
                    rr.first_tok = tau
                rr.last_tok = tau
                rr.in_flight = False
                if rr.iter >= rr.lout:
                    rr.finished = True
                    rr.ph = PH_FIN
                    fins.append(rid)
                else:
                    rr.ph = PH_DPRE_READY
            for rid in fins:  # FIN beside final D POST TDN (:242/:333)
                lines.append(f"FIN {rid}")

    def _deliver(self, x, lines):
        d = "UP" if x.up else "DOWN"
        tag = "DEC" if x.dec else "PRE"
        lines.append(f"XDN {d} {x.remote} {x.size} {tag} {len(x.ids)}"
                     + "".join(f" {i}" for i in x.ids))
        for rid in x.ids:
            rr = self.reqs[rid]
            if not x.dec:
                rr.ph = PH_PPROC_READY if x.up else PH_PPOST_READY
            else:
                rr.ph = PH_DPROC_READY if x.up else PH_DPOST_READY
            rr.in_flight = False

    # -- response validation + application ------------------------------------
    def apply_response(self, toks, t):
        if not toks:
            raise Violation("empty response")
        try:
            n = int(toks[0])
        except ValueError:
            raise Violation(f"response count unparsable: {toks[0]!r}")
        if n < 0 or n > self.K + 1:
            raise Violation(f"assignment count out of range: {n}")
        body = toks[1:]
        pos = 0

        def need(k):
            nonlocal pos
            if pos + k > len(body):
                raise Violation("response truncated mid-assignment")
            out = body[pos:pos + k]
            pos += k
            return out

        local_taken = False
        cloud_taken = [False] * self.K
        scheduled = []

        def local_free():
            return self.local_until is None or self.local_until <= t + EPS

        def cloud_free(c):
            return self.cloud_until[c] is None or self.cloud_until[c] <= t + EPS

        for _ in range(n):
            srv = need(1)[0]
            w1 = need(1)[0]
            w2 = need(1)[0]
            if w1 == "P" and w2 == "PRE":
                a = need(2)
                cl, rid = int(a[0]), int(a[1])
                if srv != "E":
                    raise Violation("P PRE server must be E")
                if local_taken or not local_free():
                    raise Violation("P PRE on busy/double-booked local")
                if not (0 <= cl < self.K):
                    raise Violation(f"P PRE cloud {cl} outside [0,{self.K})")
                r = self._req_ok(rid, PH_ARR, "P PRE")
                d = self.dur(P_PRE, r.lin)
                spec = f"P PRE {cl} {rid}"
                tsk = Task(-1, P_PRE, d, [rid], spec, cl, 0, 0)
                tsk.finish = t + self.S + d
                r.ph = PH_PPRE_RUN
                r.cloud = cl
                r.in_flight = True
                local_taken = True
                scheduled.append(tsk)
            elif w1 == "P" and w2 == "PROC":
                a = need(4)
                ls, le, cl, rid = int(a[0]), int(a[1]), int(a[2]), int(a[3])
                r = self._req_ok(rid, PH_PPROC_READY, "P PROC")
                if r.cloud != cl:
                    raise Violation("P PROC remote != assigned cloud")
                if srv != f"C{cl}":
                    raise Violation("P PROC server token mismatch")
                if not cloud_free(cl):
                    raise Violation("P PROC on busy cloud")
                if not (0 <= ls < le <= self.layers):   # (:138-152, :376-378)
                    raise Violation(f"illegal piece [{ls},{le}) layers={self.layers}")
                if ls != r.next_ls:
                    raise Violation(f"piece gap/order: ls={ls} next_ls={r.next_ls}")
                d = (le - ls) / self.layers * self.dur(P_PROC, r.lin)  # (:149)
                spec = f"P PROC {ls} {le} {cl} {rid}"
                tsk = Task(cl, P_PROC, d, [rid], spec, cl, ls, le)
                tsk.finish = t + self.S + d
                r.ph = PH_PPROC_RUN
                r.in_flight = True
                cloud_taken[cl] = True
                scheduled.append(tsk)
            elif w1 == "P" and w2 == "POST":
                a = need(2)
                cl, rid = int(a[0]), int(a[1])
                r = self._req_ok(rid, PH_PPOST_READY, "P POST")
                if r.cloud != cl:
                    raise Violation("P POST remote != assigned cloud")
                if srv != "E":
                    raise Violation("P POST server must be E")
                if local_taken or not local_free():
                    raise Violation("P POST on busy/double-booked local")
                d = self.dur(P_POST, r.lin)
                spec = f"P POST {cl} {rid}"
                tsk = Task(-1, P_POST, d, [rid], spec, cl, 0, 0)
                tsk.finish = t + self.S + d
                r.ph = PH_PPOST_RUN
                r.in_flight = True
                local_taken = True
                scheduled.append(tsk)
            elif w1 == "D" and w2 in ("PRE", "POST"):
                a = need(2)
                neg, m = a[0], int(a[1])
                if neg != "-1":
                    raise Violation("D PRE/POST marker must be -1")
                if srv != "E":
                    raise Violation("D PRE/POST server must be E")
                ids = [int(v) for v in need(m)]
                if m < 1 or len(set(ids)) != len(ids):
                    raise Violation("malformed group (m<1 or duplicate ids)")
                want = PH_DPRE_READY if w2 == "PRE" else PH_DPOST_READY
                for rid in ids:
                    self._req_ok(rid, want, f"D {w2}")
                if local_taken or not local_free():
                    raise Violation(f"D {w2} on busy/double-booked local")
                d = self.dur(D_PRE if w2 == "PRE" else D_POST, m)
                spec = f"D {w2} -1 {m}" + "".join(f" {i}" for i in ids)
                tsk = Task(-1, D_PRE if w2 == "PRE" else D_POST, d, ids, spec,
                           -1, 0, 0)
                tsk.finish = t + self.S + d
                run_ph = PH_DPRE_RUN if w2 == "PRE" else PH_DPOST_RUN
                for rid in ids:
                    self.reqs[rid].ph = run_ph
                    self.reqs[rid].in_flight = True
                local_taken = True
                scheduled.append(tsk)
            elif w1 == "D" and w2 == "PROC":
                a = need(2)
                cl, m = int(a[0]), int(a[1])
                if srv != f"C{cl}":
                    raise Violation("D PROC server token mismatch")
                ids = [int(v) for v in need(m)]
                if m < 1 or len(set(ids)) != len(ids):
                    raise Violation("malformed group (m<1 or duplicate ids)")
                for rid in ids:
                    r = self._req_ok(rid, PH_DPROC_READY, "D PROC")
                    if r.cloud != cl:
                        raise Violation("D PROC member on wrong cloud")
                if cloud_taken[cl] or not cloud_free(cl):
                    raise Violation("D PROC on busy/double-booked cloud")
                d = self.dur(D_PROC, m)
                spec = f"D PROC {cl} {m}" + "".join(f" {i}" for i in ids)
                tsk = Task(cl, D_PROC, d, ids, spec, cl, 0, 0)
                tsk.finish = t + self.S + d
                for rid in ids:
                    self.reqs[rid].ph = PH_DPROC_RUN
                    self.reqs[rid].in_flight = True
                cloud_taken[cl] = True
                scheduled.append(tsk)
            else:
                raise Violation(f"unknown assignment shape: {w1} {w2}")
        if pos != len(body):
            raise Violation("extra tokens after last assignment")
        for tsk in scheduled:  # seq = assignment-line order (problem.md:206)
            tsk.seq = self.seqc
            self.seqc += 1
            if tsk.server == -1:
                self.local_until = tsk.finish
                self.local_task = tsk
            else:
                self.cloud_until[tsk.server] = tsk.finish
                self.cloud_task[tsk.server] = tsk

    def _req_ok(self, rid, want_ph, what):
        if rid not in self.reqs:
            raise Violation(f"{what}: rid {rid} has not arrived")
        r = self.reqs[rid]
        if r.finished:
            raise Violation(f"{what}: rid {rid} already FINished")
        if r.in_flight:
            raise Violation(f"{what}: rid {rid} already part of an in-flight task")
        if r.ph != want_ph:
            raise Violation(f"{what}: rid {rid} wrong phase "
                            f"(have {r.ph}, need {want_ph})")
        return r

    # -- stuck detection (problem.md:390-394) ----------------------------------
    def check_stuck(self):
        if self.all_done():
            return
        if self.n_arrived < len(self.arrivals):
            return  # a future ARR can create an event (:392)
        if self.local_task is not None or any(self.cloud_task):
            return
        if self.up_q or self.dn_q:
            return
        bad = sorted(r.id for r in self.reqs.values() if not r.finished)
        raise Violation(f"STUCK state: unfinished {bad[:8]} with no pending cause")

    # -- informational score (DEBUG ONLY — never cited as evidence) ------------
    def debug_score(self):
        c = self.cfg
        if not self.all_done():
            return None
        last = max(r.last_tok for r in self.reqs.values())
        first = min(r.arr for r in self.reqs.values())
        E = last - first
        N = sum(r.iter for r in self.reqs.values())
        tp = N / E if E > 0 else 0.0
        denom = c["tp_ub"] - c["tp_base"]
        ctp = max(0.0, min(1.0, (tp - c["tp_base"]) / denom)) if denom > 0 else 0.0
        R = len(self.reqs)
        tdr = sum(r.ppost_t - r.arr for r in self.reqs.values()) / R
        spans = sum((r.last_tok - r.first_tok) for r in self.reqs.values()
                    if r.lout >= 2)
        gaps = sum(r.lout - 1 for r in self.reqs.values())
        tpot = spans / gaps if gaps > 0 else 0.0
        e1 = max(0.0, (tdr - c["SLO1"]) / c["SLO1"]) if c["SLO1"] > 0 else 0.0
        e2 = max(0.0, (tpot - c["SLO2"]) / c["SLO2"]) if c["SLO2"] > 0 else 0.0
        dist = math.sqrt(e1 * e1 + e2 * e2)
        cw = (max(0.0, 1.0 - dist / c["dist_base"]) if c["dist_base"] > 0
              else (1.0 if dist == 0.0 else 0.0))
        return 1000.0 * (c["w_tp"] * ctp + c["w_c"] * cw)


# ---------------------------------------------------------------------------
# Stream generators (constraint ranges from problem.md:267-289).
# ---------------------------------------------------------------------------
def gen_config(rng):
    K = rng.randint(1, 8)
    wt = rng.choice([0.0, 1.0, 0.5, 0.8, 0.2, rng.random()])
    corner = rng.random()
    dist_base = 0.0 if corner < 0.08 else (1e9 if corner < 0.14
                                           else rng.uniform(0.001, 1e4))
    tp_ub = rng.choice([1e-9, 1e-3, 0.0625, 1.0, 1e3])
    cfg = {
        "K": K,
        "S": rng.choice([1.0, 1.0, 2.5, 10.0, round(rng.uniform(1, 10), 3)]),
        "lat": rng.choice([0.001, 0.1, 2.0, 50.0]),
        "bw": rng.choice([0.001, 0.01, 1.0, 10.0, 100.0]),
        "bpt": rng.choice([1, 125000, 1000000]),
        "layers": rng.choice([1, 2, 4, 8, 16, 32, 64]),
        "SLO1": rng.choice([1e-3, 1.0, 30.0, 1e4, 1e9]),
        "SLO2": rng.choice([1e-3, 1.0, 15.0, 1e4, 1e9]),
        "tp_ub": tp_ub,
        "tp_base": 0.0,
        "dist_base": dist_base,
        "w_tp": wt,
        "w_c": 1.0 - wt,
    }
    if rng.random() < 0.3:
        cfg["tp_base"] = rng.uniform(0, cfg["tp_ub"] * 0.999)
    return cfg


def gen_table(rng, _layers):
    N = rng.randint(2, 12)
    sizes = rng.sample(range(1, 65), min(N, 64))
    fam = rng.choice(["rising", "falling", "flat", "V", "step", "random",
                      "concave"])
    rows = []
    for b in sizes:
        row = [float(b)]
        for _c in range(6):
            miss = rng.random() < 0.18
            if fam == "flat":
                v = 4.0
            elif fam == "rising":
                v = 1.0 + 0.05 * b
            elif fam == "falling":
                v = max(0.5, 8.0 - 0.05 * b)
            elif fam == "V":
                v = 1.0 + abs(b - 8) * 0.4
            elif fam == "step":
                v = 2.0 if b <= 4 else 9.0
            elif fam == "concave":
                v = 1.0 + 8.0 / b
            else:
                v = rng.uniform(0.5, 20)
            row.append(-1.0 if miss else round(v, 3))
        rows.append(row)
    for c in range(6):  # every column >=1 non-missing (problem.md:229)
        if all(r[c + 1] == -1.0 for r in rows):
            rows[rng.randrange(len(rows))][c + 1] = round(rng.uniform(0.5, 10), 3)
    rng.shuffle(rows)  # rows unordered (problem.md:228)
    return rows


def gen_arrivals(rng, r_max=60):
    pat = rng.choice(["single", "burst", "dribble", "poisson", "lull_burst",
                      "ties", "mixed"])
    n = {"single": 1, "burst": rng.randint(2, 40), "dribble": rng.randint(5, 40),
         "poisson": rng.randint(5, 60), "lull_burst": rng.randint(6, 60),
         "ties": rng.randint(2, 30), "mixed": rng.randint(5, 50)}[pat]
    n = min(n, r_max)
    arrs = []
    t = rng.uniform(0, 10)
    lin = lambda: rng.randint(1, 4096)
    if pat == "single":
        arrs = [(t, lin())]
    elif pat == "burst":
        arrs = [(t, lin()) for _ in range(n)]
    elif pat == "dribble":
        for _ in range(n):
            arrs.append((t, lin()))
            t += 1.0
    elif pat == "poisson":
        for _ in range(n):
            arrs.append((t, lin()))
            t += rng.expovariate(0.05)
    elif pat == "lull_burst":
        for i in range(n):
            arrs.append((t, lin()))
            t += 30.0 if i % 5 == 4 else 0.5
    elif pat == "ties":
        base = t
        arrs = [(base + rng.randrange(3) * 1.0, lin()) for _ in range(n)]
    else:
        for _ in range(n):
            arrs.append((t, rng.randint(1, 256)))
            t += rng.choice([0.0, 0.25, 2.0, 15.0])
    return arrs


# ---------------------------------------------------------------------------
# Closed-loop runner.
# ---------------------------------------------------------------------------
def run_stream(solver_path, cfg, table, arrivals, seed, deadline_s=120,
               collect=None, lout_cap=True):
    rng = random.Random(seed)

    def lout_fn():
        # Singleton-heavy draw respecting sum(Lout)<=2e5 scale for big streams;
        # small streams use full-range draws.
        if lout_cap:
            return min(512, 1 + int(rng.expovariate(1 / 60)))
        return rng.randint(1, 512)

    j = Judge(cfg, table, arrivals, rng, lout_fn)
    # NOTE: pipes are BINARY and every frame/startup block is written with ONE
    # os.write call. A line-buffered text pipe splits a frame across multiple
    # wakeups and inflates the solver's measured CPU several-fold (measured
    # 2026-08-23): the harness must not be the benchmark's bottleneck.
    proc = subprocess.Popen([solver_path], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    deadline = time.monotonic() + deadline_s
    wfd = proc.stdin.fileno()

    def send(s):
        os.write(wfd, s.encode())

    try:
        send(f"{cfg['K']} {cfg['S']:.9f} {cfg['lat']:.9f} {cfg['bw']:.9f} "
             f"{cfg['bpt']} {cfg['layers']}\n"
             f"{cfg['SLO1']:.9f} {cfg['SLO2']:.9f} {cfg['tp_ub']:.9f} "
             f"{cfg['tp_base']:.9f} {cfg['dist_base']:.9f} "
             f"{cfg['w_tp']:.9f} {cfg['w_c']:.9f}\n"
             f"{len(table)}\n"
             + "".join(" ".join(f"{v:.9f}" for v in r) + "\n" for r in table))
        rd = TokenReader(proc.stdout, deadline)
        frames = 0
        while True:
            pend = j.pending_events()
            if pend is None:
                j.check_stuck()  # raises unless all done
                break
            tau, lines = pend
            j.now = tau
            send(f"{tau:.9f}\n{len(lines)}\n" + ("\n".join(lines) +
                                                 "\n" if lines else ""))
            frames += 1
            toks = read_response(rd)
            if collect is not None:
                collect.append(toks)
            j.apply_response(toks, tau)
            j.check_stuck()
        send("END\n")
        try:
            proc.stdin.close()
        except BrokenPipeError:
            pass
        rc = proc.wait(timeout=30)
        if rc != 0:
            raise Violation(f"solver exit code {rc} (statement requires 0, :388)")
        if not j.all_done():
            raise Violation("stream ended with unfinished requests")
        return frames, j.debug_score()
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()


# ---------------------------------------------------------------------------
# Mode drivers.
# ---------------------------------------------------------------------------
EX1_FRAMES = [
    ("0.000000000", ["ARR 0 4"]),
    ("4.000000000", ["TDN E P PRE 0 0 3.000000000"]),
    ("10.000000000", ["XDN UP 0 500000 PRE 1 0"]),
    ("21.000000000", ["TDN C0 P PROC 0 4 0 0 10.000000000"]),
    ("27.000000000", ["XDN DOWN 0 500000 PRE 1 0"]),
    ("30.000000000", ["TDN E P POST 0 0 2.000000000"]),
    ("32.000000000", ["TDN E D PRE -1 1 0 1.000000000"]),
    ("35.000000000", ["XDN UP 0 125000 DEC 1 0"]),
    ("40.000000000", ["TDN C0 D PROC 0 1 0 4.000000000"]),
    ("43.000000000", ["XDN DOWN 0 125000 DEC 1 0"]),
    ("45.000000000", ["TDN E D POST -1 1 0 1.000000000", "FIN 0"]),
]
EX1_WANT = [
    ["1", "E", "P", "PRE", "0", "0"],
    ["0"],
    ["1", "C0", "P", "PROC", "0", "4", "0", "0"],
    ["0"],
    ["1", "E", "P", "POST", "0", "0"],
    ["1", "E", "D", "PRE", "-1", "1", "0"],
    ["0"],
    ["1", "C0", "D", "PROC", "0", "1", "0"],
    ["0"],
    ["1", "E", "D", "POST", "-1", "1", "0"],
    ["0"],
]


def mode_replay(solver):
    """Strict transcript replay of Example 1 (problem.md:497-615) — the only
    byte-exact ground truth in the statement."""
    proc = subprocess.Popen([solver], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                            text=True, bufsize=1)
    deadline = time.monotonic() + 60

    def send(s):
        proc.stdin.write(s)
        proc.stdin.flush()

    send("1 1.000000000 2.000000000 1.000000000 125000 4\n")
    send("30.000000000 15.000000000 0.062500000 0.022222222 0.000000000 "
         "0.500000000 0.500000000\n")
    send("2\n")
    send("1 3.000000000 10.000000000 2.000000000 1.000000000 4.000000000 1.000000000\n")
    send("4 3.000000000 10.000000000 2.000000000 1.000000000 4.000000000 1.000000000\n")
    ok = True
    rd = TokenReader(proc.stdout, deadline)
    try:
        for fi, (t, lines) in enumerate(EX1_FRAMES):
            send(f"{t}\n{len(lines)}\n" + "\n".join(lines) + "\n")
            got = read_response(rd)
            want = EX1_WANT[fi]
            if got != want:
                print(f"REPLAY MISMATCH at frame {fi} (t={t}):")
                print(f"  want: {' '.join(want)}")
                print(f"  got : {' '.join(got)}")
                ok = False
                break
        if ok:
            send("END\n")
            proc.stdin.close()
            rc = proc.wait(timeout=15)
            if rc != 0:
                print(f"REPLAY FAIL: exit code {rc}")
                ok = False
    finally:
        if proc.poll() is None:
            proc.kill()
    print("EXAMPLE-1 REPLAY:",
          "PASS (byte-identical responses, 11 frames)" if ok else "FAIL")
    return 0 if ok else 1


def mode_loop(solver, n_streams, seed0, quiet=False):
    fails = 0
    for s in range(n_streams):
        seed = seed0 + s
        rng = random.Random(seed)
        cfg = gen_config(rng)
        table = gen_table(rng, cfg["layers"])
        arrivals = gen_arrivals(rng)
        try:
            run_stream(solver, cfg, table, arrivals, seed)
            if not quiet and (s + 1) % 500 == 0:
                print(f"  .. {s + 1}/{n_streams} streams ok", flush=True)
        except Violation as e:
            fails += 1
            print(f"FAIL seed={seed}: {e}")
            print(f"  cfg={cfg}")
            if fails >= 5:
                print("too many failures, stopping early")
                break
    print(f"FUZZ LOOP: {n_streams - fails}/{n_streams} passed "
          f"({'PASS' if fails == 0 else 'FAIL'})")
    return 0 if fails == 0 else 1


def mode_det(solver, n_streams, seed0):
    diffs = 0
    for s in range(n_streams):
        seed = seed0 + s
        rng = random.Random(seed)
        cfg = gen_config(rng)
        table = gen_table(rng, cfg["layers"])
        arrivals = gen_arrivals(rng)
        log_a, log_b = [], []
        try:
            run_stream(solver, cfg, table, arrivals, seed, collect=log_a)
            run_stream(solver, cfg, table, arrivals, seed, collect=log_b)
            if log_a != log_b:
                diffs += 1
                print(f"NONDETERMINISM seed={seed}")
        except Violation as e:
            print(f"FAIL(seed {seed}, det run): {e}")
            diffs += 1
        if diffs >= 5:
            break
    print(f"DETERMINISM: {n_streams - diffs}/{n_streams} identical "
          f"({'PASS' if diffs == 0 else 'FAIL'})")
    return 0 if diffs == 0 else 1


def mode_stress(solver, hard):
    """Frame/CPU stress. Two streams:
    density (default): R=2000, sum(Lout)~=1.2e5 (:267/:270), fast resources,
      mega-batching allowed — measures raw solver throughput.
    hard (--hard): the SAME size, but a table+SLO2 combination that FORCES
      singleton decode rounds (P_gap(m=2) > SLO2 >= P_gap(1)) and jittered
      arrival/task timings that defeat coalescing — maximizes LEGAL frame
      count toward the statement's 2e6 cap (problem.md:291). A fully legal
      2e6-frame test remains likely unconstructible under the sum(Lout) cap
      (SB03 §5 caveat), so child-CPU is extrapolated linearly and recorded.
    Child CPU is measured exactly via getrusage(RUSAGE_CHILDREN)."""
    rng = random.Random(20260823)
    if hard:
        cfg = dict(K=8, S=1.0, lat=0.05, bw=10.0, bpt=125000, layers=32,
                   SLO1=1e9, SLO2=5.0, tp_ub=1e9, tp_base=0.0, dist_base=1e9,
                   w_tp=0.0, w_c=1.0)
        # dpre/dpost jump after m=1 so any group of 2 breaches SLO2=5:
        #   P(m=1) = 2S + 0.5 + 0.5 = 3 <= 5   (feasible)
        #   P(m=2) = 2S + 3 + 3     = 8  > 5   (infeasible -> m_tp = 1)
        table = [[1.0, 0.5, 4.0, 0.5, 0.5, 2.0, 0.5],
                 [2.0, 0.5, 4.0, 0.5, 3.0, 2.0, 3.0],
                 [4.0, 0.5, 4.0, 0.5, 9.0, 2.0, 9.0],
                 [64.0, 0.5, 4.0, 0.5, 9.0, 2.0, 9.0]]
    else:
        cfg = dict(K=8, S=1.0, lat=0.001, bw=100.0, bpt=125000, layers=32,
                   SLO1=1e9, SLO2=1e9, tp_ub=1e9, tp_base=0.0, dist_base=1e9,
                   w_tp=1.0, w_c=0.0)
        table = [[float(b), 0.5, 4.0, 0.5, 0.5, 2.0, 0.5]
                 for b in (1, 2, 4, 8, 16, 32, 64, 128, 256)]
    arrivals = []
    t = 0.0
    for _i in range(2000):
        arrivals.append((t, rng.randint(1, 64)))
        t += rng.choice([0.0, 0.001, 0.5]) if not hard else \
            rng.choice([0.013, 0.079, 0.211, 0.337])
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    t0 = time.monotonic()
    frames, _score = run_stream(solver, cfg, table, arrivals, 20260823,
                                deadline_s=3600, lout_cap=True)
    wall = time.monotonic() - t0
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    cpu = ((after.ru_utime - before.ru_utime)
           + (after.ru_stime - before.ru_stime))
    rss_mb = after.ru_maxrss / 1024.0
    extrap = cpu * (2_000_000 / max(1, frames))
    print(f"STRESS[{'hard' if hard else 'density'}]: frames={frames} "
          f"child_cpu={cpu:.2f}s wall={wall:.1f}s child_max_rss={rss_mb:.0f}MB")
    print(f"STRESS: extrapolated CPU at 2e6 frames = {extrap:.2f}s "
          f"(limit 15s, program gate 12s; linear model, conservative w.r.t. "
          f"the 1.2M-frame fast-mode guard)")
    ok = extrap < 12.0 and rss_mb < 256
    print("STRESS CPU/MEM:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["replay", "loop", "det", "stress"])
    ap.add_argument("--solver", default="build/submission")
    ap.add_argument("-n", type=int, default=10000)
    ap.add_argument("--seed", type=int, default=777000)
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--hard", action="store_true",
                    help="stress: forced-singleton SLO2 trap (max frames)")
    a = ap.parse_args()
    if a.mode == "replay":
        sys.exit(mode_replay(a.solver))
    elif a.mode == "loop":
        sys.exit(mode_loop(a.solver, a.n, a.seed, a.quiet))
    elif a.mode == "det":
        sys.exit(mode_det(a.solver, min(a.n, 2000), a.seed))
    else:
        sys.exit(mode_stress(a.solver, a.hard))


if __name__ == "__main__":
    main()
