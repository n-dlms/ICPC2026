"""atlas_solve.py — hidden-test parameter atlas builder (D-155).

Input: Codeforces judgement protocol lines, one per test:
  "#N: OK [...]: points P tp=T mean_tdr=D mean_tpot=P2 dist=X norm_tp=A
   norm_c=B normalized_score=S"

Solves per test (linear algebra on the public scoring formula):
  w_tp     = (S - ncw) / (ntp - ncw)           (weight recovery)
  dist_base = dist / (1 - ncw) when 0 < ncw < 1 (cliff distance)
The output feeds docs/HIDDEN_ATLAS.md (full multi-observation solves use
atlas_multi.py). Usage: python3 tools/atlas_solve.py <judgement-file>
"""
# CF hidden-test atlas builder
# Input: judgement protocol lines (one per test), format from Codeforces:
# "#N: OK [...]: points P tp=T mean_tdr=D mean_tpot=P2 dist=X norm_tp=A norm_c=B normalized_score=S points=PTS"
# Solves per test: w_tp = (S - ncw)/(ntp - ncw); dist_base = dist/(1-ncw) when 0<ncw<1.
import re, sys, json

LINE = re.compile(
    r"#(\d+):.*?points ([\d.]+) tp=([\d.e+-]+) mean_tdr=([\d.e+-]+) mean_tpot=([\d.e+-]+) "
    r"dist=([\d.e+-]+) norm_tp=([\d.e+-]+) norm_c=([\d.e+-]+) normalized_score=([\d.e+-]+)")

def parse(path):
    tests = {}
    for line in open(path):
        m = LINE.search(line)
        if not m:
            continue
        i = int(m.group(1))
        tests[i] = dict(points=float(m.group(2)), tp=float(m.group(3)),
                        tdr=float(m.group(4)), tpot=float(m.group(5)),
                        dist=float(m.group(6)), ntp=float(m.group(7)),
                        ncw=float(m.group(8)), score_norm=float(m.group(9)))
    return tests

def solve(tests, tag):
    print(f"\n=== {tag}: {len(tests)} tests ===")
    out = {}
    for i in sorted(tests):
        t = tests[i]
        S, a, b = t["score_norm"], t["ntp"], t["ncw"]
        den = a - b
        w = (S - b) / den if abs(den) > 1e-9 else None
        dbase = t["dist"] / (1.0 - b) if 1e-9 < b < 1 - 1e-9 and t["dist"] > 0 else None
        # sanity: weights must lie in [0,1]
        ok = w is not None and -1e-6 <= w <= 1 + 1e-6
        out[i] = dict(w_tp=w, dist_base=dbase, **t)
        print(f"#{i:2d} pts={t['points']:8.3f} ntp={a:.6f} ncw={b:.6f} "
              f"w_tp={'DEGEN' if w is None else f'{w:.4f}'+('' if ok else ' !!OUT[0,1]')} "
              f"{'dbase='+format(dbase,'.4g') if dbase else ''}")
    return out

if __name__ == "__main__":
    atlas = {}
    for path, tag in zip(sys.argv[1::2], sys.argv[2::2]):
        atlas[tag] = solve(parse(path), tag)
    json.dump(atlas, open("/tmp/atlas.json", "w"), indent=1)
    print("\nwrote /tmp/atlas.json")
