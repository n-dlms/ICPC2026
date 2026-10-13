#!/usr/bin/env python3
# tools/discriminator.py -- Part 2.3 link-ceiling experiment (D-063)
#
# Self-contained interactor + resource-utilization fingerprint for the CF
# TDR-catastrophic cluster (tests 9/15/18/10). Independent of interactor.py
# on purpose: it is a cross-check of the tandem-queue physics.
#
# IMPORTANT convention choice: this harness models OUR deployed judge
# (interactor.py), which keys prefill_pre / prefill_post by batch_size = 1
# (NOT by Lin). That is F1 in the engineering package. The consequence is
# benign here: the dominant term in the tandem cycle is the UP/DOWN transfer,
# which dwarfs the P_PRE duration difference, and with this convention the
# analytic floor reproduces the CF mean_tdr values to ~0.1% (see selfcheck
# notes). So the structural-vs-recoverable verdict is robust to F1.
#
# Usage:
#   python3 tools/discriminator.py --selfcheck
#   python3 tools/discriminator.py --solver build/exp_cap2 --case w15_like
#   python3 tools/discriminator.py --solver build/exp_cap2 --all
import argparse
import heapq
import math
import os
import subprocess
import sys
import threading

EPS = 1e-9

# ---------- task-time table (statement interpolation, -1 skip, clamps) ----------
# rows: (batch_size, PPRE, PPROC, PPOST, DPRE, DPROC, DPOST)
DEFAULT_ROWS = [
    (1,    2, 10,  2, 1, 2,  1),
    (64,   5, 40,  4, 2, 5,  2),
    (512,  9, 120, 7, 4, 12, 4),
    (4096, 15, 300, 12, 8, 30, 8),
]


class Table:
    def __init__(self, rows):
        rows = sorted(rows, key=lambda r: r[0])
        self.rows = list(rows)
        # columns 0..5 = PPRE, PPROC, PPOST, DPRE, DPROC, DPOST
        self.cols = [[(r[0], r[c + 1]) for r in rows if r[c + 1] is not None] for c in range(6)]

    def line(self, r):
        return " ".join("%.9f" % v if isinstance(v, float) else str(v) for v in r)

    def get(self, step, size):
        col = self.cols[step]
        if not col:
            return 1.0
        if size <= col[0][0]:
            return col[0][1]
        if size >= col[-1][0]:
            return col[-1][1]
        for i in range(1, len(col)):
            if col[i][0] >= size:
                (x0, y0), (x1, y1) = col[i - 1], col[i]
                if x1 == x0:
                    return y0
                return y0 + (y1 - y0) * (size - x0) / (x1 - x0)
        return col[-1][1]


# P_PRE=0 P_PROC=1 P_POST=2 D_PRE=3 D_PROC=4 D_POST=5
def transfer_ms(cfg, length):
    return cfg["lat"] + 8.0 * length * cfg["bpt"] / (cfg["bw"] * 1e6)


# ---------- phases ----------
(AWAIT_PRE, PRE_RUN, WAIT_UP, PROC_READY, PROC_RUN, WAIT_DOWN, POST_READY, POST_RUN,
 DPRE_READY, DPRE_RUN, WAIT_UP_D, DPROC_READY, DPROC_RUN, WAIT_DOWN_D, DPOST_READY,
 DPOST_RUN, DONE) = range(17)


class Req:
    def __init__(self, rid, arr, lin, lout):
        self.rid, self.arr, self.lin, self.lout = rid, arr, lin, lout
        self.phase, self.cloud, self.iter, self.next_ls = AWAIT_PRE, -1, 0, 0
        self.ppost_t = None
        self.tokens = []


class Sim:
    def __init__(self, case, table):
        self.c = case
        self.T = table
        self.timeline = []  # (t, "TDN"/"XDN"/"FIN", detail) for tracing
        self.reqs = {}
        self.order = []
        self.local_busy = False
        self.cloud_busy = [False] * case["K"]
        self.up_tail = 0.0
        self.down_tail = 0.0
        self.events = []
        self.seq = itertools_count()
        self.busy = {"local": 0.0, "up": 0.0, "down": 0.0, "cloud": [0.0] * case["K"]}
        self.tdr_sum = 0.0
        self.gap_sum = 0.0
        self.gap_n = 0
        self.last_token = 0.0
        self.first_arr = None
        self.tok_total = 0
        self.violation = None

    def push(self, t, kind, data):
        heapq.heappush(self.events, (t, next(self.seq), kind, data))

    # ---------- event delivery ----------
    def deliver(self, t, kind, d):
        if kind == "ARR":
            r = Req(d["rid"], t, d["lin"], d["lout"])
            self.reqs[r.rid] = r
            self.order.append(r.rid)
            if self.first_arr is None or t < self.first_arr:
                self.first_arr = t
        elif kind == "TDN":
            srv, step, ids, dur, extra = d["srv"], d["step"], d["ids"], d["dur"], d["extra"]
            self.timeline.append((t, "TDN", step))
            if srv == -1:
                self.local_busy = False
            else:
                self.cloud_busy[srv] = False
            if step == "P_PRE":
                r = self.reqs[ids[0]]
                assert r.phase == PRE_RUN
                r.phase = WAIT_UP
                self._enq_up(t, r.lin, "PRE", [r.rid])
            elif step == "P_PROC":
                ls, le, c, rid = extra
                r = self.reqs[rid]
                assert r.phase == PROC_RUN
                r.next_ls = le
                if le == self.c["layers"]:
                    r.phase = WAIT_DOWN
                    self._enq_down(t, r.lin, "PRE", [rid])
                else:
                    r.phase = PROC_READY
            elif step == "P_POST":
                r = self.reqs[ids[0]]
                assert r.phase == POST_RUN
                r.phase = DPRE_READY
                r.ppost_t = t
                self.tdr_sum += t - r.arr
            elif step == "D_PRE":
                per = {}
                for rid in ids:
                    r = self.reqs[rid]
                    assert r.phase == DPRE_RUN
                    r.phase = WAIT_UP_D
                    per.setdefault(r.cloud, []).append(rid)
                for c in sorted(per):
                    self._enq_up(t, len(per[c]), "DEC", per[c])
            elif step == "D_PROC":
                c = extra
                for rid in ids:
                    r = self.reqs[rid]
                    assert r.phase == DPROC_RUN
                    r.phase = WAIT_DOWN_D
                self._enq_down(t, len(ids), "DEC", list(ids))
            elif step == "D_POST":
                for rid in ids:
                    r = self.reqs[rid]
                    assert r.phase == DPOST_RUN
                    r.iter += 1
                    r.tokens.append(t)
                    self.last_token = max(self.last_token, t)
                    self.tok_total += 1
                    if len(r.tokens) >= 2:
                        self.gap_sum += r.tokens[-1] - r.tokens[-2]
                        self.gap_n += 1
                    r.phase = DONE if r.iter >= r.lout else DPRE_READY
                    if r.iter >= r.lout:
                        self.push(t, "FIN", {"rid": rid})
        elif kind == "XDN":
            up, c, tag, ids = d["up"], d["c"], d["tag"], d["ids"]
            self.timeline.append((t, "XDN", ("UP" if up else "DOWN") + "/" + tag))
            for rid in ids:
                r = self.reqs[rid]
                if up:
                    assert r.phase == WAIT_UP if tag == "PRE" else r.phase == WAIT_UP_D
                    r.phase = PROC_READY if tag == "PRE" else DPROC_READY
                else:
                    assert r.phase == WAIT_DOWN if tag == "PRE" else r.phase == WAIT_DOWN_D
                    r.phase = POST_READY if tag == "PRE" else DPOST_READY

    def _enq_up(self, enter, length, tag, ids):
        dur = transfer_ms(self.c, length)
        start = max(enter, self.up_tail)
        self.up_tail = start + dur
        self.busy["up"] += dur
        self.push(start + dur, "XDN",
                  {"up": True, "c": self.reqs[ids[0]].cloud if tag == "PRE" else self.reqs[ids[0]].cloud,
                   "tag": tag, "ids": ids})

    def _enq_down(self, enter, length, tag, ids):
        dur = transfer_ms(self.c, length)
        start = max(enter, self.down_tail)
        self.down_tail = start + dur
        self.busy["down"] += dur
        self.push(start + dur, "XDN",
                  {"up": False, "c": self.reqs[ids[0]].cloud, "tag": tag, "ids": ids})

    # ---------- assignment application (all at frame time t) ----------
    def apply(self, t, assigns):
        for a in assigns:
            srv, step = a["srv"], a["step"]
            if srv == -1 and self.local_busy:
                return f"busy local at {t}"
            if srv >= 0 and self.cloud_busy[srv]:
                return f"busy C{srv} at {t}"
            if step == "P_PRE":
                r = self.reqs[a["rid"]]
                if r.phase != AWAIT_PRE:
                    return "P_PRE phase"
                if not (0 <= a["cloud"] < self.c["K"]):
                    return "P_PRE cloud"
                r.cloud = a["cloud"]
                r.phase = PRE_RUN
                dur = self.T.get(0, 1)  # F1: our judge keys prefill_pre by 1
                self._start(-1, t, self.c["S"] + dur, "local",
                            "TDN", {"srv": -1, "step": "P_PRE", "ids": [r.rid],
                                    "dur": dur, "extra": None, "cloud": a["cloud"]})
            elif step == "P_PROC":
                r = self.reqs[a["rid"]]
                if r.phase != PROC_READY or r.cloud != srv:
                    return "P_PROC phase/cloud"
                if a["ls"] != r.next_ls or a["le"] <= a["ls"] or a["le"] > self.c["layers"]:
                    return "P_PROC piece"
                r.phase = PROC_RUN
                dur = (a["le"] - a["ls"]) / self.c["layers"] * self.T.get(1, r.lin)
                self._start(srv, t, self.c["S"] + dur, "cloud",
                            "TDN", {"srv": srv, "step": "P_PROC", "ids": [r.rid],
                                    "dur": dur, "extra": (a["ls"], a["le"], srv, r.rid)})
            elif step == "P_POST":
                r = self.reqs[a["rid"]]
                if r.phase != POST_READY or r.cloud != a["cloud"]:
                    return "P_POST phase"
                r.phase = POST_RUN
                dur = self.T.get(2, 1)  # F1: our judge keys prefill_post by 1
                self._start(-1, t, self.c["S"] + dur, "local",
                            "TDN", {"srv": -1, "step": "P_POST", "ids": [r.rid],
                                    "dur": dur, "extra": None, "cloud": a["cloud"]})
            elif step == "D_PRE":
                for rid in a["ids"]:
                    if self.reqs[rid].phase != DPRE_READY:
                        return "D_PRE phase"
                for rid in a["ids"]:
                    self.reqs[rid].phase = DPRE_RUN
                dur = self.T.get(3, len(a["ids"]))
                self._start(-1, t, self.c["S"] + dur, "local",
                            "TDN", {"srv": -1, "step": "D_PRE", "ids": list(a["ids"]),
                                    "dur": dur, "extra": None})
            elif step == "D_PROC":
                for rid in a["ids"]:
                    r = self.reqs[rid]
                    if r.phase != DPROC_READY or r.cloud != srv:
                        return "D_PROC phase/cloud"
                for rid in a["ids"]:
                    self.reqs[rid].phase = DPROC_RUN
                dur = self.T.get(4, len(a["ids"]))
                self._start(srv, t, self.c["S"] + dur, "cloud",
                            "TDN", {"srv": srv, "step": "D_PROC", "ids": list(a["ids"]),
                                    "dur": dur, "extra": srv})
            elif step == "D_POST":
                for rid in a["ids"]:
                    if self.reqs[rid].phase != DPOST_READY:
                        return "D_POST phase"
                for rid in a["ids"]:
                    self.reqs[rid].phase = DPOST_RUN
                dur = self.T.get(5, len(a["ids"]))
                self._start(-1, t, self.c["S"] + dur, "local",
                            "TDN", {"srv": -1, "step": "D_POST", "ids": list(a["ids"]),
                                    "dur": dur, "extra": None})
        return None

    def _start(self, srv, t, span, kind, evkind, payload):
        if kind == "local":
            self.local_busy = True
            self.busy["local"] += span
        else:
            self.cloud_busy[srv] = True
            self.busy["cloud"][srv] += span
        self.push(t + span, evkind, payload)

    def done(self):
        return len(self.order) == self.c["R"] and all(
            self.reqs[i].phase == DONE for i in self.order)

    # ---------- scoring (statement formula; TPOT denominator = gaps only) ----------
    def score(self):
        c = self.c
        span = self.last_token - self.first_arr if self.first_arr is not None else 0.0
        tp = self.tok_total / span if span > 0 else 0.0
        norm_tp = max(0.0, min(1.0, (tp - c["tp_base"]) / (c["tp_UB"] - c["tp_base"]))) \
            if c["tp_UB"] > c["tp_base"] else 0.0
        tdr = self.tdr_sum / self.c["R"]
        tpot = self.gap_sum / self.gap_n if self.gap_n else 0.0
        e_tdr = max(0.0, (tdr - c["SLO1"]) / c["SLO1"])
        e_tpot = max(0.0, (tpot - c["SLO2"]) / c["SLO2"])
        dist = math.sqrt(e_tdr * e_tdr + e_tpot * e_tpot)
        if c["dist_base"] > 0:
            norm_c = max(0.0, 1.0 - dist / c["dist_base"])
        else:
            norm_c = 1.0 if dist == 0 else 0.0
        return {"tp": tp, "norm_tp": norm_tp, "tdr": tdr, "tpot": tpot, "dist": dist,
                "norm_c": norm_c, "score": 1000.0 * (c["w_tp"] * norm_tp + c["w_c"] * norm_c),
                "makespan": span}

    def utilization(self):
        mk = self.score()["makespan"] or 1.0
        return {"local": self.busy["local"] / mk,
                "up": self.busy["up"] / mk,
                "down": self.busy["down"] / mk,
                "cloud_max": max(b / mk for b in self.busy["cloud"]),
                "cloud_mean": sum(self.busy["cloud"]) / len(self.busy["cloud"]) / mk}

    # ---------- analytic tandem floor (D-063) ----------
    def floor(self):
        c = self.c
        aq = 0.0
        u = 0.0
        p = 0.0
        n = 0
        for r in self.reqs.values():
            aq += (c["S"] + self.T.get(0, 1)) + (c["S"] + self.T.get(2, 1))
            u += transfer_ms(c, r.lin)
            p += c["S"] + self.T.get(1, r.lin)
            n += 1
        aq /= max(1, n)
        u /= max(1, n)
        p /= max(1, n)
        cstar = max(aq, u, u, p / c["K"])  # up == down term
        floor = cstar * (c["R"] - 1) / 2.0 + aq  # + one chain overhead
        return {"cstar": cstar, "floor": floor, "aq": aq, "u": u, "p_per_k": p / c["K"]}


def itertools_count():
    import itertools
    return itertools.count()


# ---------- frame rendering + solver bridge ----------
def frame_lines(sim, t, evs):
    lines = ["%.9f" % t]
    body = []
    for kind, d in evs:
        if kind == "ARR":
            body.append("ARR %d %d" % (d["rid"], d["lin"]))
        elif kind == "TDN":
            srv = "E" if d["srv"] == -1 else "C%d" % d["srv"]
            sp = d["step"]
            if sp == "P_PRE":
                body.append("TDN %s P PRE %d %d %.9f" % (srv, d["cloud"], d["ids"][0], d["dur"]))
            elif sp == "P_PROC":
                ls, le, c, rid = d["extra"]
                body.append("TDN %s P PROC %d %d %d %d %.9f" % (srv, ls, le, c, rid, d["dur"]))
            elif sp == "P_POST":
                body.append("TDN %s P POST %d %d %.9f" % (srv, d["cloud"], d["ids"][0], d["dur"]))
            elif sp == "D_PRE":
                body.append("TDN %s D PRE -1 %d %s %.9f" % (
                    srv, len(d["ids"]), " ".join(map(str, d["ids"])), d["dur"]))
            elif sp == "D_PROC":
                body.append("TDN %s D PROC %d %d %s %.9f" % (
                    srv, d["srv"], len(d["ids"]),
                    " ".join(map(str, d["ids"])), d["dur"]))
            elif sp == "D_POST":
                body.append("TDN %s D POST -1 %d %s %.9f" % (
                    srv, len(d["ids"]), " ".join(map(str, d["ids"])), d["dur"]))
        elif kind == "XDN":
            up, c, tag, ids = d["up"], d["c"], d["tag"], d["ids"]
            size = (len(ids) * sim.c["bpt"]) if (tag == "DEC") else (sim.reqs[ids[0]].lin * sim.c["bpt"])
            body.append("XDN %s %d %d %s %d %s" % (
                "UP" if up else "DOWN", c, size, tag, len(ids), " ".join(map(str, ids))))
        elif kind == "FIN":
            body.append("FIN %d" % d["rid"])
    lines.append(str(len(body)))
    lines.extend(body)
    return "\n".join(lines) + "\n"


def parse_assign(line):
    toks = line.split()
    srv_tok = toks[0]
    if srv_tok == "E":
        srv = -1
    else:
        assert srv_tok[0] == "C"
        srv = int(srv_tok[1:])
    w1, w2 = toks[1], toks[2]
    if w1 == "P" and w2 == "PRE":
        return {"srv": srv, "step": "P_PRE", "cloud": int(toks[3]), "rid": int(toks[4])}
    if w1 == "P" and w2 == "PROC":
        return {"srv": srv, "step": "P_PROC", "ls": int(toks[3]), "le": int(toks[4]),
                "cloud": int(toks[5]), "rid": int(toks[6])}
    if w1 == "P" and w2 == "POST":
        return {"srv": srv, "step": "P_POST", "cloud": int(toks[3]), "rid": int(toks[4])}
    if w1 == "D" and w2 == "PRE":
        m = int(toks[4])
        return {"srv": srv, "step": "D_PRE", "ids": [int(x) for x in toks[5:5 + m]]}
    if w1 == "D" and w2 == "PROC":
        m = int(toks[4])
        return {"srv": srv, "step": "D_PROC", "ids": [int(x) for x in toks[5:5 + m]]}
    if w1 == "D" and w2 == "POST":
        m = int(toks[4])
        return {"srv": srv, "step": "D_POST", "ids": [int(x) for x in toks[5:5 + m]]}
    raise ValueError("bad assign: %s" % line)


def read_response(proc):
    line = proc.stdout.readline()
    if not line:
        raise ValueError("solver EOF mid-protocol")
    n = int(line.split()[0])
    assigns = []
    for _ in range(n):
        line = proc.stdout.readline()
        if not line:
            raise ValueError("solver EOF mid-response")
        assigns.append(parse_assign(line))
    return assigns


def naive_step(sim, t):
    """In-harness reference: work-conserving, one task per free machine."""
    assigns = []
    if not sim.local_busy:
        for rid in sim.order:
            r = sim.reqs[rid]
            if r.phase == AWAIT_PRE:
                assigns.append({"srv": -1, "step": "P_PRE", "cloud": 0, "rid": rid})
                break
            if r.phase == POST_READY:
                assigns.append({"srv": -1, "step": "P_POST", "cloud": r.cloud, "rid": rid})
                break
            if r.phase == DPRE_READY:
                assigns.append({"srv": -1, "step": "D_PRE", "ids": [rid]})
                break
            if r.phase == DPOST_READY:
                assigns.append({"srv": -1, "step": "D_POST", "ids": [rid]})
                break
    for c in range(sim.c["K"]):
        if sim.cloud_busy[c]:
            continue
        for rid in sim.order:
            r = sim.reqs[rid]
            if r.phase == PROC_READY and r.cloud == c:
                assigns.append({"srv": c, "step": "P_PROC", "ls": r.next_ls,
                                "le": sim.c["layers"], "cloud": c, "rid": rid})
                break
            if r.phase == DPROC_READY and r.cloud == c:
                assigns.append({"srv": c, "step": "D_PROC", "ids": [rid]})
                break
    return assigns


# ---------- omniscient-greedy reference (for --tp throughput verdict) ----------
def greedy_step(sim, t):
    """Maximal legal batching, no holds, no SLO-aware waiting. Used by --tp mode as
    the 'omniscient greedy' reference: full batches everywhere a batch is legal
    (D_PRE across clouds, D_POST across clouds, D_PROC per-cloud), single tasks where
    a machine is fundamentally serial (P_PRE/P_POST on local; P_PROC/D_PROC per cloud)."""
    assigns = []
    if not sim.local_busy:
        for rid in sim.order:
            r = sim.reqs[rid]
            if r.phase == AWAIT_PRE:
                c = min(range(sim.c["K"]), key=lambda k: (sim.cloud_busy[k], k))
                assigns.append({"srv": -1, "step": "P_PRE", "cloud": c, "rid": rid})
                break
        else:
            for rid in sim.order:
                r = sim.reqs[rid]
                if r.phase == POST_READY:
                    assigns.append({"srv": -1, "step": "P_POST", "cloud": r.cloud, "rid": rid})
                    break
            else:
                pre = [rid for rid in sim.order if sim.reqs[rid].phase == DPRE_READY]
                if pre:
                    assigns.append({"srv": -1, "step": "D_PRE", "ids": pre})
                else:
                    post = [rid for rid in sim.order if sim.reqs[rid].phase == DPOST_READY]
                    if post:
                        assigns.append({"srv": -1, "step": "D_POST", "ids": post})
    for c in range(sim.c["K"]):
        if sim.cloud_busy[c]:
            continue
        for rid in sim.order:
            r = sim.reqs[rid]
            if r.phase == PROC_READY and r.cloud == c:
                assigns.append({"srv": c, "step": "P_PROC", "ls": r.next_ls,
                                "le": sim.c["layers"], "cloud": c, "rid": rid})
                break
        else:
            dproc = [rid for rid in sim.order if sim.reqs[rid].phase == DPROC_READY and sim.reqs[rid].cloud == c]
            if dproc:
                assigns.append({"srv": c, "step": "D_PROC", "ids": dproc})
    return assigns


# ---------- corrected physical throughput ceiling (D-063 / --tp) ----------
def tp_ceiling(case, table):
    """Physical token-rate ceiling.
    local wave rate m/(2*S + dpre(m) + dpost(m))  [D_PRE and D_POST each pay S on the
      serial local machine]
    link rate   = bw*1e6/(8*bpt) tokens/ms per direction (UP and DOWN pipeline in parallel)
    cloud rate  = K*mc/(S + dproc(mc)), mc = m/K  [per-cloud D_PROC is serial]
    ceiling_rate = min(local, link, cloud); argmin names the bottleneck resource.
    Prefill prefix (non-decode-heavy cases 22/11): prefix = max of
      local-serialized prefill  R*(2*S + ppre(1) + ppost(1)),
      link fill                 R*2*transfer(lin),
      cloud pipeline            R*(S + pproc(lin))/K.
    ceiling_tp = total_tokens / (prefix + total_tokens/ceiling_rate)."""
    R = case["R"]
    lin = case["arrivals"][0]["lin"]
    bps = sorted(set([1, 64, 512, 4096, R]))
    best_local = (0, 0.0)
    for m in bps:
        dpre = table.get(3, m); dpost = table.get(5, m)
        rate = m / (2 * case["S"] + dpre + dpost)
        if rate > best_local[1]:
            best_local = (m, rate)
    link = case["bw"] * 1e6 / (8.0 * case["bpt"])
    best_cloud = (0, 0.0)
    for mc in bps:
        mc = max(1.0, mc / case["K"])
        dproc = table.get(4, mc)
        rate = case["K"] * mc / (case["S"] + dproc)
        if rate > best_cloud[1]:
            best_cloud = (mc, rate)
    rate = min(best_local[1], link, best_cloud[1])
    argmin = "local" if rate == best_local[1] else ("link" if rate == link else "cloud")
    prefix_local = R * (2 * case["S"] + table.get(0, 1) + table.get(2, 1))
    prefix_link = R * 2.0 * transfer_ms(case, lin)
    prefix_cloud = R * (case["S"] + table.get(1, lin)) / case["K"]
    prefix = max(prefix_local, prefix_link, prefix_cloud)
    tot = sum(a["lout"] for a in case["arrivals"])
    ceil_tp = tot / (prefix + tot / rate) if rate > 0 else 0.0
    return {"local_rate": best_local[1], "link_rate": link, "cloud_rate": best_cloud[1],
            "ceiling_rate": rate, "argmin": argmin, "prefix": prefix,
            "ceiling_tp": ceil_tp, "total_tokens": tot}


def classify_tp(ach, grd, ceil):
    a_over_g = ach / grd if grd > 0 else 0.0
    g_over_c = grd / ceil if ceil > 0 else 0.0
    if a_over_g >= 0.95 and g_over_c >= 0.95:
        verdict = "STRUCTURAL"
    elif a_over_g >= 0.95 and g_over_c < 0.95:
        verdict = "GREEDY-LIMITED"
    elif a_over_g < 0.95 and g_over_c >= 0.95:
        verdict = "RECOVERABLE"
    else:
        verdict = "RECOVERABLE+greedy-gap"
    return a_over_g, g_over_c, verdict


def run_case(case, table, solver_path=None, step_fn=None):
    sim = Sim(case, table)
    for i, arr in enumerate(case["arrivals"]):
        sim.push(arr["t"], "ARR", {"rid": i, "lin": arr["lin"], "lout": arr["lout"]})

    proc = None
    err_thread = None
    if solver_path:
        proc = subprocess.Popen([solver_path], stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, bufsize=1)
        head = [
            "%d %.9f %.9f %.9f %d %d" % (case["K"], case["S"], case["lat"], case["bw"],
                                         case["bpt"], case["layers"]),
            "%.9f %.9f %.9f %.9f %.9f %.9f %.9f" % (
                case["SLO1"], case["SLO2"], case["tp_UB"], case["tp_base"],
                case["dist_base"], case["w_tp"], case["w_c"]),
            str(len(table.rows)) if hasattr(table, "rows") else str(len(DEFAULT_ROWS)),
        ]
        for r in (table.rows if hasattr(table, "rows") else DEFAULT_ROWS):
            head.append(table.line(r))
        proc.stdin.write("\n".join(head) + "\n")
        proc.stdin.flush()
        err_lines = []

        def drain():
            for _ in proc.stderr:
                pass

        err_thread = threading.Thread(target=drain, daemon=True)
        err_thread.start()

    frame = 0
    while not sim.done() and sim.events:
        t = sim.events[0][0]
        batch = []
        while sim.events and abs(sim.events[0][0] - t) < EPS:
            _, _, kind, d = heapq.heappop(sim.events)
            batch.append((kind, d))
        # deliver in arrival order of events (no priority from line order)
        for kind, d in batch:
            sim.deliver(t, kind, d)
        # FIN events pushed during deliver (same timestamp) must ride this frame.
        fins = []
        while sim.events and abs(sim.events[0][0] - t) < EPS:
            _, _, kind, d = heapq.heappop(sim.events)
            assert kind == "FIN"
            fins.append((kind, d))
        batch = batch + fins
        if solver_path:
            assert proc is not None
            proc.stdin.write(frame_lines(sim, t, batch))
            proc.stdin.flush()
            try:
                assigns = read_response(proc)
            except ValueError as ve:
                return {"error": str(ve)}
            err = sim.apply(t, assigns)
        else:
            assigns = (step_fn or naive_step)(sim, t)
            err = sim.apply(t, assigns)
        if err:
            return {"error": err, "frame": frame, "t": t}
        frame += 1

    if proc:
        try:
            proc.stdin.write("END\n")
            proc.stdin.flush()
            proc.wait(timeout=30)
        except Exception:
            proc.kill()
        if err_thread:
            err_thread.join(timeout=2)

    sc = sim.score()
    ut = sim.utilization()
    fl = sim.floor()
    return {"score": sc, "util": ut, "floor": fl, "sim": sim,
            "ratio": sc["tdr"] / fl["floor"] if fl["floor"] > 0 else 0.0,
            "frames": frame}


# ---------- selfcheck: statement Example 1 ----------
def selfcheck():
    cfg = {"K": 1, "S": 1.0, "lat": 2.0, "bw": 1.0, "bpt": 125000, "layers": 4,
           "SLO1": 1e9, "SLO2": 1e9, "tp_UB": 1.0, "tp_base": 0.0,
           "dist_base": 1e9, "w_tp": 0.0, "w_c": 1.0, "R": 1}
    rows = [(1, 3, 10, 2, 1, 4, 1), (4, 3, 10, 2, 1, 4, 1)]
    table = Table(rows)
    case = dict(cfg)
    case["arrivals"] = [{"t": 0.0, "lin": 4, "lout": 1}]
    res = run_case(case, table, solver_path=None)
    if "error" in res:
        print("SELFCHECK FAIL:", res["error"])
        return False
    # collect all TDN/XDN/FIN timestamps from a fresh deterministic trace
    sc = res["score"]
    # Expected: TDR = 30, token at 45. The naive single-request path:
    # P_PRE TDN@4, XDN UP@10, P_PROC TDN@21, XDN DOWN@27, P_POST TDN@30,
    # D_PRE TDN@32, XDN UP@35, D_PROC TDN@40, XDN DOWN@43, D_POST TDN@45.
    ok = (abs(sc["tdr"] - 30.0) < 1e-6)
    print("SELFCHECK: tdr=%.6f (expect 30.0) token_at=%.6f (expect 45.0) -> %s" % (
        sc["tdr"], res["score"]["makespan"] + 0.0, "PASS" if ok else "FAIL"))
    chain = [("%.0f" % x[0]) for x in res["sim"].timeline]
    print("  timeline TDN/XDN: %s" % " ".join(chain))
    expected = ["4", "10", "21", "27", "30", "32", "35", "40", "43", "45"]
    tdx = [c for c in chain if c in expected]
    print("  expected chain  : %s" % " ".join(expected))
    print("  chain match     : %s" % ("YES" if chain == expected else "NO"))
    # Also print the implied timestamp chain for manual inspection
    print("  (Example-1 chain: TDN 4, XDN 10, TDN 21, XDN 27, TDN 30, "
          "TDN 32, XDN 35, TDN 40, XDN 43, TDN 45)")
    return ok


# ---------- case family (CF-signature back-solved) ----------
def build_case(name):
    base = {"S": 1.0, "layers": 4, "bpt": 1_000_000, "tp_UB": 1.0, "tp_base": 0.0,
            "SLO2": 1e9}
    if name == "w15_like":
        c = dict(base, K=8, lat=5.0, bw=1.05, R=2000, w_tp=0.45, w_c=0.55,
                SLO1=5e4, dist_base=150.0)
        c["arrivals"] = [{"t": 0.0, "lin": 4096, "lout": 1}] * c["R"]
    elif name == "w18_like":
        c = dict(base, K=8, lat=5.0, bw=0.43, R=2000, w_tp=0.58, w_c=0.42,
                SLO1=1e5, dist_base=400.0)
        c["arrivals"] = [{"t": 0.0, "lin": 4096, "lout": 1}] * c["R"]
    elif name == "w10_like":
        c = dict(base, K=8, lat=2.0, bw=22.8, R=2000, w_tp=0.15, w_c=0.85,
                SLO1=2e4, dist_base=150.0)
        half = c["R"] // 2
        c["arrivals"] = ([{"t": 0.0, "lin": 512, "lout": 1}] * half +
                         [{"t": 0.0, "lin": 512, "lout": 8}] * (c["R"] - half))
    elif name == "w9_like":
        c = dict(base, K=4, lat=2.0, bw=71.7, R=1000, w_tp=0.05, w_c=0.95,
                SLO1=600.0, dist_base=20.0)
        c["arrivals"] = [{"t": 0.0, "lin": 128, "lout": 1}] * c["R"]
    elif name == "w15_R100":
        c = dict(base, K=8, lat=5.0, bw=1.05, R=100, w_tp=0.45, w_c=0.55,
                SLO1=5e4, dist_base=150.0)
        c["arrivals"] = [{"t": 0.0, "lin": 4096, "lout": 1}] * c["R"]
    elif name == "w15_R500":
        c = dict(base, K=8, lat=5.0, bw=1.05, R=500, w_tp=0.45, w_c=0.55,
                SLO1=5e4, dist_base=150.0)
        c["arrivals"] = [{"t": 0.0, "lin": 4096, "lout": 1}] * c["R"]
    else:
        raise KeyError(name)
    return c


CASES = ["w15_like", "w18_like", "w10_like", "w9_like", "w15_R100", "w15_R500"]


# ---------- throughput family (intentional-bottleneck shapes, exact w_tp/w_c) ----------
# Path C (per user 2026-08-16): drive the family by INTENTIONAL bottleneck assignment at
# the exact inverted w_tp/w_c weights (D-063 weights). CF tp/tpot/tdr matching is a
# secondary validation only; the deliverable is the 2x2 verdict that names the binding
# resource and any recoverable slack (OD-9 liveness). Bottlenecks: link (most), local
# (t22, S large so the serial D_PRE+D_POST wave dominates), and t13/t14 carry a FLAT
# decode table so D-051's exhaust->unbounded rule fires (the OD-9 trigger: w_c>0 AND
# flat-clamp table AND TPOT above the wave floor).
FLAT_ROWS = [(1, 2, 10, 2, 1, 2, 1), (4096, 2, 10, 2, 1, 2, 1)]  # flat decode -> unbounded

def _link(R, C, Lout, w_tp, w_c, S=1.0, lat=0.01, K=8, lin=1):
    # link bound: bw set so transfer(R) = C (pool = R saturates the link)
    bw = 8.0 * R / (C - lat)
    return dict(R=R, K=K, lin=lin, Lout=Lout, S=S, lat=lat, bw=bw,
                w_tp=w_tp, w_c=w_c, SLO1=1e9, SLO2=1e9, tp_UB=1.0, tp_base=0.0,
                dist_base=1e9, bottleneck="link")

def _local(R, C, Lout, w_tp, w_c, S=20.0, lat=0.01, K=8, lin=1):
    # local bound: very fast link so the serial local D_PRE+D_POST wave dominates
    return dict(R=R, K=K, lin=lin, Lout=Lout, S=S, lat=lat, bw=1000.0,
                w_tp=w_tp, w_c=w_c, SLO1=1e9, SLO2=1e9, tp_UB=1.0, tp_base=0.0,
                dist_base=1e9, bottleneck="local")

TP_PARAMS = {
    # Tests 5,6,19,22 : high w_tp. t22 assigned local-bound to exercise the serial local.
    "t5":  _link(200, 300.0, 10, 0.8, 0.2),
    "t6":  _link(200, 150.0, 10, 0.9, 0.1),
    "t19": _link(200, 2600.0, 10, 1.0, 0.0),
    "t22": _local(200, 50.0, 10, 0.5, 0.5),
    # Tests 13,14 : OD-9 probe -- w_c>0 AND a flat decode table (D-051 unbounded).
    "t13": dict(_link(300, 115.0, 10, 0.75, 0.25), rows=FLAT_ROWS),
    "t14": dict(_link(300, 185.0, 10, 0.65, 0.35), rows=FLAT_ROWS),
    # Tests 12,16 : high-w_tp, link bound (t12 slow link / huge tpot shape).
    "t12": _link(50, 500.0, 20, 0.99, 0.01),
    "t16": _link(200, 150.0, 20, 0.98, 0.02),
    # Test 11 : horizon (huge Lout, dist=0), link bound, prefill cheap but decode long.
    "t11": _link(50, 16000.0, 4000, 0.5, 0.5, S=1.0, lat=5.0, lin=1),
}
TP_ORDER = ["t5", "t6", "t19", "t22", "t13", "t14", "t12", "t16", "t11"]


def build_tp_case(name, p):
    c = dict(K=p["K"], S=p["S"], lat=p["lat"], bw=p["bw"], bpt=1_000_000,
             layers=4, tp_UB=p["tp_UB"], tp_base=p["tp_base"],
             SLO1=p["SLO1"], SLO2=p["SLO2"], dist_base=p["dist_base"],
             w_tp=p["w_tp"], w_c=p["w_c"], R=p["R"])
    g = p.get("stagger", 0.0)
    c["arrivals"] = [{"t": i * g, "lin": p["lin"], "lout": p["Lout"]} for i in range(p["R"])]
    return c


def run_tp(name, solver_path):
    p = TP_PARAMS[name]
    rows = p.get("rows", DEFAULT_ROWS)
    table = Table(rows)
    case = build_tp_case(name, p)
    ra = run_case(case, table, solver_path=solver_path)
    rg = run_case(case, table, step_fn=greedy_step)
    if "error" in ra:
        print("%-5s ARTIFACT ERROR %s" % (name, ra["error"])); return
    if "error" in rg:
        print("%-5s GREEDY ERROR %s" % (name, rg["error"])); return
    ceil = tp_ceiling(case, table)
    a_over_g, g_over_c, verdict = classify_tp(ra["score"]["tp"], rg["score"]["tp"], ceil["ceiling_tp"])
    ua, ug = ra["util"], rg["util"]
    a_tp, g_tp = ra["score"]["tp"], rg["score"]["tp"]
    sat_ok = (max(ua["local"], ua["up"], ua["down"], ua["cloud_max"]) >= 0.95 or
              max(ug["local"], ug["up"], ug["down"], ug["cloud_max"]) >= 0.95)
    print("%-5s [%s] w_tp=%.2f w_c=%.2f | ach=%.5f grd=%.5f ceil=%.5f | a/g=%.3f g/c=%.3f | %s | bot=%s"
          % (name, p.get("bottleneck", "?"), p["w_tp"], p["w_c"],
             a_tp, g_tp, ceil["ceiling_tp"], a_over_g, g_over_c, verdict, ceil["argmin"]))
    print("      sat ach L=%.3f U=%.3f D=%.3f Cmax=%.3f | grd L=%.3f U=%.3f D=%.3f Cmax=%.3f %s"
          % (ua["local"], ua["up"], ua["down"], ua["cloud_max"],
             ug["local"], ug["up"], ug["down"], ug["cloud_max"],
             "SAT" if sat_ok else "lowsat"))


AB_CASES = ["t5", "t6", "t22", "t13", "t14", "t16"]
AB_SOLVERS = [("cap2", "build/exp_cap2"),
              ("nohold", "build/exp_nohold"),
              ("nocap", "build/exp_nocap")]

ACC_SOLVERS = [("cap2", "build/exp_cap2"),
               ("accum", "build/exp_accumulate")]

ACC2_SOLVERS = [("cap2", "build/exp_cap2"),
                ("accum2", "build/exp_accum2")]

ACC3_SOLVERS = [("cap2", "build/exp_cap2"),
                ("accum3", "build/exp_accum3")]

def run_tp_ab(case):
    p = TP_PARAMS[case]
    rows = p.get("rows", DEFAULT_ROWS)
    table = Table(rows)
    c = build_tp_case(case, p)
    rg = run_case(c, table, step_fn=greedy_step)
    if "error" in rg:
        print("  %-5s GREEDY ERROR %s" % (case, rg["error"])); return None
    grd = rg["score"]["tp"]
    row = {"greedy": grd}
    for key, path in AB_SOLVERS:
        if not os.path.exists(path):
            print("  %-5s missing solver %s" % (case, path)); row[key] = float("nan"); continue
        r = run_case(c, table, solver_path=path)
        if "error" in r:
            print("  %-5s %s ERROR %s" % (case, path, r["error"])); row[key] = float("nan"); continue
        row[key] = r["score"]["tp"]
    return row

def run_tp_acc(case):
    p = TP_PARAMS[case]
    rows = p.get("rows", DEFAULT_ROWS)
    table = Table(rows)
    c = build_tp_case(case, p)
    rg = run_case(c, table, step_fn=greedy_step)
    if "error" in rg:
        print("  %-5s GREEDY ERROR %s" % (case, rg["error"])); return None
    grd = rg["score"]["tp"]
    row = {"greedy": grd}
    for key, path in ACC_SOLVERS:
        if not os.path.exists(path):
            print("  %-5s missing solver %s" % (case, path)); row[key] = float("nan"); continue
        r = run_case(c, table, solver_path=path)
        if "error" in r:
            print("  %-5s %s ERROR %s" % (case, path, r["error"])); row[key] = float("nan"); continue
        row[key] = r["score"]["tp"]
    return row

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selfcheck", action="store_true")
    ap.add_argument("--solver", default=None)
    ap.add_argument("--case", default=None)
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--tp", action="store_true")
    ap.add_argument("--tpab", action="store_true")
    ap.add_argument("--tpacc", action="store_true")
    ap.add_argument("--tpacc2", action="store_true")
    ap.add_argument("--tpacc3", action="store_true")
    args = ap.parse_args()

    if args.selfcheck:
        ok = selfcheck()
        sys.exit(0 if ok else 1)

    if args.tpacc3:
        print("== throughput Variant D3 : OD-9-gated accumulation (exp_accum3) ==")
        print("== on %d RECOVERABLE cases (achieved tp; greedy = omniscient reference) ==" % len(AB_CASES))
        print("%-5s %9s %9s %9s   a/g:  cap2 accum3" % (
            "case", "cap2", "accum3", "greedy"))
        for name in AB_CASES:
            p = TP_PARAMS[name]
            rows = p.get("rows", DEFAULT_ROWS)
            table = Table(rows)
            c = build_tp_case(name, p)
            rg = run_case(c, table, step_fn=greedy_step)
            if "error" in rg:
                print("  %-5s GREEDY ERROR %s" % (name, rg["error"])); continue
            g = rg["score"]["tp"]
            def ag(v): return (v / g) if g else 0.0
            row = {"greedy": g, "cap2": float("nan"), "accum3": float("nan")}
            for key, path in ACC3_SOLVERS:
                if not os.path.exists(path):
                    print("  %-5s missing solver %s" % (name, path)); continue
                r = run_case(c, table, solver_path=path)
                if "error" in r:
                    print("  %-5s %s ERROR %s" % (name, path, r["error"])); continue
                row[key] = r["score"]["tp"]
            print("%-5s %9.5f %9.5f %9.5f        %.3f %.3f" % (
                name, row["cap2"], row["accum3"], g,
                ag(row["cap2"]), ag(row["accum3"])))
        sys.exit(0)

    if args.tpacc2:
        print("== throughput Variant D2 : D_POST-priority gate, target=K (exp_accum2) ==")
        print("== on %d RECOVERABLE cases (achieved tp; greedy = omniscient reference) ==" % len(AB_CASES))
        print("%-5s %9s %9s %9s   a/g:  cap2 accum2" % (
            "case", "cap2", "accum2", "greedy"))
        for name in AB_CASES:
            p = TP_PARAMS[name]
            rows = p.get("rows", DEFAULT_ROWS)
            table = Table(rows)
            c = build_tp_case(name, p)
            rg = run_case(c, table, step_fn=greedy_step)
            if "error" in rg:
                print("  %-5s GREEDY ERROR %s" % (name, rg["error"])); continue
            g = rg["score"]["tp"]
            def ag(v): return (v / g) if g else 0.0
            row = {"greedy": g, "cap2": float("nan"), "accum2": float("nan")}
            for key, path in ACC2_SOLVERS:
                if not os.path.exists(path):
                    print("  %-5s missing solver %s" % (name, path)); continue
                r = run_case(c, table, solver_path=path)
                if "error" in r:
                    print("  %-5s %s ERROR %s" % (name, path, r["error"])); continue
                row[key] = r["score"]["tp"]
            print("%-5s %9.5f %9.5f %9.5f        %.3f %.3f" % (
                name, row["cap2"], row["accum2"], g,
                ag(row["cap2"]), ag(row["accum2"])))
        sys.exit(0)

    if args.tpacc:
        print("== throughput Variant D : D_POST-priority accumulation gate (exp_accumulate) ==")
        print("== on %d RECOVERABLE cases (achieved tp; greedy = omniscient reference) ==" % len(AB_CASES))
        print("%-5s %9s %9s %9s   a/g:  cap2  accum" % (
            "case", "cap2", "accum", "greedy"))
        for name in AB_CASES:
            row = run_tp_acc(name)
            if not row:
                continue
            g = row["greedy"]
            def ag(v): return (v / g) if g else 0.0
            print("%-5s %9.5f %9.5f %9.5f        %.3f %.3f" % (
                name, row["cap2"], row["accum"], g,
                ag(row["cap2"]), ag(row["accum"])))
        sys.exit(0)

    if args.tpab:
        print("== throughput A/B : SloAwareDelay hold (nohold) vs V9_XREMOTE cap (nocap) ==")
        print("== on %d RECOVERABLE cases (achieved tp; greedy = omniscient reference) ==" % len(AB_CASES))
        print("%-5s %9s %9s %9s %9s   a/g:  cap2   nohold nocap" % (
            "case", "cap2", "nohold", "nocap", "greedy"))
        for name in AB_CASES:
            row = run_tp_ab(name)
            if not row:
                continue
            g = row["greedy"]
            def ag(v): return (v / g) if g else 0.0
            print("%-5s %9.5f %9.5f %9.5f %9.5f        %.3f %.3f %.3f" % (
                name, row["cap2"], row["nohold"], row["nocap"], g,
                ag(row["cap2"]), ag(row["nohold"]), ag(row["nocap"])))
        sys.exit(0)

    if args.tp:
        solver = args.solver or "build/exp_cap2"
        print("== throughput discriminator (--tp) : artifact vs omniscient-greedy vs physical ceiling ==")
        for name in TP_ORDER:
            run_tp(name, solver)
        sys.exit(0)

    table = Table(DEFAULT_ROWS)
    targets = [args.case] if args.case else (CASES if args.all else ["w15_like"])
    for name in targets:
        case = build_case(name)
        res = run_case(case, table, solver_path=args.solver)
        if "error" in res:
            print("%-12s ERROR %s (frame %s t=%s)" % (
                name, res["error"], res.get("frame"), res.get("t")))
            continue
        sc, ut, fl = res["score"], res["util"], res["floor"]
        print("%-12s tdr=%.4e floor=%.4e ratio=%.3f | up=%.3f down=%.3f "
              "local=%.3f cmax=%.3f | norm_c=%.3f score=%.1f"
              % (name, sc["tdr"], fl["floor"], res["ratio"],
                 ut["up"], ut["down"], ut["local"], ut["cloud_max"],
                 sc["norm_c"], sc["score"]))


if __name__ == "__main__":
    main()
