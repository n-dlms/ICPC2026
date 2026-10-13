#!/usr/bin/env python3
"""SA10: D_PRE/D_POST group-size + cohort analysis from interactor traces.
Usage: python3 trace_groups.py TRACE1 [TRACE2 ...]"""
import re, sys, collections
for path in sys.argv[1:]:
    dpre=collections.Counter(); dpost=collections.Counter(); dproc=collections.Counter()
    ppre=collections.Counter(); seq=[]
    lt=0.0; ft=1e18
    for line in open(path):
        m=re.match(r'DECISION t=([\d.e+-]+) ASSIGN E D PRE -1 (\d+)',line)
        if m: dpre[int(m.group(2))]+=1; seq.append(('D_PRE',float(m.group(1)),int(m.group(2))))
        m=re.match(r'DECISION t=([\d.e+-]+) ASSIGN E D POST -1 (\d+)',line)
        if m: dpost[int(m.group(2))]+=1; lt=max(lt,0)
        m=re.match(r'DECISION t=([\d.e+-]+) ASSIGN C(\d+) D PROC \d+ (\d+)',line)
        if m: dproc[int(m.group(3))]+=1
        m=re.match(r'DECISION t=([\d.e+-]+) ASSIGN E P PRE (\d+) (\d+)',line)
        if m: ppre[int(m.group(2))]+=1
        m=re.match(r'EVENT t=([\d.e+-]+) TDN E D POST.*dur=',line)
        if m: lt=max(lt,float(m.group(1)))
        m=re.match(r'EVENT t=([\d.e+-]+) ARR ',line)
        if m: ft=min(ft,float(m.group(1)))
    print(f"== {path}")
    print("  P_PRE/cloud:", dict(sorted(ppre.items())))
    print("  D_PRE hist:", dict(sorted(dpre.items())))
    print("  D_POST hist:", dict(sorted(dpost.items())))
    print("  D_PROC hist (top):", dict(sorted(dproc.items())[:12]))
    print(f"  E = {lt-ft:.1f}")
