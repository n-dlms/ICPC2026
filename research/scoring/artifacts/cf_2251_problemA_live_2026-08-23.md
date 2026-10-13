# Live fetch of https://codeforces.com/contest/2251/problem/A — captured 2026-08-23 via web reader tool
# Fetched WITHOUT login cookies (reader-side anonymous view of a logged-in page cache is NOT guaranteed).
# NOTE: the live page displayed a banner: "The problem statement has recently been changed. View the changes."
#       The ?changes=true variant returned the same ordinary statement body to the reader tool (no diff text recovered).
# KEY SECTIONS VERBATIM (cross-checked against local scrape /home/dlamini/ICPC2026/problem.md of Aug/22):

## Banner
The problem statement has recently been changed. View the changes.

## Intro
This is an interactive scheduling problem. The "interactor" sends events as they happen; after each event group, you choose ready tasks to start. You never predict future requests. For a fixed test it is deterministic and non-adaptive: the same legal responses produce the same events and score.

IMPORTANT: YES, THERE WILL BE A SYSTEM TEST AFTER THE CHALLENGE.

## Challenge Format (verbatim)
Two worked examples appear at the end of the statement. Test 1 is the public worked Example 1, tests 2–22 are hidden preliminary tests, and finals use a separate frozen set. Per-test scores are reported on the 0–1000 scale defined below, and the contest system aggregates them according to the contest rules; no hacks are used.

## Scoring (verbatim core)
clamp(x;base,target)=max(0,min(1,(x−base)/(target−base)))

Output-Rate Component. tp=∑iLout[i]/total elapsed time tokens/ms, where total elapsed time = (the latest final-token production time over all requests) − (the earliest request arrival time), in ms. Output rate component = clamp(tp;tpbase,tpUB). The value tpbase comes from a fixed one-request-at-a-time reference schedule; tpUB is an estimated high rate. At or below the baseline the component is 0; at or above the upper bound it is 1; between them it grows linearly. These values set scoring and are not promises about any solution in the testing kit.

Waiting-Time Component. [...] tdr = mean over all requests of (input stage final step completion − arrival). tpot = mean of ej+1−ej over all consecutive output gaps of all requests pooled (1≤j<Lout[i]). A request with Lout[i]=1 contributes no gaps. If no request contributes a gap, tpot is defined as 0.

excess_tdr=max(0,(tdr−SLO1)/SLO1), excess_tpot=max(0,(tpot−SLO2)/SLO2), dist=sqrt(excess_tdr²+excess_tpot²).

waiting-time component = clamp(dist;distbase,0) — the same conversion with lower values treated as better: lower dist is better, 1 at dist=0, 0 at the reference scheduler's amount above the waiting-time targets distbase. Written out directly (the form easiest to implement and verify):

waiting-time component = { max(0, 1−dist/distbase) if distbase>0; 1 if distbase=0 and dist=0; 0 if distbase=0 and dist>0 }

Important special case: if distbase=0, the waiting-time component is either 0 or 1. It is 1 only when both mean-waiting-time targets are met (dist=0), and 0 otherwise.

NormalizedScore = wtp·clamp(tp;tpbase,tpUB) + wc·clamp(dist;distbase,0), Score = 1000·NormalizedScore.

Thus each completed test awards a score in [0,1000]. Verdicts. A completed interaction scores as above. A protocol error, malformed output, stuck state with no future event, or exceeding the time/memory limit scores 0 on that test; other tests are unaffected. No partial credit is awarded for an unfinished test.

## Contest aggregation (verbatim)
The 22 preliminary tests provide feedback and do not contribute to the final ranking. The final score is the arithmetic mean of the 20 frozen final-test scores. Ranking uses that mean before display rounding; the displayed score is rounded to three digits after the decimal point. Therefore a known per-test vector of 0, 500, and 1000-point outcomes contributes exactly those values before averaging. This problem is not open to hacks.

## Constraints (verbatim)
1≤K≤8; 1≤R≤2000; 1≤Lin[i]≤4096; 1≤Lout[i]≤512; ∑iLout[i]≤2·10^5 per test; 1≤num_layers≤64; 2≤N≤4096.
1≤S≤10; 0.001≤latency_in_ms≤50; 0.001≤bandwidth_gbps≤100; 1≤bytes_per_token≤10^6.
0.001≤SLO1,SLO2≤10^9; 0≤tpbase,distbase≤10^9; 10^−9≤tpUB≤10^9; and tpUB>tpbase.
Every non-missing task-table entry is in [0.001,10^4] ms. [...] At most 2·10^6 frames per test; use fast I/O.
