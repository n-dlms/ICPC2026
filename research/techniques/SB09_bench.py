#!/usr/bin/env python3
"""
SB09 bench driver: runs SB09_bench.cpp benches, collects numbers, and prints summary.
Also runs size analyzer and rename audit.
"""
import subprocess, pathlib, re, sys

def run(cmd):
    r = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    return r.stdout + r.stderr

print("=== SB09 BENCH DRIVER ===")
# Build bench if needed
subprocess.run("g++ -std=c++17 -O2 -Wall -Wextra -Werror -Wno-pedantic -o /tmp/sb09bench research/techniques/SB09_bench.cpp", shell=True, check=True)
print(run("/tmp/sb09bench parse_check 2>&1 | head -n 30"))
print(run("/tmp/sb09bench parse_bench 2>/dev/null"))
print(run("/tmp/sb09bench writer_check 2>/dev/null"))
print("--- writer bench (format only, no syscall flood) ---")
print(run("/tmp/sb09bench writer_bench 2>/dev/null | head -n 10"))
print(run("/tmp/sb09bench clock_bench 2>/dev/null"))

print("\n=== SIZE ANALYZER ===")
print(run("python3 research/techniques/SB09_size_analyzer.py 2>&1 | head -n 40"))

# Rename audit: find long identifiers >=6 chars, >=5 occurrences, safe to rename
print("\n=== RENAME AUDIT ===")
import collections
text = pathlib.Path("src/submission.cpp").read_text()
# remove string literals
def strip_strings(s):
    res=[]; i=0; n=len(s)
    in_dq=in_sq=False
    while i<n:
        c=s[i]
        if in_dq:
            if c=='\\': i+=2; continue
            if c=='"': in_dq=False
            i+=1; continue
        if in_sq:
            if c=='\\': i+=2; continue
            if c=="'": in_sq=False
            i+=1; continue
        if c=='"': in_dq=True; i+=1; continue
        if c=="'": in_sq=True; i+=1; continue
        res.append(c); i+=1
    return "".join(res)
clean = strip_strings(text)
clean = re.sub(r'//.*', '', clean)
clean = re.sub(r'/\*.*?\*/', '', clean, flags=re.S)
tokens = re.findall(r'\b[A-Za-z_][A-Za-z0-9_]*\b', clean)
# Filter: length>=6, not std keyword, not in global keyword set + std members + macros
std_names = set(["include","define","ifdef","endif","ifndef","else","namespace","class","struct","public","private","static","const","constexpr","virtual","override","inline","using","return","if","else","for","while","switch","case","break","continue","template","typename","size_t","int","double","float","char","bool","void","auto","std","vector","string","map","set","queue","deque","pair","chrono","steady_clock","TimePoint","Duration","static_cast","assert","push_back","string_view","emplace_back","reserve","clear","empty","size","back","front","begin","end","assign","resize","capacity","data","c_str","substr","find","rfind","length","to_string","stod","stoi","sort","max","min","clamp","sqrt","round","ceil","floor","abs","fabs","pow","exp","log","sin","cos"])
# Also exclude std method names and keywords
keyword_extra = set(["second","first","second","valid_","min_lin","max_lin","next_token","next_int","next_double","push_back","pop_back"])
cnt = collections.Counter(tokens)
candidates=[]
for w,c in cnt.most_common(300):
    if len(w)>=6 and c>=5 and w not in std_names and w not in keyword_extra and not w.startswith("q"):
        if w in ["PRE","POST","PROC","ARR","TDN","XDN","FIN","P_PRE","P_POST","P_PROC","D_PRE","D_PROC","D_POST","DECODE_PRE","DECODE_POST","DECODE_PROC","PREFILL_PRE","PREFILL_POST","PREFILL_PROC"]: continue
        # Exclude single-std-word collisions: distance collides with std::distance if using namespace std
        if w in ["distance","latency_ms","bandwidth_gbps","bytes_per_token","num_layers"]: 
            # these are safe as members only if we don't add using namespace std that brings std::distance; we handle separately
            pass
        candidates.append((w,c))
# After raw count, filter via collision rules: if using namespace std will be added (P1), distance is unsafe
safe_candidates = [p for p in candidates if p[0] not in ["distance","second","first","valid_","second"]]
print(f"Long identifiers >=6 chars, >=5 occurrences, raw candidates: {len(candidates)}")
print(f"After collision rule (exclude std::distance, std members): {len(safe_candidates)} safe")
for w,c in safe_candidates[:30]:
    print(f"  {w:25s} {c:4d} occ -> rename est save {(len(w)-2)*c:4d} chars")
# Estimate saving
saving = sum((len(w)-2)*c for w,c in safe_candidates[:20])
print(f"Top-20 safe saving estimate: {saving} chars minified (2-char q name)")

# Curated safe list per doc
print("\nCurated 6-rename set from doc (§4.4): Request, latency_ms, bandwidth_gbps, bytes_per_token, num_layers, pending -> ~531 chars")

# Allocation harness
print("\n=== ALLOCATION HARNESS ===")
hpath = pathlib.Path("research/techniques/SB09_alloc_harness.cpp")
if hpath.exists():
    subprocess.run("g++ -std=c++17 -O2 -Wall -Wextra -Werror -Wno-pedantic -Wno-mismatched-new-delete -o /tmp/sb09alloc research/techniques/SB09_alloc_harness.cpp", shell=True, check=True)
    print(run("/tmp/sb09alloc 2>&1"))
else:
    print("harness not created - static audit used")

print("\n=== DONE ===")
