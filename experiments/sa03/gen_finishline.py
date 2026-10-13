#!/usr/bin/env python3
"""SA03 finish-line experiment: same arrivals/work, different scoring lines.
If the scheduler adapts to score state at the tail, E will differ."""
import os

OUT = "/tmp/sa03/cases"
BS = [1, 2, 4, 8, 16, 32, 64]


def emit(name, slo):
    tab = [[m, 1.0, 5.0, 1.0, 1.0, m * 0.8, 1.0] for m in BS]
    arrivals = [(0.0, 8, 8)] * 10 + [(30.0, 64, 2)]
    lines = [f"4 1.0 1.0 100.0 1 8",
             " ".join(str(x) for x in slo),
             str(len(tab))]
    for r in tab:
        lines.append(" ".join(str(x) for x in r))
    lines.append(str(len(arrivals)))
    for (t, lin, lo) in arrivals:
        lines.append(f"{t} {lin} {lo}")
    p = os.path.join(OUT, name)
    with open(p, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote", p)


# tp-heavy: tight-ish tp band, waiting worthless
emit("fin_tp.txt", [1e9, 1e9, 0.5, 0.02, 5.0, 1.0, 0.0])
# waiting-heavy: tight SLO1, throughput worthless
emit("fin_wc.txt", [40.0, 5.0, 0.5, 0.02, 5.0, 0.0, 1.0])
# neutral
emit("fin_half.txt", [40.0, 5.0, 0.5, 0.02, 5.0, 0.5, 0.5])
