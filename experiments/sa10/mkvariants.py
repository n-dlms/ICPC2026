#!/usr/bin/env python3
"""SA10: gate-isolation case variants (copies under /tmp/sa10; sources untouched).
x9_k1.txt   - K=1 (kills EFT spread; isolates cohort-race fragmenter)
x9_wc.txt   - w_tp=0.95/w_c=0.05 (activates DPRE_ACCUMULATE target -> re-merge)
m5_wc0.txt  - m5_order with w_c=0 (deactivates DPRE_ACCUMULATE)
var_*.txt   - w_c=0 residents with w_c=0.05 (A1 blast-radius probes)"""
import os
R='/home/dlamini/ICPC2026'
def rw(src,dst,edits):
    L=open(src).read().split('\n')
    for (ln,fn) in edits: L[ln]=fn(L[ln])
    open(dst,'w').write('\n'.join(L))
rw(f'{R}/cases/adv/adv_x9_above.txt','/tmp/sa10/x9_k1.txt',[(0,lambda s:'1 '+ ' '.join(s.split()[1:]))])
rw(f'{R}/cases/adv/adv_x9_above.txt','/tmp/sa10/x9_wc.txt',[(1,lambda s:'1000000000.0 1000000000.0 7.0 0.5 5.0 0.95 0.05')])
rw(f'{R}/cases/mimics/m5_order.txt','/tmp/sa10/m5_wc0.txt',[(1,lambda s:'2000.0 50.0 2.6 0.05 3.0 1.0 0.0')])
for n,d in [('m19b','mimics'),('m19c','mimics'),('adv_x4_massacre','adv'),('adv_x7_bang','adv'),
            ('adv_x1_blackhole','adv'),('adv_x6_horizon','adv'),('adv_straggler','adv'),('adv_finstorm','adv')]:
    rw(f'{R}/cases/{d}/{n}.txt',f'/tmp/sa10/var_{n}.txt',
       [(1,lambda s:' '.join(s.split()[:5]+['0.95','0.05']))])
print("variants written to /tmp/sa10/")
