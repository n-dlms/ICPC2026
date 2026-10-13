#!/usr/bin/env python3
"""
SB09 size analyzer: inventory 20 largest minified blocks of src/submission.cpp
Segments by brace depth (mimics merge_submission.py minify: string-aware comment strip,
blank lines removed, 1-space-per-brace reindent). Reports per-block minified chars sorted.
"""
import re, pathlib, sys

SRC = pathlib.Path("src/submission.cpp")
# Use same minify logic as tools/merge_submission.py but without _RENAME, just for sizing
# We implement minimal: strip comments, blank lines, reindent 1-space-per-brace

def strip_comments(text: str) -> str:
    out=[]; i=0; n=len(text); state=0
    while i<n:
        c=text[i]; nx=text[i+1] if i+1<n else ""
        if state==1:
            if c=='\n': state=0; out.append(c)
            i+=1; continue
        if state==2:
            if c=='*' and nx=='/': state=0; i+=2; continue
            i+=1; continue
        if state==3:
            out.append(c)
            if c=='\\' and nx: out.append(nx); i+=2; continue
            if c=='"': state=0
            i+=1; continue
        if state==4:
            out.append(c)
            if c=='\\' and nx: out.append(nx); i+=2; continue
            if c=="'": state=0
            i+=1; continue
        if c=='/' and nx=='/': state=1; i+=2; continue
        if c=='/' and nx=='*': state=2; i+=2; continue
        if c=='"': state=3; out.append(c); i+=1; continue
        if c=="'": state=4; out.append(c); i+=1; continue
        out.append(c); i+=1
    return "".join(out)

def strip_strings(line: str) -> str:
    out=[]; i=0; st=0
    while i<len(line):
        c=line[i]
        if st==0:
            if c=='"': st=1
            elif c=="'": st=2
            elif c=='/' and i+1<len(line) and line[i+1]=='/': break
            else: out.append(c)
        elif st==1:
            if c=='\\': i+=2; continue
            if c=='"': st=0
        else:
            if c=='\\': i+=2; continue
            if c=="'": st=0
        i+=1
    return "".join(out)

def minify(text:str)->str:
    # strip comments already
    # remove blank lines, reindent
    out=[]; depth=0
    for line in text.split("\n"):
        s=line.strip()
        if not s: continue
        if s.startswith("#"): out.append(s); continue
        code=strip_strings(s)
        lead_close=len(code)-len(code.lstrip("}"))
        emit_depth=max(0,depth-lead_close)
        out.append(" "*emit_depth+s)
        depth+=code.count("{")-code.count("}")
        if depth<0: depth=0
    return "\n".join(out)+"\n"

raw = SRC.read_text()
clean = strip_comments(raw)
mini = minify(clean)

# Segment by brace depth
# We want blocks at depth 1 (direct children of namespace or global)
# Approach: iterate through mini, track depth, record blocks that start at depth 1->2 and end when depth returns to 1

blocks=[]
depth=0
start_idx=-1
start_line=-1
lines=mini.split("\n")
# Need char-wise depth
pos=0
n=len(mini)
depth=0
block_start=-1
block_name=""
# Use string-aware brace counting
in_dq=False; in_sq=False
for i,ch in enumerate(mini):
    nx=mini[i+1] if i+1<n else ""
    if in_dq:
        if ch=='\\' and nx: continue
        if ch=='"': in_dq=False
        continue
    if in_sq:
        if ch=='\\' and nx: continue
        if ch=="'": in_sq=False
        continue
    if ch=='"': in_dq=True; continue
    if ch=="'": in_sq=True; continue
    if ch=='{':
        if depth==1 and block_start==-1:
            # start of depth-2 block, capture preceding header (prev ~200 chars)
            # Find start of declaration: look backward for newline
            j=mini.rfind("\n", 0, i)
            header = mini[max(0,j-120):i].strip().split("\n")[-1].strip()[:120]
            block_start=i
            block_name=header
        depth+=1
    elif ch=='}':
        depth-=1
        if depth==1 and block_start!=-1:
            block_end=i
            content=mini[block_start:block_end+1]
            blocks.append((block_name, len(content), block_start, block_end))
            block_start=-1
        if depth<0: depth=0

# Also consider global blocks (includes, main) outside namespace - they are depth 0
# Collect them separately: everything before "namespace icpc2026" and after its close
# For now just report depth-1 blocks + add synthetic "global header" and "main"

# Sort by size descending
blocks_sorted=sorted(blocks, key=lambda x: x[1], reverse=True)

print(f"Total minified chars: {len(mini)} (CF limit 65535, margin {65535-len(mini)})")
print(f"Found {len(blocks)} depth-1 blocks (classes/functions inside namespace)\n")
print("20 LARGEST BLOCKS (minified chars):")
print("-"*80)
for idx,(name,size,a,b) in enumerate(blocks_sorted[:20],1):
    print(f"{idx:2d}. {size:5d} chars | {name!r}")
print("-"*80)
# Show also cumulative
cum=sum(s for _,s,_,_ in blocks_sorted[:20])
print(f"Cumulative top-20: {cum} ({cum/len(mini)*100:.1f}% of file)")
print(f"Remainder ({len(blocks)-20} blocks): {len(mini)-cum} chars")

# Also dump full list to file for doc inclusion
out_path=pathlib.Path("research/techniques/SB09_block_inventory.txt")
with open(out_path,"w") as f:
    f.write(f"Total minified {len(mini)} margin {65535-len(mini)}\n")
    for idx,(name,size,a,b) in enumerate(blocks_sorted,1):
        f.write(f"{idx:3d}. {size:5d} {name}\n")
print(f"\nFull inventory written to {out_path}")

# Also report raw vs minified per block for top 5 with context
for name,size,a,b in blocks_sorted[:5]:
    print(f"\n--- {name} ({size} chars) ---")
    print(mini[a-200:a+300][:500])
