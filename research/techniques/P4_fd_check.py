#!/usr/bin/env python3
"""P4_fd_check.py — finite-difference verification of the NEW State::Oracle
exact score calculus (src/state.hpp), standalone under research/techniques/.

Self-contained: the "exact score" below is implemented directly from
problem.md:425-495 (statement-verified formula); no repository scoring or
judging tooling is invoked. The only repo file executed is the driver
P4_fd_check.cpp, which prints the Oracle bodies under test.

Verified objects (the P4-swapped bodies):
  marginal_tp_v   — V_bar_tp slope, one-sided-full at tp==tp_base, 0 at/above tp_UB
  marginal_tdr_v  — V_P(mean): interior incl. dist==dist_base (recovery dir)
  marginal_tpot_v — V_S(mean): same structure on the tpot axis
  delay_cost_ppost / delay_cost_span — EXACT finite forms, incl. the
                    dist_base==0 cliff (discrete p_violation*1000*w_c)
plus component identities (score / cw / tp) and scale cases (SLO up to 1e9).
"""
import math
import os
import random
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "P4_fd_check.cpp")
BIN = "/tmp/p4fdchk"
SEED = 20260826


def build():
    subprocess.run(
        ["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
         "-o", BIN, SRC], check=True)


# ---------------- exact score (problem.md:425-495) ----------------
def parts(c, E, N, tdr, tpot, G):
    tp = N / E if E > 0.0 else 0.0
    TP = 0.0 if c["tp_UB"] <= c["tp_base"] else max(0.0, min(1.0, (tp - c["tp_base"]) / (c["tp_UB"] - c["tp_base"])))
    e1 = max(0.0, (tdr - c["SLO1"]) / c["SLO1"])
    e2 = max(0.0, (tpot - c["SLO2"]) / c["SLO2"]) if G > 0.0 else 0.0
    d = math.sqrt(e1 * e1 + e2 * e2)
    if c["dist_base"] > 0.0:
        W = max(0.0, 1.0 - d / c["dist_base"])
    else:
        W = 1.0 if d == 0.0 else 0.0
    return {"tp": tp, "TP": TP, "e1": e1, "e2": e2, "dist": d, "W": W,
            "S": 1000.0 * (c["w_tp"] * TP + c["w_c"] * W)}


def scoreE(c, E, N, tdr, tpot, G):
    return 1000.0 * c["w_tp"] * parts(c, E, N, tdr, tpot, G)["TP"]


def scoreTdr(c, tdr, E, N, tpot, G):
    return 1000.0 * c["w_c"] * parts(c, E, N, tdr, tpot, G)["W"]


def scoreTpot(c, tpot, E, N, tdr, G):
    return 1000.0 * c["w_c"] * parts(c, E, N, tdr, tpot, G)["W"]


# ---------------- FD machinery (SB04_check.py method) ----------------
def d4(f, x, h):
    return (f(x - 2 * h) - 8.0 * f(x - h) + 8.0 * f(x + h) - f(x + 2 * h)) / (12.0 * h)


def d4r(f, x, h):
    return (16.0 * d4(f, x, h / 2.0) - d4(f, x, h)) / 15.0


def deriv_slack(f, x, slack):
    h = 0.05 * slack
    if h <= 0.0:
        return None, False
    v1 = d4r(f, x, h)
    v2 = d4r(f, x, h / 4.0)
    if not (math.isfinite(v1) and math.isfinite(v2)):
        return None, False
    ok = abs(v1 - v2) <= 3e-10 * max(abs(v1), abs(v2), 1e-300)
    return v1, ok


def slack_E(c, E, N):
    tp = N / E
    s = 0.9 * E
    if tp < c["tp_base"]:
        if c["tp_base"] > 0:
            s = min(s, E - N / c["tp_base"])
    elif tp >= c["tp_UB"]:
        s = min(s, N / c["tp_UB"] - E)
    else:
        s = min(s, E - N / c["tp_UB"])
        if c["tp_base"] > 0:
            s = min(s, N / c["tp_base"] - E)
    return s


def _slack_axis(c, x, other, G, which):
    p = parts(c, 1.0, 1.0, x, other, G) if which == "tdr" else parts(c, 1.0, 1.0, other, x, G)
    e, SLO = (p["e1"], c["SLO1"]) if which == "tdr" else (p["e2"], c["SLO2"])
    s = abs(x - SLO)
    d = p["dist"]
    if c["dist_base"] > 0 and d > 0 and e > 0:
        rate = e / (d * SLO)
        if d < c["dist_base"]:
            s = min(s, 0.5 * (c["dist_base"] - d) / rate)
        else:
            s = min(s, (d - c["dist_base"]) / rate)
    return max(s, 1e-12)


# ---------------- samplers (ported from SB04_check.py) ----------------
def rand_cfg(rng, force=None):
    c = {}
    r = rng.random()
    if r < 0.05:
        c["w_tp"], c["w_c"] = 1.0, 0.0
    elif r < 0.10:
        c["w_tp"], c["w_c"] = 0.0, 1.0
    else:
        c["w_tp"] = rng.random()
        c["w_c"] = 1.0 - c["w_tp"]
    c["tp_base"] = rng.choice([0.0, 10 ** rng.uniform(-6, 2)])
    width = 10 ** rng.uniform(-4, 2) * (1.0 + rng.random())
    c["tp_UB"] = c["tp_base"] + max(width, 1e-3 * c["tp_base"])
    c["SLO1"] = 10 ** rng.uniform(-3, 9)          # incl. the 1e9 constraint corner
    c["SLO2"] = 10 ** rng.uniform(-3, 9)
    if force == "cliff":
        c["dist_base"] = 0.0
    elif force == "interior_wait":
        c["dist_base"] = 10 ** rng.uniform(-3, 3)
    else:
        c["dist_base"] = rng.choice([0.0, 10 ** rng.uniform(-6, 3)])
    return c


def make_tp_interior(rng, c, lo=0.08, hi=0.92):
    N = rng.uniform(1.0, 2e5)
    frac = lo + (hi - lo) * rng.random()
    return N / (c["tp_base"] + frac * (c["tp_UB"] - c["tp_base"])), N


def make_excess(rng, SLO, target):
    if target <= 0.0:
        return SLO * (1.0 - 10 ** rng.uniform(-3, 0.3) * 0.9)
    return SLO * (1.0 + target)


def sample_states(rng):
    """(name, cfg, E, N, tdr, tpot, G, ndly, ddly, rhat, ghat)"""
    S = []

    def add(name, c, E, N, tdr, tpot, G, nd=1, dd=None, rhat=None, ghat=None):
        # Mirror the artifact's arithmetic EXACTLY: tokens_out is an integer,
        # r_hat/g_hat arrive as ints through the driver protocol.
        N = float(max(1, int(N)))
        r_ = rhat if rhat is not None else max(1.0, rng.uniform(1, 2000))
        g_ = ghat if ghat is not None else max(1.0, G if G > 0 else rng.uniform(1, 2e5))
        r_ = float(max(1, int(round(r_))))
        g_ = float(max(1, int(round(g_))))
        if dd is None:
            dd = abs(rng.gauss(1.0, 1.0)) * max(1e-3, abs(c["SLO1"] - tdr)) * r_ / nd
        S.append((name, c, E, N, tdr, tpot, G, nd, dd, r_, g_))

    for _ in range(200):                      # CAT1: V_E live
        c = rand_cfg(rng)
        if c["w_tp"] <= 0.0:
            c["w_tp"], c["w_c"] = 0.7, 0.3
        E, N = make_tp_interior(rng, c)
        G = rng.choice([0.0, rng.uniform(1.0, 2e5)])
        tdr = make_excess(rng, c["SLO1"], rng.choice([0.0, 0.4, 2.0]))
        tpot = make_excess(rng, c["SLO2"], rng.choice([0.0, 0.4, 2.0])) if G > 0 else 0.0
        add("ve_live", c, E, N, tdr, tpot, G)
    for _ in range(25):                       # CAT1b: kinks at base / UB
        c = rand_cfg(rng)
        if c["w_tp"] <= 0.0:
            c["w_tp"], c["w_c"] = 0.7, 0.3
        N = float(max(1, int(rng.uniform(1.0, 2e5))))
        E = 10 ** rng.uniform(0, 5)
        if rng.random() < 0.5:
            c["tp_base"] = N / E
            c["tp_UB"] = c["tp_base"] * (1.0 + rng.random())
            add("ve_kink_base", c, E, N, c["SLO1"] * 0.5, c["SLO2"] * 0.5, 10.0)
        else:
            c["tp_UB"] = N / E
            c["tp_base"] = c["tp_UB"] * rng.random() * 0.9
            add("ve_kink_ub", c, E, N, c["SLO1"] * 0.5, c["SLO2"] * 0.5, 10.0)
    for _ in range(60):                       # CAT2/3: saturated both sides
        c = rand_cfg(rng)
        if c["w_tp"] <= 0.0:
            c["w_tp"], c["w_c"] = 0.7, 0.3
        N = rng.uniform(1.0, 2e5)
        if rng.random() < 0.5 and c["tp_base"] > 0.0:
            tp_t = c["tp_base"] * rng.uniform(0.01, 0.85)
        else:
            tp_t = c["tp_UB"] * rng.uniform(1.05, 8.0)
        add("ve_dead", c, N / tp_t, N, c["SLO1"] * 2.0, c["SLO2"] * 2.0, 50.0)
    for _ in range(180):                      # CAT4: V_P live
        c = rand_cfg(rng, "interior_wait")
        if c["w_c"] <= 0.0:
            c["w_tp"], c["w_c"] = 0.3, 0.7
        R = rng.uniform(1.0, 2000.0)
        tdr = c["SLO1"] * (1.0 + rng.uniform(0.05, 3.0))
        G = rng.choice([0.0, rng.uniform(1.0, 2e5)])
        tpot = make_excess(rng, c["SLO2"], rng.choice([0.0, 0.0, 0.3, 1.5])) if G > 0 else 0.0
        add("vp_live", c, 100.0, 1000.0, tdr, tpot, G, rhat=R)
    for _ in range(60):                       # CAT5: e1==0, e2>0
        c = rand_cfg(rng, "interior_wait")
        if c["w_c"] <= 0.0:
            c["w_tp"], c["w_c"] = 0.3, 0.7
        R = rng.uniform(1, 2000)
        add("vp_dead_e0", c, 100.0, 1000.0, c["SLO1"] * rng.uniform(0.1, 0.9),
            c["SLO2"] * 2.5, rng.uniform(10.0, 1e4), rhat=R)
    for _ in range(60):                       # CAT6: past the cliff edge
        c = rand_cfg(rng, "interior_wait")
        if c["w_c"] <= 0.0:
            c["w_tp"], c["w_c"] = 0.3, 0.7
        c["dist_base"] = 10 ** rng.uniform(-6, -1)
        R = rng.uniform(1, 2000)
        add("vp_past_cliff", c, 100.0, 1000.0, c["SLO1"] * 6.0, c["SLO2"] * 6.0,
            rng.uniform(10.0, 1e4), rhat=R)
    for _ in range(60):                       # CAT7a: dist==0 slack interior
        c = rand_cfg(rng, "interior_wait")
        if c["w_c"] <= 0.0:
            c["w_tp"], c["w_c"] = 0.3, 0.7
        R = rng.uniform(1, 2000)
        add("corner_slack", c, 100.0, 1000.0, c["SLO1"] * rng.uniform(0.05, 0.95),
            c["SLO2"] * rng.uniform(0.05, 0.95), rng.uniform(10.0, 1e4), rhat=R)
    for _ in range(80):                       # CAT7b: ON the knife edges
        c = rand_cfg(rng, "interior_wait")
        if c["w_c"] <= 0.0:
            c["w_tp"], c["w_c"] = 0.3, 0.7
        R = rng.uniform(1.0, 2000.0)
        if rng.random() < 0.5:
            tdr = c["SLO1"] * (1.0 + 10 ** rng.uniform(-4, 0))
            c["SLO1"] = tdr
            add("knife_tdr", c, 100.0, 1000.0, tdr,
                c["SLO2"] * rng.uniform(0.05, 0.95), rng.uniform(10.0, 1e4), rhat=R)
        else:
            G = rng.uniform(10.0, 1e5)
            tpot = c["SLO2"] * (1.0 + 10 ** rng.uniform(-4, 0))
            c["SLO2"] = tpot
            add("knife_tpot", c, 100.0, 1000.0,
                c["SLO1"] * rng.uniform(0.05, 0.95), tpot, G, rhat=R, ghat=G)
    for _ in range(170):                      # CAT8: V_S live
        c = rand_cfg(rng, "interior_wait")
        if c["w_c"] <= 0.0:
            c["w_tp"], c["w_c"] = 0.3, 0.7
        tpot = c["SLO2"] * (1.0 + rng.uniform(0.05, 3.0))
        tdr = make_excess(rng, c["SLO1"], rng.choice([0.0, 0.0, 0.4]))
        G = rng.uniform(1.0, 2e5)
        add("vs_live", c, 100.0, 1000.0, tdr, tpot, G, ghat=G)
    for _ in range(120):                      # CAT9: cliff discrete behavior
        c = rand_cfg(rng, "cliff")
        if c["w_c"] <= 0.0:
            c["w_tp"], c["w_c"] = 0.4, 0.6
        R = rng.uniform(1.0, 2000.0)
        G = rng.uniform(10.0, 1e5)
        u = rng.uniform(0.001, 0.5)
        kind = rng.randrange(4)
        if kind == 0:
            tdr, tpot = c["SLO1"] * (1.0 + u), c["SLO2"] * rng.uniform(0.1, 0.95)
            dd = R * c["SLO1"] * (u + rng.uniform(0.01, 0.4))
        elif kind == 1:
            tdr, tpot = c["SLO1"] * (1.0 + u), c["SLO2"] * rng.uniform(0.1, 0.95)
            dd = R * c["SLO1"] * u * rng.uniform(0.1, 0.8)
        elif kind == 2:
            tdr, tpot = c["SLO1"] * rng.uniform(0.1, 0.95), c["SLO2"] * (1.0 + u)
            dd = G * c["SLO2"] * (u + rng.uniform(0.01, 0.4))
        else:
            tdr, tpot = c["SLO1"] * (1.0 + u), c["SLO2"] * (1.0 + u)
            dd = rng.choice([R * c["SLO1"] * 2.0, G * c["SLO2"] * 2.0])
        nd = 1 if kind != 2 else 1
        dl = dd if kind != 2 else None
        if kind == 2:
            # span-side probe: size the span delta via G
            add("cliff", c, 100.0, 1000.0, tdr, tpot, G, nd=1,
                dd=G * c["SLO2"] * (u + rng.uniform(0.01, 0.4)), rhat=R, ghat=G)
        else:
            add("cliff", c, 100.0, 1000.0, tdr, tpot, G, nd=nd, dd=dl, rhat=R, ghat=G)
    for _ in range(60):                       # CAT10-scale: SLO ~ 1e9 corners
        c = rand_cfg(rng)
        if c["w_c"] <= 0.0:
            c["w_tp"], c["w_c"] = 0.3, 0.7
        c["SLO1"] = c["SLO2"] = 1e9
        R = rng.uniform(1.0, 2000.0)
        G = rng.uniform(10.0, 2e5)
        add("slo_1e9", c, 100.0, 1000.0,
            c["SLO1"] * rng.choice([0.5, 1.0000001, 2.0]),
            c["SLO2"] * rng.choice([0.5, 1.0000001, 2.0]), G, rhat=R, ghat=G)
    return S


FIELDS = ["mtpv", "mtdrv", "mtpotv", "score", "tp", "dcpp", "dcsp", "cw"]


def run_states(states):
    inp = []
    for st in states:
        _n, c, E, N, tdr, tpot, G, nd, dd, r_, g_ = st
        inp.append(
            "A %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %d %.17g %d %d"
            % (c["w_tp"], c["w_c"], c["tp_base"], c["tp_UB"], c["SLO1"], c["SLO2"],
               c["dist_base"], 0.0, E, N, tdr, tpot, G, nd, dd, round(r_), round(g_)))
    p = subprocess.run([BIN], input="\n".join(inp) + "\n",
                       capture_output=True, text=True, check=True)
    out = []
    for ln in p.stdout.strip().splitlines():
        toks = ln.split()
        assert len(toks) == len(FIELDS), ln
        out.append({k: float(v) for k, v in zip(FIELDS, toks)})
    assert len(out) == len(states)
    return out


def rel(a, b):
    return abs(a - b) / max(abs(a), abs(b), 1e-300)


def main():
    build()
    rng = random.Random(SEED)
    states = sample_states(rng)
    outs = run_states(states)
    fails = []
    maxerr = {}
    zero_ok = {}
    skipped = [0]

    def note(k, e):
        maxerr[k] = max(maxerr.get(k, 0.0), e)

    for st, o in zip(states, outs):
        name, c, E, N, tdr, tpot, G, nd, dd, r_, g_ = st
        p0 = parts(c, E, N, tdr, tpot, G)
        for py, ck in ((p0["S"], "score"), (p0["tp"], "tp"), (p0["W"], "cw")):
            if rel(py, o[ck]) > 1e-12:
                fails.append("%s: %s py=%.17g cpp=%.17g" % (name, ck, py, o[ck]))
        # --- marginal_tp_v: slope * N/E^2 must equal -dScore/dE ---
        sE = slack_E(c, E, N)
        on_base = abs(N / E - c["tp_base"]) <= 0.0
        on_ub = c["tp_UB"] > 0 and (N / E) == c["tp_UB"]
        if o["mtpv"] != 0.0:
            if on_base:
                pass  # covered by the dedicated kink block below
            else:
                fd, okc = deriv_slack(lambda x: scoreE(c, x, N, tdr, tpot, G), E, sE)
                if fd is None or not okc:
                    skipped[0] += 1
                else:
                    e = rel(-fd, o["mtpv"] * N / (E * E))
                    note("V_E(mtpv*N/E^2)", e)
                    if e > 1e-8:
                        fails.append("mtpv %s: fd=%.17g cpp=%.17g rel=%.3g"
                                     % (name, -fd, o["mtpv"] * N / (E * E), e))
        else:
            h = 0.25 * sE
            fE = lambda x: scoreE(c, x, N, tdr, tpot, G)
            if not on_ub and not (fE(E - h) == fE(E) == fE(E + h)):
                fails.append("mtpv zero-region but score moves (%s)" % name)
            zero_ok["V_E_exact0"] = zero_ok.get("V_E_exact0", 0) + 1
        # --- marginal_tdr_v vs -d(scoreTdr)/dtdr ---
        sT = _slack_axis(c, tdr, tpot, G, "tdr")
        if o["mtdrv"] != 0.0:
            fd, okc = deriv_slack(lambda x: scoreTdr(c, x, E, N, tpot, G), tdr, sT)
            if fd is None or not okc:
                skipped[0] += 1
            else:
                e = rel(-fd, o["mtdrv"])
                note("V_P_mean", e)
                if e > 1e-8:
                    fails.append("mtdrv %s: fd=%.17g cpp=%.17g" % (name, -fd, o["mtdrv"]))
        else:
            h = 0.25 * sT
            fT = lambda x: scoreTdr(c, x, E, N, tpot, G)
            # Reduction side must be flat; an increase-side drop AT the knife
            # (tdr==SLO1, dist==0) is the exact one-sided behavior under test.
            if fT(tdr - h) != fT(tdr):
                fails.append("mtdrv reduction side moves (%s)" % name)
            zero_ok["V_P_exact0"] = zero_ok.get("V_P_exact0", 0) + 1
        # --- marginal_tpot_v ---
        if G > 0.0:
            sP = _slack_axis(c, tpot, tdr, G, "tpot")
            if o["mtpotv"] != 0.0:
                fd, okc = deriv_slack(lambda x: scoreTpot(c, x, E, N, tdr, G), tpot, sP)
                if fd is None or not okc:
                    skipped[0] += 1
                else:
                    e = rel(-fd, o["mtpotv"])
                    note("V_S_mean", e)
                    if e > 1e-8:
                        fails.append("mtpotv %s: fd=%.17g cpp=%.17g" % (name, -fd, o["mtpotv"]))
            else:
                h = 0.25 * sP
                fP = lambda x: scoreTpot(c, x, E, N, tdr, G)
                if fP(tpot - h) != fP(tpot):
                    fails.append("mtpotv reduction side moves (%s)" % name)
                zero_ok["V_S_exact0"] = zero_ok.get("V_S_exact0", 0) + 1
        else:
            if o["mtpotv"] != 0.0:
                fails.append("G==0 but mtpotv nonzero (%s)" % name)
            zero_ok["V_S_G0"] = zero_ok.get("V_S_G0", 0) + 1
        # --- delay costs: mirrored-expression bitwise + exact difference ---
        if c["w_c"] > 0.0 and dd > 0.0 and nd > 0:
            # ppost side (mean shift nd*dd/r_)
            tdr2 = tdr + nd * dd / r_
            w_after_p = parts(c, 1.0, 1.0, tdr2, tpot, G)["W"]
            mirror_p = 1000.0 * c["w_c"] * (p0["W"] - w_after_p)
            if mirror_p != o["dcpp"]:
                fails.append("dcpp mirror %s: %.17g vs %.17g" % (name, mirror_p, o["dcpp"]))
            exact_p = p0["S"] - parts(c, E, N, tdr2, tpot, G)["S"]
            if abs(exact_p) >= 1e-5 * max(1.0, p0["S"]):
                e = rel(exact_p, o["dcpp"])
                note("delayCostPpost(exact)", e)
                if e > 1e-8:
                    fails.append("dcpp %s: exact=%.17g cpp=%.17g" % (name, exact_p, o["dcpp"]))
            # span side (mean shift nd*dd/g_) — cliff included
            if G > 0.0 and g_ > 0:
                tpot2 = tpot + nd * dd / g_
                w_after_s = parts(c, 1.0, 1.0, tdr, tpot2, G)["W"]
                mirror_s = 1000.0 * c["w_c"] * (p0["W"] - w_after_s)
                if mirror_s != o["dcsp"]:
                    fails.append("dcsp mirror %s: %.17g vs %.17g" % (name, mirror_s, o["dcsp"]))
                exact_s = p0["S"] - parts(c, E, N, tdr, tpot2, G)["S"]
                if abs(exact_s) >= 1e-5 * max(1.0, p0["S"]):
                    e = rel(exact_s, o["dcsp"])
                    note("delayCostSpan(exact)", e)
                    if e > 1e-8:
                        fails.append("dcsp %s: exact=%.17g cpp=%.17g" % (name, exact_s, o["dcsp"]))
        # --- cliff semantics: dcsp must price the full jump when it flips W ---
        if c["dist_base"] == 0.0 and c["w_c"] > 0.0 and G > 0.0 and g_ > 0 and dd > 0.0:
            tpot2 = tpot + nd * dd / g_
            flip = (p0["W"] == 1.0) and (parts(c, 1.0, 1.0, tdr, tpot2, G)["W"] == 0.0)
            if flip and o["dcsp"] <= 0.0:
                fails.append("cliff dcsp must price jump (%s)" % name)
            if not flip and o["dcsp"] != 0.0:
                fails.append("cliff dcsp nonzero without flip (%s)" % name)
            zero_ok["cliff_dcsp"] = zero_ok.get("cliff_dcsp", 0) + 1

    # --- kink semantics at tp == tp_base bitwise: slope must be the FULL rate
    #     (one-sided into the ramp), verified against a large-h one-sided FD.
    for st, o in zip(states, outs):
        name, c, E, N, tdr, tpot, G, *_ = st
        if name != "ve_kink_base":
            continue
        # One-sided rate INTO the ramp: reducing E by h raises tp above base.
        h = 0.2 * max(1e-12, E - N / c["tp_UB"])
        g1 = (scoreE(c, E - h, N, tdr, tpot, G) - scoreE(c, E, N, tdr, tpot, G)) / h
        gh = (scoreE(c, E - h / 2, N, tdr, tpot, G) - scoreE(c, E, N, tdr, tpot, G)) / (h / 2.0)
        gr = 2.0 * gh - g1
        if abs(gh - g1) <= 1e-6 * max(abs(o["mtpv"] * N / (E * E)), 1e-300):
            e = rel(gr, o["mtpv"] * N / (E * E))
            note("V_E(kink@base)", e)
            if e > 1e-8 or o["mtpv"] <= 0.0:
                fails.append("kink@base: rich=%.17g cpp=%.17g" % (gr, o["mtpv"] * N / (E * E)))
    bad = [st[0] for st, o in zip(states, outs) if st[0] == "ve_kink_ub" and o["mtpv"] != 0.0]
    if bad:
        fails.append("mtpv at tp==tp_UB must be exactly 0")

    print("== P4 Oracle FD verification: %d randomized states (seed %d) ==" % (len(states), SEED))
    print("%-24s %14s" % ("formula", "max rel err"))
    for k in sorted(maxerr):
        print("%-24s %14.3e" % (k, maxerr[k]))
    for k in sorted(zero_ok):
        print("%-24s %14s   exact-0 checks: %d" % (k, "0 (exact)", zero_ok[k]))
    worst = max(maxerr.values()) if maxerr else 0.0
    if fails:
        print("FAILS (%d):" % len(fails))
        for f in fails[:25]:
            print("  " + f)
        sys.exit(1)
    print("uncertified-FD skips (ill-conditioned, not asserted): %d" % skipped[0])
    print("FD ALL PASS (max in-region rel err %.3e < 1e-8 gate; zeros exact)" % worst)


if __name__ == "__main__":
    main()
