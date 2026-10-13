#!/usr/bin/env python3
"""Adversarial case generator for the ICPC 2026 solver.

Generates mathematically hostile cases along 5 axes designed to break the
fixed scheduling policies. Each axis targets a specific failure mode of the
greedy/work-conserving baseline.

Output: cases/adv/adv_<axis>.txt  (consumed by tools/adversarial_runner.py)

Case file format (mirrors tools/interactor.py Case reader):
    K S latency bandwidth bytes_per_token num_layers
    SLO1 SLO2 tp_UB tp_base dist_base w_tp w_c
    N
    N x 7 rows: batch prefill_pre prefill_proc prefill_post decode_pre decode_proc decode_post
    R
    R lines: t lin lout
"""
import argparse
import os

OUT_DIR = os.path.join("cases", "adv")
BS = [1, 2, 4, 8, 16, 32, 64]


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


def table_sublinear():
    pre = [1, 2, 4, 8, 16, 32, 64]
    dec = [2, 4, 8, 16, 32, 64, 128]  # linear -> per-request cost constant (sublinear)
    return [[BS[i], pre[i], pre[i], pre[i], pre[i], dec[i], pre[i]] for i in range(7)]


def table_superlinear():
    # decode_proc scales as b^1.5 -> strictly convex (superlinear). Coefficient is
    # large so decode compute dominates the local D PRE/D POST overhead, making the
    # superlinear trap actually bite: GreedyAllBatch batches everything together ->
    # decode time explodes -> score ~0; MarginalBatch must stop at m=1.
    pre = [1, 2, 4, 8, 16, 32, 64]
    dec = [round(100.0 * b ** 1.5) for b in BS]
    return [[BS[i], pre[i], pre[i], pre[i], pre[i], dec[i], pre[i]] for i in range(7)]


def table_cliff():
    # min_TDR = 3*S + pre_pre(1)+pre_proc(1)+pre_post(1) + 2*transfer(1).
    # S=1, transfer(1)~=1 -> 3 + 1+1+1 + 2*1 = 8.0 > SLO1=5.0 => TDR cliff.
    pre = [1, 1, 1, 1, 1, 1, 1]
    dec = [2, 4, 8, 16, 32, 64, 128]
    return [[BS[i], pre[i], pre[i], pre[i], pre[i], dec[i], pre[i]] for i in range(7)]


def gen_transfer(out):
    # Transfer-Saturation Hell: latency=50, bandwidth=0.001, bytes_per_token=1e6.
    # UP/DOWN transfers take seconds. V9_XREMOTE caps cross-cloud D PRE at 3 when
    # latency >= XREMOTE_LAT_THRESH (50). We verify the solver refuses to span >3
    # clouds in a single D PRE group.
    K, S, lat, bw, bpt, layers = 8, 1.0, 50.0, 0.001, 1000000, 8
    slo = [50.0, 10.0, 1000.0, 0.0, 5.0, 0.5, 0.5]
    R = 30
    arrivals = [(0.0, 1, 3)] * R  # Lin=1 keeps transfers shorter; Lout=3 exercises decode
    emit(out, K, S, lat, bw, bpt, layers, slo, table_sublinear(), arrivals)


def gen_micro(out):
    # The Microsecond Burst: R=2000 requests, all ARR at t=1e-9. Floods the local
    # computer and the ready queues. Stresses starvation (P PRE must not starve) and
    # any O(R) scan per frame (TLE risk).
    K, S, lat, bw, bpt, layers = 8, 1.0, 1.0, 1.0, 1, 5
    slo = [50.0, 10.0, 1000.0, 0.0, 5.0, 0.5, 0.5]
    R = 2000
    arrivals = [(1e-9, 4, 4)] * R
    emit(out, K, S, lat, bw, bpt, layers, slo, table_sublinear(), arrivals)


def gen_super(out):
    # The Superlinear Trap: decode_proc ~ b^1.5. GreedyAllBatch batches everything
    # together -> decode time explodes -> score ~0. MarginalBatch must stop early.
    # Transfers are made negligible (tiny latency / huge bw) so decode COMPUTE is the
    # bottleneck and the superlinear trap actually decides the score.
    K, S, lat, bw, bpt, layers = 8, 1.0, 0.001, 1000.0, 1, 5
    slo = [50.0, 10.0, 1000.0, 0.0, 5.0, 0.5, 0.5]
    R = 100
    arrivals = [(0.0, 4, 4)] * R  # Lout=4; superlinear decode dominates local overhead
    emit(out, K, S, lat, bw, bpt, layers, slo, table_superlinear(), arrivals)


def gen_degenerate(out):
    # The Degenerate Dimensions: K=1, num_layers=1. No divide-by-zero in chunk
    # logic; cloud-balancing must not crash with a single cloud.
    K, S, lat, bw, bpt, layers = 1, 1.0, 1.0, 1.0, 1, 1
    slo = [50.0, 10.0, 1000.0, 0.0, 5.0, 0.5, 0.5]
    R = 10
    arrivals = [(0.0, 4, 4)] * R
    emit(out, K, S, lat, bw, bpt, layers, slo, table_sublinear(), arrivals)


def gen_cliff(out):
    # The distbase=0 Cliff: dist_base=0, SLO1=5.0, S=1.0, min_TDR=8.0 (cannot meet).
    # The solver must detect at startup that the waiting component is unattainable
    # and switch to pure throughput (max batching) instead of fruitlessly
    # minimizing TDR.
    K, S, lat, bw, bpt, layers = 8, 1.0, 1.0, 1.0, 1, 5
    slo = [5.0, 10.0, 1000.0, 0.0, 0.0, 0.5, 0.5]
    R = 200
    arrivals = [(0.0, 4, 4)] * R
    emit(out, K, S, lat, bw, bpt, layers, slo, table_cliff(), arrivals)


def table_one_row(vals=(1.0, 1.0, 1.0, 1.0, 1.0, 1.0)):
    # Single-row table: forces clamping (no interpolation neighbour). Every
    # duration identical -> degenerate but legal. Used by the Degenerate Void.
    pp, pr, po, dp, dpr, dpo = vals
    return [[1, pp, pr, po, dp, dpr, dpo]]


def table_falsehope():
    # E5: prefill_pre=2.0, prefill_proc=2.0, prefill_post=2.0 (per spec).
    # decode columns small so decode is cheap and the TDR cliff is purely a
    # prefill/policy phenomenon.
    return [[1, 2.0, 2.0, 2.0, 1.0, 1.0, 1.0]]


def gen_massacre(out):
    # Extreme 1 - "Microsecond Massacre": I/O & frame-rate stress.
    # Transfers + compute nearly instant; 2000 requests burst at t=1e-9.
    # Targets std::cin/cout + per-frame overhead. Must run < 3.0s locally.
    # If the solver scans an O(R) ready list per frame it will TLE.
    K, S, lat, bw, bpt, layers = 8, 1.0, 0.001, 100.0, 1, 64
    slo = [50.0, 10.0, 1000.0, 0.0, 5.0, 0.5, 0.5]
    R = 2000
    # Lin=4 (modest prefill), Lout=64 (heavy decode -> floods decode frames).
    arrivals = [(1e-9, 4, 64)] * R
    emit(out, K, S, lat, bw, bpt, layers, slo, table_sublinear(), arrivals)


def gen_blackhole(out):
    # Extreme 2 - "Network Black Hole": FIFO transfer starvation.
    # Every UP/DOWN transfer takes ~8s. The solver must NOT queue many P PRE /
    # D PRE UPs at once; the single-threaded local + transfer FIFO must keep
    # in-flight UP transfers <= 2. Local must stay busy on decode while
    # transfers trickle.
    K, S, lat, bw, bpt, layers = 8, 10.0, 50.0, 0.001, 1000000, 8
    slo = [50.0, 10.0, 1000.0, 0.0, 5.0, 0.5, 0.5]
    R = 500
    # Lin=1 -> each UP transfer len=1 -> 50 + 8*1*1e6/(0.001*1e6) = 8050ms.
    # Lout=4 -> decode work keeps local busy between transfers.
    arrivals = [(0.0, 1, 4)] * R
    emit(out, K, S, lat, bw, bpt, layers, slo, table_sublinear(), arrivals)


def gen_straggler(out):
    # Extreme 3 - "Straggler's Purgatory": makespan collapse.
    # 400 short requests (Lin=1, Lout=1) + 1 long request (Lin=512, Lout=512).
    # w_tp=1, w_c=0 -> THROUGHPUT regime. The cloud balancer must recognise the
    # large-Lin request as a proxy for large Lout and isolate it on its own
    # cloud, so the makespan is bounded by the long request's own decode time.
    K, S, lat, bw, bpt, layers = 4, 1.0, 1.0, 1.0, 1, 8
    slo = [50.0, 10.0, 1000.0, 0.0, 5.0, 1.0, 0.0]
    R = 401
    arr = [(0.0, 1, 1)] * 400
    arr.append((0.0, 512, 512))  # the straggler (id = 400)
    emit(out, K, S, lat, bw, bpt, layers, slo, table_sublinear(), arr)


def gen_void(out):
    # Extreme 4 - "Degenerate Void": zero-division / logic crash.
    # K=1 (no parallelism), num_layers=1 (chunking must not divide by zero),
    # single-row table. Solver must detect K=1/num_layers=1, disable chunking
    # and cross-cloud combine, run purely sequential. Verdict OK, score ~max.
    K, S, lat, bw, bpt, layers = 1, 1.0, 0.5, 10.0, 1, 1
    slo = [50.0, 10.0, 0.1, 0.0, 5.0, 0.5, 0.5]
    R = 10
    arrivals = [(0.0, 1, 1)] * R
    emit(out, K, S, lat, bw, bpt, layers, slo, table_one_row(), arrivals)


def gen_falsehope(out):
    # Extreme 5 - "distbase=0 False Hope": scoring suicide.
    # dist_base=0, SLO1=5.0, S=1.0, prefill_*=2.0 -> min_TDR = 3*S + 6 +
    # transfer + 2 = 11ms > SLO1. Impossible to meet TDR. If the solver sits in
    # WAITING (w_tp=0 normally -> WAITING) it holds D PRE for nothing and the
    # dist_base=0 cliff zeroes the waiting term anyway. The solver must detect
    # the cliff and flip to THROUGHPUT (max batching, combine D PRE across
    # clouds). Behavioural check: cross-cloud D PRE combine must occur.
    K, S, lat, bw, bpt, layers = 8, 1.0, 1.0, 1.0, 1, 8
    slo = [5.0, 10.0, 1000.0, 0.0, 0.0, 0.0, 1.0]
    R = 100
    arrivals = [(0.0, 4, 4)] * R
    emit(out, K, S, lat, bw, bpt, layers, slo, table_falsehope(), arrivals)


def gen_teeter(out):
    # PSGO Gauntlet 1 - "SLO Teeter-Totter" (alpha=0.5): the razor's edge is
    # SLO2/dist_base placed BETWEEN the two builds' deterministic pooled-TPOT
    # means (static 8.4028 vs PSGO 8.3808; the delta is the engine crushing
    # dmb when the straggler's batched gaps spike the running mean). Herd of
    # 1000 Lout=1 arrives spread over 1s (slower flood), straggler Lout=500
    # lands at t=1800. SLO2=8.0, dist_base=0.0495: static e_tpot=0.0503 >
    # dist_base -> wait_comp 0; PSGO e_tpot=0.0476 < dist_base -> positive
    # wait_comp. Static tips over the edge, the engine holds under it.
    K, S, lat, bw, bpt, layers = 8, 1.0, 0.001, 1000.0, 1, 8
    slo = [1e6, 8.0, 1.0, 0.0, 0.0495, 0.5, 0.5]
    table = [[m, 0.5, 10, 0.5, m, 2 * m, m] for m in BS]
    arrivals = [(1.0 * i, 1, 1) for i in range(1000)]
    arrivals.append((1800.0, 1, 500))
    emit(out, K, S, lat, bw, bpt, layers, slo, table, arrivals)


def gen_ambush(out):
    # PSGO Gauntlet 2 - "TPOT Ambush": 1000 Lout=1 requests (no gaps) + 1
    # straggler with Lout=500, all at t=0; transfers near-instant (lat=0.001,
    # bw=1000) and fast prefill so the herd floods the decode pool WHILE the
    # straggler runs. The straggler (last line -> prefilled first) then batches
    # under static dmb=32 for hundreds of gaps (~130ms rounds) -> pooled mean
    # ~70+ vs SLO2=12 -> static wait_comp 0. The engine must not panic during
    # the no-gap prefill phase and must crush dmb once the straggler's gaps
    # spike the mean; post-herd it solo-decodes at ~5ms.
    K, S, lat, bw, bpt, layers = 8, 1.0, 0.001, 1000.0, 1, 8
    slo = [1e9, 8.0, 1.0, 0.0, 0.1, 0.5, 0.5]
    table = [[m, 0.5, 10, 0.5, m, 2 * m, m] for m in BS]
    arrivals = [(0.0, 1, 1)] * 1000
    arrivals.append((1800.0, 1, 500))  # the ambush: straggler lands post-flood
    emit(out, K, S, lat, bw, bpt, layers, slo, table, arrivals)


def gen_flip(out):
    # PSGO Gauntlet 3 - "Alpha Flip": w_tp=w_c=0.5 (alpha=0.5) but decode_proc
    # is superlinear ({1,1.7,3,20,80,200,520}). Herd of 500 Lout=1 bursts at
    # t=0; the straggler (Lout=300) lands mid-flood at t=600 so its early gaps
    # are big superlinear group rounds. The run is <5000 frames so the growth
    # branch never engages: pure shrink. Static's pooled mean 6.1759 > PSGO's
    # 6.1492 (the engine's crush trims the spike); SLO2=6.16 sits between them
    # and dist_base=0.002 zeroes static's wait_comp while PSGO stays compliant
    # -> the engine's dynamic override of alpha is worth ~500 points here.
    K, S, lat, bw, bpt, layers = 8, 1.0, 0.001, 1000.0, 1, 8
    slo = [1e6, 6.16, 1.0, 0.0, 0.002, 0.5, 0.5]
    proc = {1: 1.0, 2: 1.7, 4: 3.0, 8: 20.0, 16: 80.0, 32: 200.0, 64: 520.0}
    table = [[m, 0.5, 10, 0.5, m, proc[m], m] for m in BS]
    arrivals = [(0.0, 1, 1)] * 500
    arrivals.append((600.0, 1, 300))
    emit(out, K, S, lat, bw, bpt, layers, slo, table, arrivals)



def gen_abyss(out):
    # ESSAY 1 - "Transfer Equation Abyss" (bleeding edge): every prefill UP
    # transfer = latency 50 + 8*4096*1e6/(0.001*1e6) = 32,768,050 ms (~9.1h).
    # R=2000 (max) all arriving at t=0 in ONE 2000-event frame; num_layers=64;
    # S=10 (max). Total serialized UP ~6.55e10 ms (within the 1e12 validator
    # bound); timestamps reach ~1e10-1e11 where double precision is ~1e-5 ms,
    # so any exact-time comparison desyncs. Lout=1 (Sigma=2000): decode UPs
    # are cheap (len=1 -> 8,050 ms) but share the single UP FIFO with the
    # monsters. tp_base/tp_UB relative to the one-at-a-time reference
    # (~1.31e11 ms -> 1.52e-8 tokens/ms): full UP/DOWN overlap gives
    # tp ~3.05e-8 -> c_tp ~0.62 at tp_UB=4e-8, so pipelining earns points.
    K, S, lat, bw, bpt, layers = 8, 10.0, 50.0, 0.001, 1000000, 64
    slo = [1e9, 1e9, 4e-8, 1.52e-8, 5.0, 0.5, 0.5]
    table = [[m, 1, 10, 1, 1, 1, 1] for m in BS]
    arrivals = [(0.0, 4096, 1)] * 2000
    emit(out, K, S, lat, bw, bpt, layers, slo, table, arrivals)



def gen_hope2(out):
    # ESSAY 2 - "False Hope 2: the traffic-aware cliff". dist_base=0
    # (all-or-nothing). SLO1=50 sits ABOVE the Lin=1 floor (~9ms) so the
    # STARTUP check passes, but every request has Lin=512 whose floor is
    # ~108ms (prefill_proc=100 dominates) -> the mean TDR can never meet
    # SLO1, the waiting component is mathematically 0, and only 300*c_tp is
    # salvageable. The heavy prefill (100ms P PROC per request) keeps the
    # pipe full while decode wants to start: SloAwareDelay holds every
    # first-token D PRE (all members iter=0, w_c=0.7, alpha=0.3) for a
    # provably worthless TDR -- the flip must kill the hold and let the
    # token pipeline start immediately during prefill.
    K, S, lat, bw, bpt, layers = 8, 1.0, 1.0, 100.0, 1, 8
    slo = [50.0, 1e9, 2.5, 0.5, 0.0, 0.3, 0.7]
    table = [[m, 1.0 + 0.5 * m / 64.0, 2.0 + 98.0 * m / 64.0,
              1.0 + 0.5 * m / 64.0, 1 + m / 8.0, 1 + m / 8.0, 1 + m / 8.0]
             for m in BS]
    arrivals = [(0.0, 512, 16)] * 200
    emit(out, K, S, lat, bw, bpt, layers, slo, table, arrivals)


def gen_floor(out):
    # ESSAY 2 - "Throughput Floor": tp_UB=1e9 (max allowed) makes
    # c_tp=(tp-tp_base)/(tp_UB-tp_base) ~ 0 no matter what; w_tp=1 so waiting
    # is worthless. Nothing is salvageable -- the case is a survival/regression
    # net: the solver must stay legal, fast, and in throughput mode (alpha=1
    # via w_tp=1) without chasing phantom points.
    K, S, lat, bw, bpt, layers = 8, 1.0, 1.0, 100.0, 1, 8
    slo = [1e9, 1e9, 1e9, 1.5, 5.0, 1.0, 0.0]
    table = [[m, 1, 10, 1, m, 2 * m, m] for m in BS]
    arrivals = [(0.0, 8, 8)] * 500
    emit(out, K, S, lat, bw, bpt, layers, slo, table, arrivals)


def gen_flat(out):
    # ESSAY 3 - "Flat Table" illusion: every decode column lists 10.0ms at
    # batch 1..64 -> interpolation is CONSTANT and the clamp-above region is
    # free. Batching is computationally free; capping groups at the largest
    # listed batch (64) forces ~125 S+dur payments on the local instead of
    # ~8, and any m=1 solver throws away nearly all throughput. R=1000 with
    # Lout=8 keeps the decode-ready pool >> 64 so the cap binds.
    K, S, lat, bw, bpt, layers = 8, 1.0, 0.001, 1000.0, 1, 8
    slo = [1e9, 1e9, 0.5, 0.1, 5.0, 1.0, 0.0]
    table = [[m, 0.1, 2, 0.1, 10.0, 10.0, 10.0] for m in BS]
    arrivals = [(0.0, 8, 8)] * 1000
    emit(out, K, S, lat, bw, bpt, layers, slo, table, arrivals)


def gen_flat1(out):
    # ESSAY 3 - "Single-Row Table" extreme: one row (batch_size=1), all decode
    # columns 5.0ms. Every group size clamps to 5.0 (batching free), but a
    # cap-at-max-listed rule reads max_table_m=1 and locks m=1 forever ->
    # 8000 tokens x 2 x (S+5) ~ 96s of serial local decode vs ~0.1s batched.
    # The most extreme table illusion: ~976 points swing.
    K, S, lat, bw, bpt, layers = 8, 1.0, 0.001, 1000.0, 1, 8
    slo = [1e9, 1e9, 0.6, 0.05, 5.0, 1.0, 0.0]
    table = [[1, 0.5, 2, 0.5, 5.0, 5.0, 5.0]]
    arrivals = [(0.0, 8, 8)] * 1000
    emit(out, K, S, lat, bw, bpt, layers, slo, table, arrivals)


def gen_finstorm(out):
    # ESSAY 4 - "FIN Amnesia + Coalesced Frame Storm": 2000 requests all ARR
    # at t=0 (one 2000-event frame), flat decode columns (constant 5ms) so the
    # Essay-3 unbounded caps form ONE giant D PRE/D POST group -> the final
    # frame carries a 2000-id TDN beside 2000 FIN lines: ~4000 coalesced
    # events in one frame. The solver must apply every event before deciding,
    # never schedule a finished request (FIN/TDN order within the frame is
    # unspecified), and answer the final storm frame with a clean 0.
    K, S, lat, bw, bpt, layers = 8, 1.0, 0.001, 1000.0, 1, 8
    slo = [1e9, 1e9, 2.0, 0.1, 5.0, 1.0, 0.0]
    table = [[m, 0.1, 2, 0.1, 5.0, 5.0, 5.0] for m in BS]
    arrivals = [(0.0, 8, 1)] * 2000
    emit(out, K, S, lat, bw, bpt, layers, slo, table, arrivals)


def gen_stuckbait(out):
    # ESSAY 4 - "Stuck State Bait": waiting-heavy parameters (w_c=1.0 ->
    # alpha=0 -> SloAwareDelay active, m=1 batching) with 3 requests arriving
    # 1 second apart and Lout=2. After request 0's P POST the solver is alone:
    # holding its D PRE for future batchmates would freeze time (no arrival,
    # no transfer, no busy machine -> interactor kills with 0). The hold's
    # pipe==0 / all-idle guards must decline, and every ready task must issue.
    K, S, lat, bw, bpt, layers = 2, 1.0, 1.0, 100.0, 1, 8
    slo = [30.0, 5.0, 100.0, 0.0, 5.0, 0.0, 1.0]
    table = [[m, 1, 10, 1, 1, 2, 1] for m in BS]
    arrivals = [(0.0, 8, 2), (1000.0, 8, 2), (2000.0, 8, 2)]
    emit(out, K, S, lat, bw, bpt, layers, slo, table, arrivals)


def gen_x1_blackhole(out):
    # INSANE JUDGE Extreme 1 - "Network Black Hole" (master-prompt spec):
    # K=8, R=500, S=10, latency=50, bw=0.001, bpt=1e6, Lin=4096, w_tp=1,
    # w_c=0. Each prefill UP = 50 + 8*4096*1e6/(0.001*1e6) = 32,768,050 ms
    # (~9.1h); a 100-task P PRE burst queues ~34 years of serialized UP.
    # PASS = verdict ok, score > 0 (tp_base=0, tp_UB=1e-7 brackets the
    # pipeline rate 500/1.64e10 ~ 3e-8), and UP in-flight NEVER exceeds 2
    # (V11_PPRECAP staggering, trace-verified).
    K, S, lat, bw, bpt, layers = 8, 10.0, 50.0, 0.001, 1000000, 8
    slo = [1e9, 1e9, 1e-7, 0.0, 5.0, 1.0, 0.0]
    table = [[m, 1, 10, 1, 1, 2, 1] for m in BS]
    arrivals = [(0.0, 4096, 1)] * 500
    emit(out, K, S, lat, bw, bpt, layers, slo, table, arrivals)


def gen_x2_falsehope(out):
    # INSANE JUDGE Extreme 2 - "dist_base=0 False Hope" (master-prompt spec):
    # K=4, R=100, S=1, dist_base=0, SLO1=5, SLO2=10, w_c=1, w_tp=0; table
    # prefill 2/2/2 -> min TDR = 3*1 + 6 + 2*tr > 9ms > SLO1: physically
    # impossible. With w_tp=0 the score is provably 0 for ANY solver, so the
    # contract is behavioral: the startup analyzer must print alpha=1
    # (min_TDR > SLO1 && dist_base == 0 -> Throughput Regime), the run must
    # complete cleanly, and cross-cloud D PRE combine must occur (max
    # batching, no SLO-chasing, no SloAwareDelay holds).
    K, S, lat, bw, bpt, layers = 4, 1.0, 1.0, 1.0, 1, 8
    slo = [5.0, 10.0, 1000.0, 0.0, 0.0, 0.0, 1.0]
    table = [[1, 2.0, 2.0, 2.0, 1.0, 1.0, 1.0]]
    arrivals = [(0.0, 4, 4)] * 100
    emit(out, K, S, lat, bw, bpt, layers, slo, table, arrivals)


def gen_x3_supertrap(out):
    # INSANE JUDGE Extreme 3 - "Superlinear Table Death Trap" (spec): superlinear
    # decode_pre AND decode_proc; dec_proc(1)=10, dec_proc(10)~200 (interp of
    # 75@4, 220@8); SLO2=15; R=100, Lout=512, w_c=1, w_tp=0, dist_base=0.
    # MarginalBatch::precompute MUST stop at m=1: D_PROC avg(1)=S+10=11 <
    # marg(1->2)=15; D_PRE avg(1)=S+2lat+2 ~ 3.0 < marg=6. Single-request
    # solo round = 3+0.5+10+0.5+2tr ~ 14.0ms < 15 (SLO met at m=1 only; any
    # m>=2 round >= 44ms fails). NOTE: with 100 concurrent Lout=512 decoders
    # the pooled TPOT is bounded below by the local-computer wave (~R*3ms),
    # so sub-15ms pooled TPOT is structurally impossible for ANY scheduler;
    # the verified contract is the breakpoint cap (D PROC never exceeds m=1)
    # plus clean completion.
    K, S, lat, bw, bpt, layers = 8, 1.0, 0.001, 100.0, 1, 8
    slo = [1e9, 15.0, 1000.0, 0.0, 0.0, 0.0, 1.0]
    dpre = [2, 8, 30, 110, 400, 1500, 6000]
    dproc = [10, 25, 75, 220, 700, 2400, 9000]
    table = [[BS[i], 0.5, 2, 0.5, dpre[i], dproc[i], 0.5 * BS[i]] for i in range(7)]
    arrivals = [(0.0, 4, 512)] * 100
    emit(out, K, S, lat, bw, bpt, layers, slo, table, arrivals)


def gen_x4_massacre(out):
    # INSANE JUDGE Extreme 4 - "Microsecond Massacre" (spec, constraint-fixed):
    # lat=0.001, bw=100 -> transfers ~0.001ms; R=2000 with Lout=100 = exactly
    # the Sigma-Lout 2e5 cap (512 would violate it); near-instant compute.
    # ~1M protocol events; staggered arrivals (i*0.001ms) keep timestamps
    # distinct so frames don't coalesce away. PASS = verdict ok and CPU time
    # far under the TLE budget with the zero-allocation hot path.
    K, S, lat, bw, bpt, layers = 8, 1.0, 0.001, 100.0, 1, 8
    slo = [1e9, 1e9, 1000.0, 0.0, 5.0, 1.0, 0.0]
    table = [[m, 0.001, 0.001, 0.001, m, 2 * m, m] for m in BS]
    arrivals = [(0.001 * i, 4, 100) for i in range(2000)]
    emit(out, K, S, lat, bw, bpt, layers, slo, table, arrivals)


def gen_x5_void(out):
    # INSANE JUDGE Extreme 5 - "Degenerate Void" (spec): K=1 (no cloud
    # parallelism), num_layers=1 (any chunk arithmetic dividing by layers or
    # emitting a zero-range piece crashes or violates), R=10, Lout=1, and a
    # single-row all-1.0 table (constant columns -> clamp everywhere; the
    # Essay-3 fix makes batching free here, which is optimal). PASS = verdict
    # ok on the normal, ASan/UBSan, and assert builds; every P PROC piece is
    # exactly [0,1); no cross-cloud logic can fire with K=1.
    K, S, lat, bw, bpt, layers = 1, 1.0, 1.0, 10.0, 1, 1
    slo = [50.0, 10.0, 0.1, 0.0, 5.0, 0.5, 0.5]
    table = [[1, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0]]
    arrivals = [(0.0, 1, 1)] * 10
    emit(out, K, S, lat, bw, bpt, layers, slo, table, arrivals)


def gen_x6_horizon(out):
    # LEVEL-2 Extreme 6 - "1e12 Horizon / Floating-Point Ghost": same abyss
    # physics as x1 but at R=2000 (max) -> serialized UP total ~6.6e10 ms
    # (the ceiling these transfer params allow; UP/DOWN parallel), where
    # double ULP ~1.5e-5 dwarfs EPS=1e-9. Exercises every timestamp read,
    # busy bookkeeping, oracle sum (values ~1e10 summed 2000x), and the
    # debug-build busy_until assert at magnitude. PASS = verdict ok on
    # normal/san/debug builds + oracle exact.
    K, S, lat, bw, bpt, layers = 8, 10.0, 50.0, 0.001, 1000000, 8
    slo = [1e9, 1e9, 6e-8, 0.0, 5.0, 1.0, 0.0]
    table = [[m, 1, 10, 1, 1, 2, 1] for m in BS]
    arrivals = [(0.0, 4096, 1)] * 2000
    emit(out, K, S, lat, bw, bpt, layers, slo, table, arrivals)


def gen_x7_bang(out):
    # LEVEL-2 Extreme 7 - "Simultaneous Big Bang": all 2000 ARR events share
    # t=0.000000000 (timestamps are NONDECREASING, not strictly increasing) ->
    # ONE frame with e=2000. Burns the parser (vector growth), the ppre
    # bucket insertions (must be O(1), no heap alloc), and the first-frame
    # decide. PASS = verdict ok, no TLE, solver CPU well under budget.
    K, S, lat, bw, bpt, layers = 8, 1.0, 0.001, 1000.0, 1, 8
    slo = [1e9, 1e9, 1.0, 0.1, 5.0, 1.0, 0.0]
    table = [[m, 0.1, 2, 0.1, m, 2 * m, m] for m in BS]
    arrivals = [(0.0, 8, 1)] * 2000
    emit(out, K, S, lat, bw, bpt, layers, slo, table, arrivals)


def gen_x8_vacuum(out):
    # LEVEL-2 Extreme 8 - "Lout=1 TPOT Vacuum": every request Lout=1 ->
    # total_gaps=0 -> tpot=0 by spec definition -> dist depends ONLY on TDR.
    # w=.5/.5, dist_base=10, SLO1=50. NOTE: with R=1000 on one local, mean
    # TDR >= ~R*(2S+pre+post)/2 ~ 2s >> 50 -- the SLO1 cliff is structurally
    # unreachable for ANY policy, so the verified question is empirical:
    # does SloAwareDelay's D PRE hold cost anything in the pure-TDR regime?
    # (Architecture note: the hold RESERVES the local for P PRE/P POST --
    # TDR-positive; tokens are not TDR. Measured via delay-vs-no-delay A/B.)
    K, S, lat, bw, bpt, layers = 8, 1.0, 1.0, 100.0, 1, 8
    slo = [50.0, 100.0, 1000.0, 0.0, 10.0, 0.5, 0.5]
    table = [[m, 1, 2, 1, m, 2 * m, m] for m in BS]
    arrivals = [(0.0, 4, 1)] * 1000
    emit(out, K, S, lat, bw, bpt, layers, slo, table, arrivals)


def gen_x9_above(out):
    # LEVEL-2 Extreme 9 - "Above-Largest Table Exploit": non-monotone decode
    # columns 1->10, 10->200 (superlinear ramp), 4096->5 (cheap flat ceiling).
    # The incremental marginal climb stops at m=1 (marg 21 > avg 13) and never
    # sees that per-member cost at m~2000 is ~0.05ms. Correct cap = argmin
    # over listed breakpoints of (S+2lat+dur)/m (avg is monotone within each
    # interpolation segment, so the optimum sits on a breakpoint) = 4096 ->
    # unbounded. R=2000 with Lout=100 (Sigma=2e5 cap; the spec's Lout=512
    # would violate it by 5x). A/B: cap-1 vs argmin is ~200x throughput.
    K, S, lat, bw, bpt, layers = 8, 1.0, 0.001, 1000.0, 1, 8
    slo = [1e9, 1e9, 7.0, 0.5, 5.0, 1.0, 0.0]
    table = [[1, 0.1, 2, 0.1, 10.0, 10.0, 10.0],
             [10, 0.1, 2, 0.1, 200.0, 200.0, 200.0],
             [4096, 0.1, 2, 0.1, 5.0, 5.0, 5.0]]
    arrivals = [(0.0, 4, 100)] * 2000
    emit(out, K, S, lat, bw, bpt, layers, slo, table, arrivals)


def gen_x10_slo1e9(out):
    # LEVEL-2 Extreme 10 - "1e9 SLO Impossibility": SLO1=SLO2=1e9 (max) with
    # cheap physics — worst-case serialized TDR/TPOT ~1e4 ms meets both
    # targets for ANY schedule, so dist=0 and the waiting component is
    # provably 1.0: every drop of SLO-protection is wasted. The startup
    # analyzer must detect trivial satisfiability and set alpha=1 (pure
    # throughput: max batching caps, no D PRE hold, full cross-remote
    # combine). Linear decode table so max batching pays measurably.
    K, S, lat, bw, bpt, layers = 4, 1.0, 0.001, 1000.0, 1, 8
    slo = [1e9, 1e9, 2.0, 0.5, 10.0, 0.5, 0.5]
    table = [[m, 0.1, 2, 0.1, m, 2 * m, m] for m in BS]
    arrivals = [(0.0, 8, 100)] * 1000
    emit(out, K, S, lat, bw, bpt, layers, slo, table, arrivals)


def gen_living_cliff(out):
    # D-154 "Living Cliff" v2: TRUE binary cliff (dist_base=0) with BOTH
    # tension axes —
    #   (a) prefill side: slow link (bw=1Gbps, bpt=5000) makes UP/DOWN FIFO a
    #       real queue under bursts -> admission flooding stretches mean_tdr;
    #   (b) decode side: superlinear decode_proc makes oversized batches blow
    #       pooled TPOT.
    # Disciplined play lands both means just under target (c_w=1, ~500+ pts);
    # any sloppiness >edge kills c_w to zero (-500). Calibrated by bisection
    # against the sealed leader (see docs/DECISIONS.md D-154).
    K, S, lat, bw, bpt, layers = 4, 1.0, 1.5, 1.0, 5000, 8
    slo = [LIVING_SLO1, LIVING_SLO2, 5.0, 0.05, 0.0, 0.5, 0.5]
    table = [
        [1,    1.0,  2.0, 1.0,  0.5,    2.0,  0.5],
        [16,   1.0,  4.0, 1.0,  1.0,   14.0,  1.0],
        [64,   1.0,  8.0, 1.0,  2.5,   72.0,  2.5],
        [256,  1.0, 16.0, 1.0,  7.0,  520.0,  7.0],
        [1024, 1.0, 32.0, 1.0, 22.0, 6000.0, 22.0],
        [2048, 1.0, 48.0, 1.0, 44.0, 9999.0, 44.0],
        [4096, 1.0, 64.0, 1.0, 88.0, 9999.0, 88.0],
    ]
    lins = [16, 64, 128]
    louts = [20, 28, 36, 44, 52, 60]
    arrivals = []
    t = 0.0
    rid = 0
    while rid < 150:
        for _ in range(10):  # burst of 10 every 10 ms -> UP FIFO saturation
            arrivals.append((round(t, 3), lins[rid % 3], louts[rid % 6]))
            rid += 1
        t += 10.0
    emit(out, K, S, lat, bw, bpt, layers, slo, table, arrivals)

LIVING_SLO1 = 282.4642
LIVING_SLO2 = 31.6916

AXES = {
    "transfer": gen_transfer,
    "micro": gen_micro,
    "super": gen_super,
    "degenerate": gen_degenerate,
    "cliff": gen_cliff,
    "massacre": gen_massacre,
    "blackhole": gen_blackhole,
    "straggler": gen_straggler,
    "void": gen_void,
    "falsehope": gen_falsehope,
    "teeter": gen_teeter,
    "ambush": gen_ambush,
    "flip": gen_flip,
    "abyss": gen_abyss,
    "hope2": gen_hope2,
    "floor": gen_floor,
    "flat": gen_flat,
    "flat1": gen_flat1,
    "finstorm": gen_finstorm,
    "stuckbait": gen_stuckbait,
    "x1_blackhole": gen_x1_blackhole,
    "x2_falsehope": gen_x2_falsehope,
    "x3_supertrap": gen_x3_supertrap,
    "x4_massacre": gen_x4_massacre,
    "x5_void": gen_x5_void,
    "x6_horizon": gen_x6_horizon,
    "x7_bang": gen_x7_bang,
    "x8_vacuum": gen_x8_vacuum,
    "x9_above": gen_x9_above,
    "x10_slo1e9": gen_x10_slo1e9,
    "living_cliff": gen_living_cliff,
}


def main():
    ap = argparse.ArgumentParser(description="Generate adversarial solver cases.")
    ap.add_argument("--out", default=OUT_DIR)
    ap.add_argument("--axis", default="all",
                    choices=["all", *AXES.keys()])
    args = ap.parse_args()
    todo = AXES if args.axis == "all" else {args.axis: AXES[args.axis]}
    for name, fn in todo.items():
        p = os.path.join(args.out, f"adv_{name}.txt")
        fn(p)
        print(f"wrote {p}")


if __name__ == "__main__":
    main()
