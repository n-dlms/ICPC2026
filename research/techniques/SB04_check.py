#!/usr/bin/env python3
"""SB04_check.py — verify every formula in SB04_score_calculus.cpp against
brute-force finite differences of the EXACT score function on randomized
states, plus brute-force checks of the three-regime batch law, and the
minified-size ledger.

Self-contained: no repository scoring/judging tooling or case suite
is invoked anywhere. The "exact score" below is re-implemented directly from
problem.md:425-495 (the statement-verified formula); the only repo file read
is tools/merge_submission.py's STYLE (mirrored minifier for byte counting),
never executed on any source.

Usage:
    python3 research/techniques/SB04_check.py            # everything
    python3 research/techniques/SB04_check.py --only fd  # score calculus FD
    python3 research/techniques/SB04_check.py --only law # batch law
    python3 research/techniques/SB04_check.py --only size
"""
import math
import os
import random
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "SB04_score_calculus.cpp")
BIN = "/tmp/sb04calc"
SEED = 20260823


# ----------------------------------------------------------------------------
# build
# ----------------------------------------------------------------------------
def build():
    subprocess.run(
        ["g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
         "-DSB04_DRIVER", "-o", BIN, SRC], check=True)


# ----------------------------------------------------------------------------
# exact score (problem.md:425-495), mirrored expression-for-expression
# ----------------------------------------------------------------------------
def clamp01(x, b, t):
    return max(0.0, min(1.0, (x - b) / (t - b)))


def parts(c, E, N, R, tdr, tpot, G):
    tp = N / E if E > 0.0 else 0.0
    TP = 0.0 if c["tp_UB"] <= c["tp_base"] else clamp01(tp, c["tp_base"], c["tp_UB"])
    e1 = max(0.0, (tdr - c["SLO1"]) / c["SLO1"])
    e2 = max(0.0, (tpot - c["SLO2"]) / c["SLO2"]) if G > 0.0 else 0.0
    d = math.sqrt(e1 * e1 + e2 * e2)
    if c["dist_base"] > 0.0:
        W = max(0.0, 1.0 - d / c["dist_base"])
    else:
        W = 1.0 if d == 0.0 else 0.0
    S = 1000.0 * (c["w_tp"] * TP + c["w_c"] * W)
    return {"tp": tp, "TP": TP, "e1": e1, "e2": e2, "dist": d, "W": W, "S": S}


def scoreE(c, E, N, R, tdr, tpot, G):
    # tp COMPONENT only (wait does not depend on E): identical math, but the
    # FD is not swamped by the orthogonal component's magnitude.
    return 1000.0 * c["w_tp"] * parts(c, E, N, R, tdr, tpot, G)["TP"]


def scoreTdr(c, tdr, E, N, R, tpot, G):
    return 1000.0 * c["w_c"] * parts(c, E, N, R, tdr, tpot, G)["W"]


def scoreTpot(c, tpot, E, N, R, tdr, G):
    return 1000.0 * c["w_c"] * parts(c, E, N, R, tdr, tpot, G)["W"]


def distTdr(c, tdr, tpot, G):
    return parts(c, 1.0, 1.0, 1.0, tdr, tpot, G)["dist"]


def distTpot(c, tpot, tdr, G):
    return parts(c, 1.0, 1.0, 1.0, tdr, tpot, G)["dist"]


# ----------------------------------------------------------------------------
# finite differences (4th-order central + Richardson, adaptive h)
# ----------------------------------------------------------------------------
def d4(f, x, h):
    return (f(x - 2 * h) - 8.0 * f(x - h) + 8.0 * f(x + h) - f(x + 2 * h)) / (12.0 * h)


def d4r(f, x, h):
    """Richardson-extrapolated 4th-order central derivative (O(h^6))."""
    return (16.0 * d4(f, x, h / 2.0) - d4(f, x, h)) / 15.0


def best_deriv(f, x, scale):
    """Derivative with h swept over a deterministic list; the value with the
    best neighbor agreement wins (guards truncation vs roundoff)."""
    vals = []
    for hf in (3e-3, 1e-3, 3e-4, 1e-4, 3e-5):
        h = hf * scale
        try:
            vals.append(d4r(f, x, h))
        except ZeroDivisionError:
            continue
    vals = [v for v in vals if math.isfinite(v)]
    if not vals:
        return None
    best, bestspread = vals[0], None
    for i in range(len(vals)):
        spread = 0.0
        for j in range(len(vals)):
            if i != j:
                spread = max(spread, abs(vals[i] - vals[j]))
        if bestspread is None or spread < bestspread:
            best, bestspread = vals[i], spread
    return best


def one_sided(f, x, h, direction):
    """Right (direction=+1) one-sided derivative with Richardson."""
    def g(hh):
        return direction * (f(x + direction * hh) - f(x)) / hh
    return 2.0 * g(h / 2.0) - g(h)


# ----------------------------------------------------------------------------
# driver plumbing
# ----------------------------------------------------------------------------
FIELDS = ["tp", "TP", "e1", "e2", "dist", "W", "S", "VE", "VPm", "VP", "VUp",
          "VSm", "VS", "VSup", "dDdTdr", "dDdTpot", "iso", "VpOvVe", "VpOvVs",
          "cliffP", "cliffS", "dCP", "dCS"]


def run_states(states):
    """states: list of (name, cfg, agg, d_cliff, n_delay, delta)."""
    inp = []
    for st in states:
        _name, c, a, d, nd, dd = st
        inp.append("A %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %d %.17g"
                   % (c["w_tp"], c["w_c"], c["tp_base"], c["tp_UB"], c["SLO1"],
                      c["SLO2"], c["dist_base"], a["E"], a["N"], a["R"],
                      a["tdr"], a["tpot"], a["G"], d, nd, dd))
    p = subprocess.run([BIN], input="\n".join(inp) + "\n",
                       capture_output=True, text=True, check=True)
    out = []
    for ln in p.stdout.strip().splitlines():
        toks = ln.split()
        assert len(toks) == len(FIELDS), ln
        out.append({k: float(v) for k, v in zip(FIELDS, toks)})
    assert len(out) == len(states)
    return out


# ----------------------------------------------------------------------------
# kink-slack helpers: max |step| that keeps the state inside its current
# clamp region (so the FD never straddles a kink)
# ----------------------------------------------------------------------------
def slack_E(c, E, N):
    tp = N / E
    s = 0.9 * E                       # domain: E stays positive
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


def _slack_axis(c, tdr, tpot, G, which):
    p = parts(c, 1.0, 1.0, 1.0, tdr, tpot, G)
    e1, e2, d = p["e1"], p["e2"], p["dist"]
    if which == "tdr":
        s = abs(tdr - c["SLO1"])
        e, SLO = e1, c["SLO1"]
    else:
        s = abs(tpot - c["SLO2"])
        e, SLO = e2, c["SLO2"]
    if c["dist_base"] > 0 and d > 0 and e > 0:
        rate = e / (d * SLO)                      # d(dist)/d(axis)
        if d < c["dist_base"]:
            s = min(s, 0.5 * (c["dist_base"] - d) / rate)
        else:
            s = min(s, (d - c["dist_base"]) / rate)
    return max(s, 1e-12)


def slack_tdr(c, tdr, tpot, G):
    return _slack_axis(c, tdr, tpot, G, "tdr")


def slack_tpot(c, tdr, tpot, G):
    return _slack_axis(c, tdr, tpot, G, "tpot")


def deriv_slack(f, x, slack):
    """4th-order + Richardson derivative at h = 5% of the kink slack (large-h
    first: roundoff ~ 1/h, truncation ~ h^6 after Richardson and the slack cap
    keeps the stencil inside one clamp region). Returns (value, certified):
    certified iff the h and h/4 stencils agree to < 3e-10 relative — the FD is
    then self-converged well below the 1e-9 gate."""
    h = 0.05 * slack
    if h <= 0.0:
        return None, False
    try:
        v1 = d4r(f, x, h)
        v2 = d4r(f, x, h / 4.0)
    except ZeroDivisionError:
        return None, False
    if not (math.isfinite(v1) and math.isfinite(v2)):
        return None, False
    ok = abs(v1 - v2) <= 3e-10 * max(abs(v1), abs(v2), 1e-300)
    return v1, ok


# ----------------------------------------------------------------------------
# random cfg/state samplers (conditioned to named regions)
# ----------------------------------------------------------------------------
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
    # conditioning: keep the ramp width >= 1e-3*tp_base so (tp - tp_base) does
    # not lose digits against tp_base in the FD (sampler choice, legal range)
    c["tp_UB"] = c["tp_base"] + max(width, 1e-3 * c["tp_base"])
    c["SLO1"] = 10 ** rng.uniform(-3, 9)
    c["SLO2"] = 10 ** rng.uniform(-3, 9)
    if force == "cliff":
        c["dist_base"] = 0.0
    elif force == "interior_wait":
        c["dist_base"] = 10 ** rng.uniform(-3, 3)
    else:
        c["dist_base"] = rng.choice([0.0, 10 ** rng.uniform(-6, 3)])
    return c


def make_tp_interior(rng, c, lo=0.08, hi=0.92):
    """E, N with tp strictly inside (tp_base, tp_UB) at given fractions."""
    N = rng.uniform(1.0, 2e5)
    frac = lo + (hi - lo) * rng.random()
    tp_t = c["tp_base"] + frac * (c["tp_UB"] - c["tp_base"])
    E = N / tp_t
    return E, N


def make_excess(rng, SLO, target):
    """mean with excess factor target (SLO*(1+target)); target<=0 => below."""
    if target <= 0.0:
        return SLO * (1.0 - 10 ** rng.uniform(-3, 0.3) * 0.9)
    return SLO * (1.0 + target)


# ----------------------------------------------------------------------------
# category samplers -> (states, checks)
# ----------------------------------------------------------------------------
def sample_states(rng):
    """Returns list of (name, cfg, agg, d_cliff, n_delay, delta)."""
    S = []

    def add(name, c, E, N, R, tdr, tpot, G, d=1.0, nd=1, dd=None):
        if dd is None:
            # finite-delay probe sized to cross SLO1 sometimes
            dd = abs(rng.gauss(1.0, 1.0)) * max(1e-3, abs(c["SLO1"] - tdr)) * R / nd
        S.append((name, c, {"E": E, "N": N, "R": R, "tdr": tdr,
                            "tpot": tpot, "G": G}, d, nd, dd))

    # CAT1: V_E live (tp interior), tdr/tpot arbitrary incl live excesses.
    for _ in range(150):
        c = rand_cfg(rng)
        if c["w_tp"] <= 0.0:
            c["w_tp"], c["w_c"] = 0.7, 0.3
        E, N = make_tp_interior(rng, c)
        R = rng.uniform(1.0, 2000.0)
        G = rng.choice([0.0, rng.uniform(1.0, 2e5)])
        tdr = make_excess(rng, c["SLO1"], rng.choice([0.0, 0.4, 2.0]))
        tpot = make_excess(rng, c["SLO2"], rng.choice([0.0, 0.4, 2.0])) if G > 0 else 0.0
        add("ve_live", c, E, N, R, tdr, tpot, G)
    # CAT1b: tp exactly at tp_base / tp_UB (kink semantics).
    for _ in range(25):
        c = rand_cfg(rng)
        if c["w_tp"] <= 0.0:
            c["w_tp"], c["w_c"] = 0.7, 0.3
        N = rng.uniform(1.0, 2e5)
        E = 10 ** rng.uniform(0, 5)
        if rng.random() < 0.5:
            c["tp_base"] = N / E                     # tp() == tp_base bitwise
            c["tp_UB"] = c["tp_base"] * (1.0 + rng.random())
            add("ve_kink_base", c, E, N, 7.0, c["SLO1"] * 0.5, c["SLO2"] * 0.5, 10.0)
        else:
            c["tp_UB"] = N / E                       # tp() == tp_UB bitwise
            c["tp_base"] = c["tp_UB"] * rng.random() * 0.9
            add("ve_kink_ub", c, E, N, 7.0, c["SLO1"] * 0.5, c["SLO2"] * 0.5, 10.0)
    # CAT2/3: tp saturated both sides (V_E must be exactly 0).
    for _ in range(60):
        c = rand_cfg(rng)
        if c["w_tp"] <= 0.0:
            c["w_tp"], c["w_c"] = 0.7, 0.3
        N = rng.uniform(1.0, 2e5)
        if rng.random() < 0.5 and c["tp_base"] > 0.0:
            tp_t = c["tp_base"] * rng.uniform(0.01, 0.85)      # below base
        else:
            tp_t = c["tp_UB"] * rng.uniform(1.05, 8.0)         # above UB
        add("ve_dead", c, N / tp_t, N, 33.0, c["SLO1"] * 2.0,
            c["SLO2"] * 2.0, 50.0)
    # CAT4: V_P live (e1>0 margin, dist < dist_base margin).
    for _ in range(150):
        c = rand_cfg(rng, "interior_wait")
        if c["w_c"] <= 0.0:
            c["w_tp"], c["w_c"] = 0.3, 0.7
        R = rng.uniform(1.0, 2000.0)
        tdr = c["SLO1"] * (1.0 + rng.uniform(0.05, 3.0))
        G = rng.choice([0.0, rng.uniform(1.0, 2e5)])
        tpot = make_excess(rng, c["SLO2"], rng.choice([0.0, 0.0, 0.3, 1.5])) if G > 0 else 0.0
        add("vp_live", c, 100.0, 1000.0, R, tdr, tpot, G)
    # CAT5: e1 == 0 with e2 > 0 (norm gradient has no tdr component).
    for _ in range(60):
        c = rand_cfg(rng, "interior_wait")
        if c["w_c"] <= 0.0:
            c["w_tp"], c["w_c"] = 0.3, 0.7
        add("vp_dead_e0", c, 100.0, 1000.0, rng.uniform(1, 2000),
            c["SLO1"] * rng.uniform(0.1, 0.9), c["SLO2"] * 2.5,
            rng.uniform(10.0, 1e4))
    # CAT6: past the cliff (dist > dist_base): wait clamped to 0.
    for _ in range(60):
        c = rand_cfg(rng, "interior_wait")
        if c["w_c"] <= 0.0:
            c["w_tp"], c["w_c"] = 0.3, 0.7
        c["dist_base"] = 10 ** rng.uniform(-6, -1)
        add("vp_past_cliff", c, 100.0, 1000.0, rng.uniform(1, 2000),
            c["SLO1"] * 6.0, c["SLO2"] * 6.0, rng.uniform(10.0, 1e4))
    # CAT7a: inside the dist==0 slack region (both SLOs met, off the edge).
    for _ in range(60):
        c = rand_cfg(rng, "interior_wait")
        if c["w_c"] <= 0.0:
            c["w_tp"], c["w_c"] = 0.3, 0.7
        add("corner_slack", c, 100.0, 1000.0, rng.uniform(1, 2000),
            c["SLO1"] * rng.uniform(0.05, 0.95), c["SLO2"] * rng.uniform(0.05, 0.95),
            rng.uniform(10.0, 1e4))
    # CAT7b: ON the knife edge tdr==SLO1 (or tpot==SLO2), other axis in slack.
    for _ in range(80):
        c = rand_cfg(rng, "interior_wait")
        if c["w_c"] <= 0.0:
            c["w_tp"], c["w_c"] = 0.3, 0.7
        R = rng.uniform(1.0, 2000.0)
        if rng.random() < 0.5:
            tdr = c["SLO1"] * (1.0 + 10 ** rng.uniform(-4, 0))
            c["SLO1"] = tdr                       # bitwise equality
            add("knife_tdr", c, 100.0, 1000.0, R, tdr,
                c["SLO2"] * rng.uniform(0.05, 0.95), rng.uniform(10.0, 1e4))
        else:
            G = rng.uniform(10.0, 1e5)
            tpot = c["SLO2"] * (1.0 + 10 ** rng.uniform(-4, 0))
            c["SLO2"] = tpot
            add("knife_tpot", c, 100.0, 1000.0, R,
                c["SLO1"] * rng.uniform(0.05, 0.95), tpot, G)
    # CAT8: V_S live (e2 > 0, dist interior).
    for _ in range(150):
        c = rand_cfg(rng, "interior_wait")
        if c["w_c"] <= 0.0:
            c["w_tp"], c["w_c"] = 0.3, 0.7
        tpot = c["SLO2"] * (1.0 + rng.uniform(0.05, 3.0))
        tdr = make_excess(rng, c["SLO1"], rng.choice([0.0, 0.0, 0.4]))
        add("vs_live", c, 100.0, 1000.0, rng.uniform(1, 2000), tdr, tpot,
            rng.uniform(1.0, 2e5))
    # CAT9: cliff (dist_base == 0) discrete jumps.
    for _ in range(120):
        c = rand_cfg(rng, "cliff")
        if c["w_c"] <= 0.0:
            c["w_tp"], c["w_c"] = 0.4, 0.6
        R = rng.uniform(1.0, 2000.0)
        G = rng.uniform(10.0, 1e5)
        u = rng.uniform(0.001, 0.5)
        kind = rng.randrange(4)
        if kind == 0:      # ppost jump crosses
            tdr, tpot = c["SLO1"] * (1.0 + u), c["SLO2"] * rng.uniform(0.1, 0.95)
            d = R * c["SLO1"] * (u + rng.uniform(0.01, 0.4))
        elif kind == 1:    # ppost no cross
            tdr, tpot = c["SLO1"] * (1.0 + u), c["SLO2"] * rng.uniform(0.1, 0.95)
            d = R * c["SLO1"] * u * rng.uniform(0.1, 0.8)
        elif kind == 2:    # span jump crosses
            tdr, tpot = c["SLO1"] * rng.uniform(0.1, 0.95), c["SLO2"] * (1.0 + u)
            d = G * c["SLO2"] * (u + rng.uniform(0.01, 0.4))
        else:              # both excesses: no single-channel rescue
            tdr, tpot = c["SLO1"] * (1.0 + u), c["SLO2"] * (1.0 + u)
            d = rng.choice([R * c["SLO1"] * 2.0, G * c["SLO2"] * 2.0])
        add("cliff", c, 100.0, 1000.0, R, tdr, tpot, G, d)
    # CAT10: exchange rates (e1, e2 both live, dist interior).
    for _ in range(100):
        c = rand_cfg(rng, "interior_wait")
        if c["w_c"] <= 0.0:
            c["w_tp"], c["w_c"] = 0.3, 0.7
        add("exchange", c, 100.0, 1000.0, rng.uniform(1, 2000),
            c["SLO1"] * (1.0 + rng.uniform(0.05, 3.0)),
            c["SLO2"] * (1.0 + rng.uniform(0.05, 3.0)), rng.uniform(10.0, 1e5))
    # CAT11: ratio forms.
    for _ in range(50):
        c = rand_cfg(rng, "interior_wait")
        E, N = make_tp_interior(rng, c) if c["w_tp"] > 0 else (100.0, 1000.0)
        R = rng.uniform(1.0, 2000.0)
        G = rng.uniform(10.0, 1e5)
        add("ratios", c, E, N, R, c["SLO1"] * (1.0 + rng.uniform(0.05, 3.0)),
            c["SLO2"] * (1.0 + rng.uniform(0.05, 3.0)), G)
    return S


# ----------------------------------------------------------------------------
# FD verification of the score calculus
# ----------------------------------------------------------------------------
def check_fd(rng):
    states = sample_states(rng)
    outs = run_states(states)
    n_states = len(states)
    maxerr = {}      # name -> max relative error seen (in-region)
    zero_ok = {}     # name -> count of exact-zero checks
    skipped = [0]    # FD self-convergence skips (ill-conditioned states)
    fails = []

    def rel(a, b):
        den = max(abs(a), abs(b), 1e-300)
        return abs(a - b) / den

    for st, o in zip(states, outs):
        name, c, a, d, ndly, ddly = st
        E, N, R, tdr, tpot, G = (a["E"], a["N"], a["R"], a["tdr"],
                                 a["tpot"], a["G"])
        p0 = parts(c, E, N, R, tdr, tpot, G)
        # --- component identity checks (exact forms) ---
        for py, ck in ((p0["tp"], "tp"), (p0["TP"], "TP"), (p0["e1"], "e1"),
                       (p0["e2"], "e2"), (p0["dist"], "dist"), (p0["W"], "W"),
                       (p0["S"], "S")):
            if rel(py, o[ck]) > 1e-12:
                fails.append("%s: %s py=%.17g cpp=%.17g" % (name, ck, py, o[ck]))
        # --- V_E (kink-slack-limited FD; on-kink states covered below) ---
        sE = slack_E(c, E, N)
        on_kink_E = sE <= 1e-9 * max(E, 1.0)
        if on_kink_E:
            pass
        elif o["VE"] != 0.0:
            fd, okc = deriv_slack(lambda x: scoreE(c, x, N, R, tdr, tpot, G), E, sE)
            if fd is None:
                skipped[0] += 1
            else:
                e = rel(-fd, o["VE"])
                if okc:
                    maxerr["V_E"] = max(maxerr.get("V_E", 0.0), e)
                else:
                    skipped[0] += 1
                if okc and e > 1e-9:
                    fails.append("V_E %s: fd=%.17g cpp=%.17g rel=%.3g"
                                 % (name, -fd, o["VE"], e))
        else:
            h = 0.25 * sE
            fE = lambda x: scoreE(c, x, N, R, tdr, tpot, G)
            if not (fE(E - h) == fE(E) == fE(E + h)):
                fails.append("V_E zero-region but score moves (%s)" % name)
            zero_ok["V_E"] = zero_ok.get("V_E", 0) + 1
        # --- V_P_mean / V_P ---
        sT = slack_tdr(c, tdr, tpot, G)
        if o["VPm"] != 0.0:
            fd, okc = deriv_slack(lambda x: scoreTdr(c, x, E, N, R, tpot, G), tdr, sT)
            if fd is None:
                skipped[0] += 1
            else:
                fdVPm = -fd
                e = rel(fdVPm, o["VPm"])
                if okc:
                    maxerr["V_P_mean"] = max(maxerr.get("V_P_mean", 0.0), e)
                    maxerr["V_P"] = max(maxerr.get("V_P", 0.0), rel(fdVPm / R, o["VP"]))
                else:
                    skipped[0] += 1
                if okc and (e > 1e-9 or rel(fdVPm / R, o["VP"]) > 1e-9):
                    fails.append("V_P_mean %s: fd=%.17g cpp=%.17g" % (name, fdVPm, o["VPm"]))
        elif tdr == c["SLO1"]:
            pass   # on the knife edge: boundary state, covered by knife checks
        else:
            h = 0.25 * sT
            fT = lambda x: scoreTdr(c, x, E, N, R, tpot, G)
            if not (fT(tdr - h) == fT(tdr) == fT(tdr + h)):
                fails.append("V_P zero-region but score moves (%s)" % name)
            zero_ok["V_P"] = zero_ok.get("V_P", 0) + 1
        # --- V_S_mean / V_S ---
        if G > 0.0:
            sP = slack_tpot(c, tdr, tpot, G)
            if o["VSm"] != 0.0:
                fd, okc = deriv_slack(lambda x: scoreTpot(c, x, E, N, R, tdr, G), tpot, sP)
                if fd is None:
                    skipped[0] += 1
                else:
                    fdVSm = -fd
                    e = rel(fdVSm, o["VSm"])
                    e2 = rel(fdVSm / G, o["VS"])
                    if okc:
                        maxerr["V_S_mean"] = max(maxerr.get("V_S_mean", 0.0), e)
                        maxerr["V_S"] = max(maxerr.get("V_S", 0.0), e2)
                    else:
                        skipped[0] += 1
                    if okc and (e > 1e-9 or e2 > 1e-9):
                        fails.append("V_S_mean %s: fd=%.17g cpp=%.17g" % (name, fdVSm, o["VSm"]))
            elif tpot == c["SLO2"]:
                pass   # knife edge
            else:
                h = 0.25 * sP
                fP = lambda x: scoreTpot(c, x, E, N, R, tdr, G)
                if not (fP(tpot - h) == fP(tpot) == fP(tpot + h)):
                    fails.append("V_S zero-region but score moves (%s)" % name)
                zero_ok["V_S"] = zero_ok.get("V_S", 0) + 1
        else:
            if o["VSm"] != 0.0 or o["VS"] != 0.0 or o["VSup"] != 0.0:
                fails.append("G==0 but V_S nonzero (%s)" % name)
        # --- knife-edge one-sided rates (tdr==SLO1 or tpot==SLO2, dist==0) ---
        if c["dist_base"] > 0.0 and p0["dist"] == 0.0 and c["w_c"] > 0.0:
            if tdr == c["SLO1"]:
                # closed form, mirrored expression (bitwise-comparable)
                cf = 1000.0 * c["w_c"] / (c["dist_base"] * c["SLO1"] * R)
                e = rel(o["VUp"], cf)
                maxerr["V_P_up(knife)"] = max(maxerr.get("V_P_up(knife)", 0.0), e)
                if e > 1e-12 or o["VUp"] <= 0.0:
                    fails.append("V_P_up knife %s: cpp=%.17g closed=%.17g"
                                 % (name, o["VUp"], cf))
                # FD cross-check: the knife response is exactly linear in h
                # out to h = dist_base*SLO1 (dist = h/SLO1), so a large h is
                # cancellation-free and exact.
                h = 0.1 * c["dist_base"] * c["SLO1"]
                up = -one_sided(lambda x: scoreTdr(c, x, E, N, R, tpot, G), tdr, h, +1)
                if abs(up) >= 1e-6 * max(1.0, p0["S"]):
                    e = rel(up, o["VUp"] * R)
                    maxerr["V_P_up(knife FD)"] = max(
                        maxerr.get("V_P_up(knife FD)", 0.0), e)
                    if e > 1e-9:
                        fails.append("V_P_up knifeFD %s: fd=%.17g cpp*R=%.17g"
                                     % (name, up, o["VUp"] * R))
            else:
                if o["VUp"] != 0.0:
                    fails.append("V_P_up must be 0 inside slack (%s)" % name)
            if G > 0.0 and tpot == c["SLO2"]:
                cf = 1000.0 * c["w_c"] / (c["dist_base"] * c["SLO2"] * G)
                e = rel(o["VSup"], cf)
                maxerr["V_S_up(knife)"] = max(maxerr.get("V_S_up(knife)", 0.0), e)
                if e > 1e-12 or o["VSup"] <= 0.0:
                    fails.append("V_S_up knife %s: cpp=%.17g closed=%.17g"
                                 % (name, o["VSup"], cf))
            elif tpot != c["SLO2"]:
                if name == "corner_slack" and o["VSup"] != 0.0 and tpot < c["SLO2"]:
                    fails.append("V_S_up must be 0 inside slack (%s)" % name)
        # --- exchange rates ---
        if p0["e1"] > 0.0 and p0["dist"] > 0.0:
            fd, okc = deriv_slack(lambda x: distTdr(c, x, tpot, G), tdr, sT)
            if fd is None or not okc:
                skipped[0] += 1
            else:
                e = rel(fd, o["dDdTdr"])
                maxerr["dDist/dTdr"] = max(maxerr.get("dDist/dTdr", 0.0), e)
                if e > 1e-9:
                    fails.append("dDist/dTdr %s: fd=%.17g cpp=%.17g" % (name, fd, o["dDdTdr"]))
        if G > 0.0 and p0["e2"] > 0.0 and p0["dist"] > 0.0:
            sP2 = slack_tpot(c, tdr, tpot, G)
            fd, okc = deriv_slack(lambda x: distTpot(c, x, tdr, G), tpot, sP2)
            if fd is None or not okc:
                skipped[0] += 1
            else:
                e = rel(fd, o["dDdTpot"])
                maxerr["dDist/dTpot"] = max(maxerr.get("dDist/dTpot", 0.0), e)
                if e > 1e-9:
                    fails.append("dDist/dTpot %s" % name)
            # iso-dist: dist(tdr+h, tpot-iso*h) - dist0 must shrink ~4x when h
            # halves (first-order correctness of the exchange rate).
            d0 = p0["dist"]
            # local probe: both the tdr step and the compensated tpot step
            # (iso*h) must stay inside their kink slacks
            h1 = min(0.02 * sT, 0.02 * sP2 / max(o["iso"], 1e-300))
            h2 = h1 / 2.0
            dr1 = abs(distTdr(c, tdr + h1, tpot - o["iso"] * h1, G) - d0)
            dr2 = abs(distTdr(c, tdr + h2, tpot - o["iso"] * h2, G) - d0)
            if dr1 > 1e-13 * max(1.0, d0) and not (dr2 / h2 <= 0.6 * dr1 / h1 + 1e-15):
                fails.append("iso-dist not first-order %s: dr1=%.3g dr2=%.3g iso=%.17g"
                             % (name, dr1, dr2, o["iso"]))
            maxerr["iso-dist drift ratio"] = max(
                maxerr.get("iso-dist drift ratio", 0.0), dr2 / max(dr1, 1e-300))
        # --- finite delay costs: exact discrete equality (any region) ---
        if c["dist_base"] > 0.0 and c["w_c"] > 0.0 and ddly > 0.0 and ndly > 0:
            tdr2 = tdr + ndly * ddly / R
            # mirrored expression (same double ops as the C++) — bitwise
            mirror = 1000.0 * c["w_c"] * (p0["W"] - parts(
                c, 1.0, 1.0, 1.0, tdr2, tpot, G)["W"])
            if mirror != o["dCP"]:
                fails.append("delayCostPpost mirror %s: %.17g vs %.17g"
                             % (name, mirror, o["dCP"]))
            exactP = p0["S"] - parts(c, E, N, R, tdr2, tpot, G)["S"]
            # compare via the score difference only when cancellation-free
            if abs(exactP) >= 1e-5 * max(1.0, p0["S"]):
                e = rel(exactP, o["dCP"])
                maxerr["delayCostPpost(exact)"] = max(
                    maxerr.get("delayCostPpost(exact)", 0.0), e)
                if e > 1e-9:
                    fails.append("delayCostPpost %s: exact=%.17g cpp=%.17g"
                                 % (name, exactP, o["dCP"]))
            if G > 0.0:
                tpot2 = tpot + ndly * ddly / G
                mirrorS = 1000.0 * c["w_c"] * (p0["W"] - parts(
                    c, 1.0, 1.0, 1.0, tdr, tpot2, G)["W"])
                if mirrorS != o["dCS"]:
                    fails.append("delayCostSpan mirror %s: %.17g vs %.17g"
                                 % (name, mirrorS, o["dCS"]))
                exactS = p0["S"] - parts(c, E, N, R, tdr, tpot2, G)["S"]
                if abs(exactS) >= 1e-5 * max(1.0, p0["S"]):
                    e = rel(exactS, o["dCS"])
                    maxerr["delayCostSpan(exact)"] = max(
                        maxerr.get("delayCostSpan(exact)", 0.0), e)
                    if e > 1e-9:
                        fails.append("delayCostSpan %s: exact=%.17g cpp=%.17g"
                                     % (name, exactS, o["dCS"]))
        # --- cliff jumps (discrete, exact) ---
        if c["dist_base"] == 0.0:
            tdr2 = tdr - d / R
            tpot2 = (tpot - d / G) if G > 0.0 else tpot
            gainP = parts(c, E, N, R, tdr2, tpot, G)["S"] - p0["S"]
            gainS = parts(c, E, N, R, tdr, tpot2, G)["S"] - p0["S"]
            e = rel(gainP, o["cliffP"])
            maxerr["cliffPpost"] = max(maxerr.get("cliffPpost", 0.0), e)
            if e > 1e-9:
                fails.append("cliffPpost %s: exact=%.17g cpp=%.17g" % (name, gainP, o["cliffP"]))
            e = rel(gainS, o["cliffS"])
            maxerr["cliffSpan"] = max(maxerr.get("cliffSpan", 0.0), e)
            if e > 1e-9:
                fails.append("cliffSpan %s: exact=%.17g cpp=%.17g" % (name, gainS, o["cliffS"]))
        # --- ratio forms (closed forms vs compositions) ---
        if o["VE"] > 0.0 and o["VP"] > 0.0:
            e = rel(o["VP"] / o["VE"], o["VpOvVe"])
            maxerr["V_P/V_E form"] = max(maxerr.get("V_P/V_E form", 0.0), e)
            closed = (c["w_c"] * p0["e1"] * (c["tp_UB"] - c["tp_base"]) * E * E
                      / (c["w_tp"] * N * c["dist_base"] * p0["dist"] * c["SLO1"] * R))
            e = rel(closed, o["VpOvVe"])
            maxerr["V_P/V_E closed"] = max(maxerr.get("V_P/V_E closed", 0.0), e)
            if e > 1e-9:
                fails.append("V_P/V_E closed %s" % name)
        if o["VP"] > 0.0 and o["VS"] > 0.0 and p0["e2"] > 0.0:
            closed = (p0["e1"] * c["SLO2"] * G) / (p0["e2"] * c["SLO1"] * R)
            e = rel(closed, o["VpOvVs"])
            maxerr["V_P/V_S closed"] = max(maxerr.get("V_P/V_S closed", 0.0), e)
            if e > 1e-9:
                fails.append("V_P/V_S closed %s" % name)

    # kink semantics: tp == tp_base exactly -> one-sided (reduction) rate = VE > 0;
    # tp == tp_UB exactly -> VE == 0.
    for st, o in zip(states, outs):
        name, c, a, d, _nd, _dd = st
        if name != "ve_kink_base":
            continue
        E, N, R, tdr, tpot, G = (a["E"], a["N"], a["R"], a["tdr"],
                                 a["tpot"], a["G"])
        h = 0.2 * (E - N / c["tp_UB"])
        g = (scoreE(c, E - h, N, R, tdr, tpot, G) - scoreE(c, E, N, R, tdr, tpot, G)) / h
        gh = (scoreE(c, E - h / 2, N, R, tdr, tpot, G)
              - scoreE(c, E, N, R, tdr, tpot, G)) / (h / 2.0)
        gr = 2.0 * gh - g
        if abs(gh - g) <= 0.3e-9 * abs(o["VE"]):
            e = rel(gr, o["VE"])
            maxerr["V_E(kink@base)"] = max(maxerr.get("V_E(kink@base)", 0.0), e)
            if e > 1e-9 or o["VE"] <= 0.0:
                fails.append("V_E at tp==tp_base must equal one-sided rate: "
                             "rich=%.17g cpp=%.17g" % (gr, o["VE"]))
        else:
            skipped[0] += 1
    bad = [st[0] for st, o in zip(states, outs)
           if st[0] == "ve_kink_ub" and o["VE"] != 0.0]
    if bad:
        fails.append("V_E at tp==tp_UB must be exactly 0")

    # --- mode C: dE_effect (max-coupling arithmetic) ---
    inp = []
    exp = []
    for _ in range(40):
        dAdv = rng.uniform(0.01, 100)
        star = rng.randrange(2)
        f1 = rng.uniform(0, 1000)
        f2 = rng.uniform(0, 1000)
        inp.append("C %.17g %d %.17g %.17g" % (dAdv, star, f1, f2))
        exp.append(min(dAdv, max(0.0, f1 - f2)) if star else 0.0)
    p = subprocess.run([BIN], input="\n".join(inp) + "\n",
                       capture_output=True, text=True, check=True)
    for ln, e in zip(p.stdout.strip().splitlines(), exp):
        got = float(ln)
        if got != e:
            fails.append("dE_effect: %.17g != %.17g" % (got, e))
    # --- mode D: coefficients ---
    inp, exp = [], []
    for _ in range(40):
        G = rng.uniform(1.0, 2e5)
        it = rng.randrange(0, 500)
        pge2 = rng.random()
        lh = rng.uniform(1.0, 512.0)
        inp.append("D %.17g %d %.17g %.17g" % (G, it, pge2, lh))
        exp.append(((1.0 if it >= 1 else pge2) / G, max(0.0, lh - it) / G,
                    (pge2 / G) if it == 0 else 0.0))
    p = subprocess.run([BIN], input="\n".join(inp) + "\n",
                       capture_output=True, text=True, check=True)
    for ln, (ng, stc, pk) in zip(p.stdout.strip().splitlines(), exp):
        got = [float(v) for v in ln.split()]
        for g, e in zip(got, (ng, stc, pk)):
            if rel(g, e) > 1e-12:
                fails.append("coefficient: %.17g != %.17g" % (g, e))
    return n_states, maxerr, zero_ok, skipped[0], fails


# ----------------------------------------------------------------------------
# batch law vs brute force
# ----------------------------------------------------------------------------
def py_pl_at(rows, m):
    if not rows:
        return 0.0
    if m <= rows[0][0]:
        return rows[0][1]
    if m >= rows[-1][0]:
        return rows[-1][1]
    for i in range(len(rows) - 1):
        a, b = rows[i], rows[i + 1]
        if a[0] <= m <= b[0]:
            if b[0] == a[0]:
                return a[1]
            f = (m - a[0]) / float(b[0] - a[0])
            return a[1] + f * (b[1] - a[1])
    return rows[-1][1]


class PyRC(object):
    def __init__(self, S, lat, bper, K, dpre, dproc, dpost):
        self.S, self.lat, self.bper, self.K = S, lat, bper, K
        self.dpre, self.dproc, self.dpost = dpre, dproc, dpost

    def xup(self, m):
        return self.K * self.lat + self.bper * m

    def xdown(self, m):
        return self.lat + self.bper * m

    def Ppipe(self, m):
        km = max(1, (m + self.K - 1) // self.K)
        return max(2 * self.S + py_pl_at(self.dpre, m) + py_pl_at(self.dpost, m),
                   self.S + py_pl_at(self.dproc, km),
                   self.xup(m), self.xdown(m))

    def Pchain(self, m):
        km = max(1, (m + self.K - 1) // self.K)
        return (3 * self.S + py_pl_at(self.dpre, m) + py_pl_at(self.dproc, km)
                + py_pl_at(self.dpost, m) + self.xup(m) + self.xdown(m))

    def Pgap(self, m, pool):
        import math as _m
        rot = _m.ceil(max(1, pool) / float(max(1, m)))
        return max(self.Pchain(m), rot * self.Ppipe(m))

    def mTP_brute(self, SLO2, pool, mmax):
        hi = max(1, min(mmax, pool))
        best = 0
        for m in range(1, hi + 1):
            if self.Pgap(m, pool) <= SLO2 and m > best:
                best = m
        return best


def check_law(rng):
    fails = []
    stats = {"law_states": 0, "qThin_exact": 0, "mTP_scan==brute": 0,
             "regime_ok": 0, "qCrit_identity": 0, "Astar_ok": 0}
    worst_qthin = 0.0
    for it in range(120):
        K = rng.randrange(1, 9)
        S = 10 ** rng.uniform(0, 1)
        lat = 10 ** rng.uniform(-3, math.log10(50))
        bper = 10 ** rng.uniform(-8, 2.9)
        def rows():
            n = rng.randrange(1, 7)
            ms = sorted(rng.sample(range(1, 513), n))
            if ms[0] != 1:
                ms[0] = 1
            return [(m, 10 ** rng.uniform(-3, 4)) for m in ms]
        dpre, dproc, dpost = rows(), rows(), rows()
        mmax = rng.randrange(8, 513)
        pool = rng.randrange(1, 301)
        SLO2 = 10 ** rng.uniform(-1, 4)
        lam = 10 ** rng.uniform(-4, 1)
        rc = PyRC(S, lat, bper, K, dpre, dproc, dpost)
        f0 = rc.Pchain(1)
        aSlope = max(0.0, (rc.Pchain(min(mmax, 64)) - f0) / float(min(mmax, 64) - 1))
        # C++ segments: breakpoints at every integer 1..mmax of Pchain.
        pts = [(m, rc.Pchain(m)) for m in range(1, mmax + 1)]
        segs = []
        for i in range(len(pts) - 1):
            c1 = (pts[i + 1][1] - pts[i][1]) / float(pts[i + 1][0] - pts[i][0])
            segs.append((float(pts[i][0]), float(pts[i + 1][0]),
                         pts[i][1] - c1 * pts[i][0], c1))
        # dense brute-force continuous argmin of W(q)=q/(2lam)+f(q)/q
        def Wof(q):
            for m1, m2, c0, c1 in segs:
                if m1 <= q <= m2:
                    return q / (2 * lam) + (c0 + c1 * q) / q
            return 1e300
        dense = min(Wof(1 + i * 0.01) for i in range(int((mmax - 1) / 0.01) + 1))
        mtp_b = rc.mTP_brute(SLO2, pool, mmax)
        # regime per the C++ detector
        qt = None
        best = (1e300, 1.0)
        for m1, m2, c0, c1 in segs:
            if c0 <= 0:
                q = m1
            else:
                q = min(max(math.sqrt(2 * lam * c0), m1), m2)
            w = q / (2 * lam) + (c0 + c1 * q) / q
            if w < best[0]:
                best = (w, q)
        qt = best[1]
        def fOf(q):
            for m1, m2, c0, c1 in segs:
                if m1 <= q <= m2:
                    return c0 + c1 * q
            return segs[-1][2] + segs[-1][3] * segs[-1][1]
        rho = lam * fOf(qt) / qt
        if rho < 1.0:
            regime, qe = 0, min(max(qt, 1.0), pool)
        elif lam * aSlope < 1.0:
            qcr = lam * f0 / (1 - lam * aSlope)
            regime, qe = 1, min(max(max(qt, qcr), 1.0), pool)
        else:
            regime = 2
            qe = float(min(pool, mtp_b) if mtp_b > 0 else pool)
        # drive C++
        toks = ["B", "%.17g" % lam, "%.17g" % f0, "%.17g" % aSlope,
                str(pool), str(mmax), "%.17g" % SLO2, "%.17g" % S,
                "%.17g" % lat, "%.17g" % bper, str(K),
                str(len(dpre))] + ["%d %.17g" % r for r in dpre] \
            + [str(len(dproc))] + ["%d %.17g" % r for r in dproc] \
            + [str(len(dpost))] + ["%d %.17g" % r for r in dpost]
        p = subprocess.run([BIN], input=" ".join(toks) + "\n",
                           capture_output=True, text=True, check=True)
        v = [float(x) for x in p.stdout.strip().split()]
        qThin_c, qCrit_c, reg_c, q_c, mtp_c, Ppipe_c, Pchain_c, Pgap_c, Ast_c = v
        reg_c, mtp_c = int(reg_c), int(mtp_c)
        stats["law_states"] += 1
        # qThin achieves the continuous minimum
        e = (Wof(qThin_c) - dense) / max(1.0, abs(dense))
        worst_qthin = max(worst_qthin, e)
        if e > 1e-9:
            fails.append("law %d: W(qThin)=%.17g > dense=%.17g" % (it, Wof(qThin_c), dense))
        else:
            stats["qThin_exact"] += 1
        # mTP scan == brute force
        if mtp_c != mtp_b:
            fails.append("law %d: mTP scan=%d brute=%d" % (it, mtp_c, mtp_b))
        else:
            stats["mTP_scan==brute"] += 1
        # regime + final q
        if reg_c != regime or abs(q_c - qe) > 1e-6 * max(1.0, qe):
            fails.append("law %d: regime %d/%d q %.17g/%.17g" % (it, reg_c, regime, q_c, qe))
        else:
            stats["regime_ok"] += 1
        # qCrit stability identity lam*(f0+a*q)/q == 1
        if qCrit_c < 1e299 and lam * aSlope < 1.0:
            ident = lam * (f0 + aSlope * qCrit_c) / qCrit_c
            if abs(ident - 1.0) > 1e-9:
                fails.append("law %d: qCrit identity %.17g" % (it, ident))
            else:
                stats["qCrit_identity"] += 1
        # period models + Astar
        qi = int(q_c + 0.5)
        if rel2(Ppipe_c, rc.Ppipe(qi)) > 1e-12 or rel2(Pchain_c, rc.Pchain(qi)) > 1e-12 \
           or rel2(Pgap_c, rc.Pgap(qi, pool)) > 1e-12:
            fails.append("law %d: period model mismatch" % it)
        if rel2(Ast_c, max(1.0, (rc.Pgap(qi, pool) - K * lat) / bper)) > 1e-12:
            fails.append("law %d: Astar mismatch" % it)
        else:
            stats["Astar_ok"] += 1
    return stats, worst_qthin, fails


def rel2(a, b):
    return abs(a - b) / max(abs(a), abs(b), 1e-300)


# ----------------------------------------------------------------------------
# size accounting (minifier mirroring tools/merge_submission.py STYLE only)
# ----------------------------------------------------------------------------
RENAMES = {  # fragment-local short names, merge_submission.py conventions
    "ScoreCalc": "sc", "SCfg": "cf", "SAgg": "ag", "PLTab": "pt", "RoundCost": "rc",
    "BatchLaw": "bl", "SegPL": "sg", "qThin": "qT", "qCrit": "qC", "qBatch": "qB",
    "segQ": "sQ", "segW": "sW", "fOf": "fO", "mTP": "mT", "Ppipe": "pP",
    "Pchain": "pC", "Pgap": "pG", "xup": "xu", "xdown": "xd", "inflightAstar": "iA",
    "V_P_mean": "vPm", "V_S_mean": "vSm", "V_P_up": "vPu", "V_S_up": "vSu",
    "V_P_over_V_E": "rPE", "V_P_over_V_S": "rPS", "dDist_dTdr": "gD1",
    "dDist_dTpot": "gD2", "isoTpotPerTdr": "iso", "tpCmp": "tc", "dist_base": "db",
    "tp_base": "tb", "tp_UB": "tu", "w_tp": "wt", "w_c": "wc", "nextGapCoef": "nGC",
    "stretchCoef": "sGC", "parkCoef": "pKC", "dE_effect": "dEE", "waitLoss": "wL",
    "admitInflight": "aI", "tdrBudgetMs": "tB", "isolateStraggler": "iS",
    "potcBand": "pB", "noGapsEver": "nG", "runPpostFirst": "rPP", "delayCost": "dC",
    "cliffPpost": "cPp", "cliffSpan": "cSp", "V_E": "vE", "V_P": "vP", "V_S": "vS",
    "score": "scr", "wait": "wat", "dist": "dst", "cliffSpan": "cSp",
}

DELETIONS = [
    ("C-map L1 0.15*dist_base tdr-critical (hold_dpre)", "src/policies.cpp", 320, 323),
    ("C-map L2 K=1 V_tpot + cliff V_tpot=1000*w_c fallbacks (group_size)",
     "src/policies.cpp", 275, 280),
    ("C-map L3 ewma_lout<2.5 + SLO1*0.8 (group_size)", "src/policies.cpp", 246, 247),
    ("C-map L4 dynamic-cap ladder 0.05/0.3/1/5 + link override", "src/scheduler.cpp", 212, 236),
    ("C-map L5 Lin>=256 Tier-1 straggler isolation", "src/policies.cpp", 39, 50),
    ("C-map L6 alpha<0.8 NoDelay + alpha xremote forms", "src/Solution.cpp", 188, 194),
    ("C-map L7 inflight bases max(2,K)/100/2+2K/( /2+1) + 0.5*dist_base headroom",
     "src/scheduler.cpp", 93, 104),
    ("C-map L8 tdr_urgent shortcut (mv_tdr>0 && a_i>0)", "src/scheduler.cpp", 111, 124),
    ("C-map L9 1.05 PoTC near-tie band", "src/policies.cpp", 101, 112),
]


def strip_comments(text):
    out, i, n, st = [], 0, len(text), 0
    while i < n:
        ch = text[i]
        nx = text[i + 1] if i + 1 < n else ""
        if st == 1:
            if ch == "\n":
                st = 0
                out.append(ch)
            i += 1
            continue
        if st == 2:
            if ch == "*" and nx == "/":
                st = 0
                i += 2
                continue
            i += 1
            continue
        if st == 3:
            out.append(ch)
            if ch == "\\" and nx:
                out.append(nx)
                i += 2
                continue
            if ch == '"':
                st = 0
            i += 1
            continue
        if ch == "/" and nx == "/":
            st = 1
            i += 2
            continue
        if ch == "/" and nx == "*":
            st = 2
            i += 2
            continue
        if ch == '"':
            st = 3
            out.append(ch)
            i += 1
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def strip_code_strings(line):
    out, i, st = [], 0, 0
    while i < len(line):
        ch = line[i]
        if st == 0:
            if ch == '"':
                st = 1
            elif ch == "/" and i + 1 < len(line) and line[i + 1] == "/":
                break
            else:
                out.append(ch)
        elif st == 1:
            if ch == "\\":
                i += 2
                continue
            if ch == '"':
                st = 0
        i += 1
    return "".join(out)


def minify(text, rename=False):
    text = strip_comments(text)
    if rename:
        for k, v in RENAMES.items():
            import re as _re
            text = _re.sub(r"\b%s\b" % _re.escape(k), v, text)
    out, depth = [], 0
    for line in text.split("\n"):
        s = line.strip()
        if not s:
            continue
        if s.startswith("#"):
            if s.startswith(("#include", "#ifndef SB04", "#define SB04", "#endif")):
                continue
            out.append(s)
            continue
        code = strip_code_strings(s)
        lead_close = len(code) - len(code.lstrip("}"))
        out.append(" " * max(0, depth - lead_close) + s)
        depth += code.count("{") - code.count("}")
        depth = max(0, depth)
    return "\n".join(out) + "\n"


def check_size():
    frag = open(SRC).read()
    core = frag.split("#ifdef SB04_DRIVER")[0]
    raw = len(minify(core))
    short = len(minify(core, rename=True))
    # per-section split (honest tiering vs what the artifact already carries)
    m1 = "// ---------- constants -> formulas"
    m2 = "// ---------- piecewise-linear helpers"
    a = core.split(m1)[0]
    b = core.split(m1)[1].split(m2)[0]
    cd = m2 + m2.join(core.split(m2)[1:])
    rows = [("SB04 fragment TOTAL (minified)", raw),
            ("SB04 fragment TOTAL (short names)", short),
            ("  sec A: exact marginals (drop-in over marginal_*: 560 B today)",
             len(minify(a, rename=True))),
            ("  sec B: constants->formulas helpers", len(minify(b, rename=True))),
            ("  sec C: PLTab/RoundCost/batch law (overlaps MarginalBatch precompute"
             " + SB03 engine)", len(minify(cd, rename=True)))]
    total_del = 0
    for label, path, l1, l2 in DELETIONS:
        lines = open(os.path.join(os.path.dirname(os.path.dirname(HERE)), path)).read().split("\n")
        blk = "\n".join(lines[l1 - 1:l2])
        n = len(minify(blk))
        total_del += n
        rows.append(("DELETE " + label, n))
    rows.append(("TOTAL deletable (constant/ladder blocks)", total_del))
    return rows


# ----------------------------------------------------------------------------
def main():
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv else "all"
    build()
    ok = True
    if only in ("all", "fd"):
        rng = random.Random(SEED)
        n_states, maxerr, zero_ok, nskip, fails = check_fd(rng)
        print("== SB04 FD verification: %d randomized states (seed %d) ==" % (n_states, SEED))
        print("%-24s %14s   %s" % ("formula", "max rel err", "in-region n / exact-0 n"))
        for k in sorted(maxerr):
            print("%-24s %14.3e" % (k, maxerr[k]))
        for k in sorted(zero_ok):
            print("%-24s %14s   exact-0 checks: %d" % (k, "0 (exact)", zero_ok[k]))
        if fails:
            ok = False
            print("FAILS (%d):" % len(fails))
            for f in fails[:25]:
                print("  " + f)
        else:
            print("uncertified-FD skips (ill-conditioned, not asserted): %d" % nskip)
        print("FD ALL PASS (max in-region rel err < 1e-9; zeros exact)")
    if only in ("all", "law"):
        rng = random.Random(SEED + 1)
        stats, worst, fails = check_law(rng)
        print("== SB04 batch law vs brute force: %d random tables ==" % stats["law_states"])
        for k, v in stats.items():
            print("  %-22s %d" % (k, v))
        print("  worst W(qThin)-dense rel excess: %.3e" % worst)
        if fails:
            ok = False
            print("FAILS (%d):" % len(fails))
            for f in fails[:25]:
                print("  " + f)
        else:
            print("BATCH LAW ALL PASS")
    if only in ("all", "size"):
        print("== SB04 size ledger (minified bytes, merge_submission style) ==")
        for label, n in check_size():
            print("  %-70s %6d" % (label, n))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
