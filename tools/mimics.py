#!/usr/bin/env python3
"""Mimic generators for the CF-low profiles, calibrated against the D-142 CF
verdicts (D-143). Each family reproduces a class the old mimics missed:

  M6/M6B:  CF #5/#6 starve class - tight tp window (tp_UB=20 vs achieved tp
           ~3-4), w_tp=0.90, huge dist slack (headroom) -> the D_PRE throttle
           (eff_cap clamp / max_inflight=2 gate) suppresses tp; relaxing it
           gains +6.3/+0.2 pts on CF, +9 locally. The OLD m6 profile (dense
           superlinear decode wall) could not engage the mechanism at all
           (q4(4096)=0.103ms -> max_inflight=100, and C/mk=4.96 cloud-sat).
  M15:     CF #15 class - the m17c slow-fat link (bpt=175000, bw=0.04) with
           Lout==1 (pure TDR): cross-cloud D_PRE combine stalls the prefill
           pipeline (~7s waves) with zero decode amortization to show for it;
           the relaxation LOSES there (-0.595 CF / -0.531 local). Guards the
           other side of the same lever.
  M17:     CF #17 exchange class - TDR-excess + TPOT-excess on the slow-fat
           link, dist<dist_base: the throttle gate must NOT fire.
  M9:      CF #9/#10/#16/#18/#21 class - Lout==1, w_c-dominant, TDR-bound.
Uses the same emit() format as adversarial_generator.py (interactor-compatible).
"""
import os


def emit(path, K, S, lat, bw, bpt, layers, slo, table, arrivals):
    lines = []
    lines.append(f"{K} {S} {lat} {bw} {bpt} {layers}")
    lines.append(" ".join(str(x) for x in slo))
    lines.append(str(len(table)))
    for r in table:
        lines.append(" ".join(str(x) for x in r))
    lines.append(str(len(arrivals)))
    for (t, lin, lo) in arrivals:
        lines.append(f"{t} {lin} {lo}")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def dense_superlinear(rows=513, maxb=4096, coef=1.0, cheappre=False):
    bs = [1] + [round(maxb * i / (rows - 1)) for i in range(1, rows)]
    bs = sorted(set(bs))
    t = []
    for b in bs:
        dec = round(coef * b ** 1.5) + 1
        if cheappre:
            pre = 0.1 + 0.01 * b
        else:
            pre = max(1, round(b * 0.02)) + 1
        t.append([b, pre, pre, pre, pre, dec, pre])
    return t


def dense_linear(rows=513, maxb=4096, coef=1.0):
    bs = [1] + [round(maxb * i / (rows - 1)) for i in range(1, rows)]
    bs = sorted(set(bs))
    t = []
    for b in bs:
        dec = round(coef * b) + 1
        pre = max(1, round(b * 0.02)) + 1
        t.append([b, pre, pre, pre, pre, dec, pre])
    return t


def sublinear():
    BS = [1, 2, 4, 8, 16, 32, 64]
    pre = [1, 2, 4, 8, 16, 32, 64]
    dec = [2, 4, 8, 16, 32, 64, 128]
    return [[BS[i], pre[i], pre[i], pre[i], pre[i], dec[i], pre[i]] for i in range(7)]


def starve_table(rows=129, maxb=1024, coef=0.01):
    # flat-ish table for the starve class: pre == dec == 0.01*b + 1, so bigger
    # decode waves amortize S without cloud-saturating (C/mk stays ~0.5).
    bs = [1] + [round(maxb * i / (rows - 1)) for i in range(1, rows)]
    bs = sorted(set(bs))
    t = []
    for b in bs:
        v = max(1, round(coef * b)) + 1
        t.append([b, v, v, v, v, v, v])
    return t


def m6(path, R=3000, lout=32, lin=4):
    # CF #5/#6 starve class, fast-link variant: q4(4096)=0.103ms ->
    # max_inflight=100, so the ONLY lever is the eff_cap clamp removal
    # (dist_headroom). D-142 A/B: Build #2 +9.36 over D-140.
    emit(path, 8, 2.0, 0.1, 10.0, 1, 8,
         [1e9, 1e9, 20.0, 0.0, 1e9, 0.9, 0.1],
         starve_table(),
         [(0.0, lin, lout)] * R)


def m6b(path, R=800, lout=32, lin=4):
    # variant: slow link -> q4(4096)=1.003ms -> max_inflight=2 engaged; the
    # up_full/D_PRE gate relaxation is the lever. D-142 A/B: +9.27.
    emit(path, 8, 2.0, 1.0, 1.0, 1, 8,
         [1e9, 1e9, 20.0, 0.0, 1e9, 0.9, 0.1],
         starve_table(),
         [(0.0, lin, lout)] * R)


def m17(path, R=300, lout=24, lin=4):
    # TDR-excess + TPOT-excess, dist<dist_base, decode waits behind prefill
    emit(path, 8, 1.0, 1.0, 1.0, 1, 10,
         [50.0, 10.0, 1000.0, 0.0, 1200.0, 0.65, 0.35],
         sublinear(),
         [(float(i) * 0.5, lin, lout) for i in range(R)])


def m17b(path, R=300, lout=24, lin=4):
    # variant: slow link -> prefill pipeline saturates the UP link
    emit(path, 8, 1.0, 0.2, 1.0, 1, 10,
         [50.0, 10.0, 1000.0, 0.0, 1200.0, 0.65, 0.35],
         sublinear(),
         [(float(i) * 0.5, lin, lout) for i in range(R)])


def cheappre_sublinear():
    # cheap prefill (0.1+0.01*b) + sublinear decode columns: the local never
    # bottlenecks the prefill; the decode waves dominate the link.
    BS = [1, 2, 4, 8, 16, 32, 64]
    pre = [0.11, 0.12, 0.14, 0.18, 0.26, 0.42, 0.74]
    dec = [2, 4, 8, 16, 32, 64, 128]
    return [[BS[i], pre[i], pre[i], pre[i], pre[i], dec[i], pre[i]] for i in range(7)]


def m17c(path, R=200, lout=64, lin=4):
    # slow-fat-link exchange probe (NOT a #17 delta predictor): cheap local
    # prefill, decode waves (200-member UP = 9s on this link) monopolize the
    # FIFO -> late prefills queue behind decode traffic -> TDR ~= 10^4-10^5 ms
    # (the CF 2.5-hour class). D-142 A/B: Build #2 -0.531 local, but CF #17
    # measured +0.056 -- the true #17 profile differs; use this only as a
    # link-saturation cost probe, not a direction match.
    emit(path, 8, 1.0, 1.0, 0.04, 175000, 10,
         [2000.0, 1e9, 1000.0, 0.0, 34.0, 0.65, 0.35],
         cheappre_sublinear(),
         [(float(i) * 50.0, lin, lout) for i in range(R)])


def m17d(path, R=200, lout=64, lin=4):
    # TPOT-excess counterpart on the same slow-fat link (the #14-ish tpot
    # regime): SLO1 loose, SLO2 strict -> dist ~= e_tpot. D-142 A/B: -0.323
    # local; CF #14 was +0.022 -- direction probe only.
    emit(path, 8, 1.0, 1.0, 0.04, 175000, 10,
         [1e9, 100.0, 1000.0, 0.0, 76.0, 0.65, 0.35],
         cheappre_sublinear(),
         [(float(i) * 50.0, lin, lout) for i in range(R)])


def m15(path, R=200, lin=4, lout=1):
    # CF #15 class: the m17c slow-fat link but Lout==1 -> pure TDR; cross-cloud
    # D_PRE combine stalls the prefill pipeline (200-member UP ~ 7s on this
    # link) with no decode amortization. Weights = CF #15's measured
    # w_tp=0.45 < w_c=0.55 (tp NOT dominant -> Build #2b restores the PPRE
    # cap here). D-142 A/B: Build #2 -0.531 (reproduces CF #15 -0.595).
    emit(path, 8, 1.0, 1.0, 0.04, 175000, 10,
         [2000.0, 1e9, 1000.0, 0.0, 34.0, 0.45, 0.55],
         starve_table(rows=65, maxb=256),
         [(float(i) * 50.0, lin, lout) for i in range(R)])


def m9(path, R=800, lin=8, bw=0.1, bpt=100):
    # Lout==1 pure-prefill TDR-bound; c-dominant
    emit(path, 8, 1.0, bw, bpt, 10, 10,
         [50.0, 10.0, 1000.0, 0.0, 117.0, 0.05, 0.95],
         sublinear(),
         [(float(i) * 0.1, lin, 1) for i in range(R)])


def m9b(path, R=800, lin=8, bw=0.05, bpt=100):
    # variant: link even slower -> TDR explodes (the #10 class)
    emit(path, 8, 1.0, bw, bpt, 10, 10,
         [50.0, 10.0, 1000.0, 0.0, 117.0, 0.15, 0.85],
         sublinear(),
         [(float(i) * 0.1, lin, 1) for i in range(R)])


def m9c(path, R=1500, lin=4, bw=0.1, bpt=100):
    # variant: burst arrivals (all at t=0) -> the TDR blowup under load
    emit(path, 8, 1.0, bw, bpt, 10, 10,
         [50.0, 10.0, 1000.0, 0.0, 117.0, 0.05, 0.95],
         sublinear(),
         [(0.0, lin, 1)] * R)


FAMILIES = {
    "m6": m6, "m6b": m6b, "m15": m15, "m17": m17, "m17b": m17b, "m17c": m17c, "m17d": m17d,
    "m9": m9, "m9b": m9b, "m9c": m9c,
}


def main():
    out = os.path.join(os.path.dirname(__file__), "..", "cases", "mimics")
    for name, fn in FAMILIES.items():
        p = os.path.join(out, name + ".txt")
        fn(p)
        print(f"wrote {p}")


if __name__ == "__main__":
    main()