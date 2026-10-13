#!/usr/bin/env python3
"""Build a single self-contained submission file from the layered sources.

Inlines every layer (headers in dependency order, then the .cpp units with
Solution.cpp last) into one translation unit. The artifact is laid out cleanly:

    // single short header line
    #include <...>          <- ALL system includes, one block, very top
    #include <...>
    using namespace ...;    <- hoisted to the top (if any)
    #define FLAGS ...       <- candidate strategy flags

    namespace icpc2026 {    <- one outer namespace wraps everything
        // constants / enums that used to sit inside each header's own
        // namespace block are surfaced here, at the top of the namespace,
        // so all the usual "top-of-file" declarations sit together.

Project-local `#include "*.hpp"` lines are stripped (the headers are emitted
inline) and every comment is stripped so the pasted file is clean.

Output: src/submission.cpp (overwrites the thin include-wrapper).
"""
import re
import pathlib

SRC = pathlib.Path("src")
OUT = SRC / "submission.cpp"

# Policy selection is now RUNTIME (PART 7 adaptive switch): the scoring params
# drive a Regime (THROUGHPUT / WAITING / BALANCED) chosen at startup in
# Solution.cpp::select_regime, and the Scheduler reads policies_.combine_dpre /
# policies_.xremote_cap instead of compile-time V8/V9 flags. The FLAGS below are
# therefore documentation-only markers (the candidate no longer needs -D flags);
# they are harmless when defined but unused. Keep this list as the human-readable
# record of what the promoted candidate embodies.
# PSGO_STATIC (D-049 option a, human call 2026-08-16): the PSGO gradient engine is
# compiled OUT of the submission artifact. Under the V11/V12 flag set the UP-FIFO
# gate pre-bounds D PRE groups below dmb, making the engine inert-to-harmful
# (flip -4.1, dribble -0.026); the artifact keeps the D-046 candidate behavior
# plus the D-048 judge fixes. The engine stays active in the dev build
# (build/solution) for continued PSGO experimentation.
# D-062 (human call 2026-08-16): PSGO engine SHIPS in the artifact — the CF field data
# (tests 5/6/19: full-batching destroys TPOT on spread-arrival linear tables) is the
# exact class the engine's dynamic shrink targets. D-049 option (a) superseded.
# D-074 (human call 2026-08-16): D3 re-activated — DPRE_ACCUMULATE only; the COND
# semantics (target>0 iff w_c>0 AND flat decode table) are folded inside the single
# #ifdef block in scheduler.cpp. Validated on the current engine-out artifact:
# t13 +0.248, t14 +0.115, zero regression t5/t6/t16/t22, x1 304.492 bit-identical,
# table_1row -0.006 accepted. Rollback: remove the flag from this list.
FLAGS = ["PART7_ADAPTIVE_REGIME", "V11_PPRECAP", "V12_MIRROR", "V15_LINORDER",
        "DPRE_ACCUMULATE", "PSGO_STATIC",
        # Gaps-audit mechanisms (research/gaps/, 2026-08-26): one per CF probe.
        # Rollback for any of them = delete its name from this list.
        "MECH_PRESSURE_PICK", "MECH_DPRE_UNGATE", "MECH_ROUNDTIME"]

# Emit headers in dependency order, then the .cpp translation units, with
# Solution.cpp (which holds main()) last.
HEADERS = ["protocol.hpp", "task_table.hpp", "state.hpp",
           "policies.hpp", "scheduler.hpp"]
UNITS = ["protocol.cpp", "task_table.cpp", "state.cpp",
         "scheduler.cpp", "policies.cpp", "Solution.cpp"]

proj_inc = re.compile(r'^\s*#include\s+"[^"]+"')
any_inc = re.compile(r'^\s*#include\b')
sys_inc = re.compile(r'^\s*#include\s*<([^>]+)>')
ns_open = re.compile(r'^\s*namespace\s+icpc2026\s*\{')
using_ns = re.compile(r'^\s*using\s+namespace\b')


def strip_proj_includes(text: str) -> str:
    return "\n".join(l for l in text.splitlines() if not proj_inc.match(l))


def collect_sys_includes() -> list:
    seen = []
    for name in HEADERS + UNITS:
        for line in (SRC / name).read_text().splitlines():
            m = sys_inc.match(line)
            if m:
                inc = m.group(1).strip()
                if inc not in seen:
                    seen.append(inc)
    return seen


def strip_comments(text: str) -> str:
    """Remove // and /* */ comments while preserving string/char literals."""
    out = []
    i = 0
    n = len(text)
    state = 0  # 0 normal, 1 line, 2 block, 3 dquote, 4 squote
    while i < n:
        c = text[i]
        nx = text[i + 1] if i + 1 < n else ""
        if state == 1:
            if c == "\n":
                state = 0
                out.append(c)
            i += 1
            continue
        if state == 2:
            if c == "*" and nx == "/":
                state = 0
                i += 2
                continue
            i += 1
            continue
        if state == 3:
            out.append(c)
            if c == "\\" and nx:
                out.append(nx)
                i += 2
                continue
            if c == '"':
                state = 0
            i += 1
            continue
        if state == 4:
            out.append(c)
            if c == "\\" and nx:
                out.append(nx)
                i += 2
                continue
            if c == "'":
                state = 0
            i += 1
            continue
        if c == "/" and nx == "/":
            state = 1
            i += 2
            continue
        if c == "/" and nx == "*":
            state = 2
            i += 2
            continue
        if c == '"':
            state = 3
            out.append(c)
            i += 1
            continue
        if c == "'":
            state = 4
            out.append(c)
            i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def strip_namespace(body: str) -> str:
    """Remove the single outer `namespace icpc2026 { ... }` wrapper so the
    content can live inside one outer namespace we provide."""
    lines = body.split("\n")
    open_idx = None
    for i, l in enumerate(lines):
        if ns_open.match(l):
            open_idx = i
            break
    if open_idx is None:
        return body
    full = "\n".join(lines)
    # absolute index of the '{' on the opening line
    brace_in_line = lines[open_idx].find("{")
    line_start = sum(len(lines[j]) + 1 for j in range(open_idx))
    abs_open_brace = line_start + brace_in_line
    # scan for the matching close brace (string/char aware)
    depth = 1
    i = abs_open_brace + 1
    n = len(full)
    in_dq = in_sq = False
    close_pos = -1
    while i < n:
        c = full[i]
        nx = full[i + 1] if i + 1 < n else ""
        if in_dq:
            if c == "\\" and nx:
                i += 2
                continue
            if c == '"':
                in_dq = False
            i += 1
            continue
        if in_sq:
            if c == "\\" and nx:
                i += 2
                continue
            if c == "'":
                in_sq = False
            i += 1
            continue
        if c == '"':
            in_dq = True
            i += 1
            continue
        if c == "'":
            in_sq = True
            i += 1
            continue
        if c == "{":
            depth += 1
            i += 1
            continue
        if c == "}":
            depth -= 1
            if depth == 0:
                close_pos = i
                break
            i += 1
            continue
        i += 1
    if close_pos < 0:
        return body  # safety: leave untouched rather than mangle
    # remove the opening line (with its newline) and the close brace char
    open_line_end = line_start + len(lines[open_idx]) + 1
    rest = full[:line_start] + full[open_line_end:]
    new_close = close_pos - (open_line_end - line_start)
    rest = rest[:new_close] + rest[new_close + 1:]
    return rest


def body(name: str, keep_using: bool = False):
    raw = (SRC / name).read_text()
    no_proj = strip_proj_includes(raw)
    no_inc = "\n".join(l for l in no_proj.splitlines() if not any_inc.match(l))
    clean = strip_comments(no_inc)
    # `using namespace icpc2026;` is redundant inside the single outer
    # namespace, so drop it there; keep it only for Solution.cpp, which is
    # emitted at global scope and needs it to resolve the icpc2026 names.
    if not keep_using:
        clean = "\n".join(l for l in clean.splitlines() if not using_ns.match(l))
    return strip_namespace(clean)


def _strip_strings(line: str) -> str:
    """Mask string/char literals and trailing // comments for brace counting."""
    out = []
    i = 0
    st = 0  # 0 code, 1 string, 2 char
    while i < len(line):
        c = line[i]
        if st == 0:
            if c == '"':
                st = 1
            elif c == "'":
                st = 2
            elif c == "/" and i + 1 < len(line) and line[i + 1] == "/":
                break
            else:
                out.append(c)
        elif st == 1:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                st = 0
        else:
            if c == "\\":
                i += 2
                continue
            if c == "'":
                st = 0
        i += 1
    return "".join(out)


def _strip_v11_else(text: str) -> str:
    """V11_PPRECAP is baked into FLAGS, so its #else arms are dead text in the
    artifact. Drop the '#else' line and the dead arm while keeping the
    #ifdef..#endif balance (verified: the two V11 #else arms contain no nested
    #if directives; no other #else exists in the merged sources)."""
    out = []
    lines = text.split("\n")
    i = 0
    n = len(lines)
    while i < n:
        if lines[i].strip() == "#ifdef V11_PPRECAP":
            depth = 0
            j = i + 1
            else_idx = -1
            end_idx = -1
            while j < n:
                s = lines[j].strip()
                if s.startswith(("#ifdef ", "#ifndef ")) or s.startswith("#if "):
                    depth += 1
                elif s == "#endif":
                    if depth == 0:
                        end_idx = j
                        break
                    depth -= 1
                elif s == "#else" and depth == 0:
                    else_idx = j
                j += 1
            if else_idx >= 0 and end_idx > else_idx:
                out.extend(lines[i:else_idx])
                out.append(lines[end_idx])  # keep the matching #endif
                i = end_idx + 1
                continue
        out.append(lines[i])
        i += 1
    return "\n".join(out)


# Curated rename map: my own long identifiers -> collision-free short tokens.
# Applied globally (declarations + uses) on the merged TU, so the artifact stays
# self-consistent and compiles. C++ keywords / std names are deliberately absent.
# Re-added (D-139) to fund the Theorem B pick_local block (65,910 over limit).
_RENAME = {
    "multi_token_dpre_count_": "q0",
    "worst_up_lin": "q1",
    "earliest_arrival": "q2",
    "prefill_inflight_": "q3",
    "transfer_time": "q4",
    "in_ready_list": "q5",
    "count_phase": "q6",
    "num_clouds_": "q7",
    "is_percloud": "q8",
    "last_finish": "q9",
    "finish_times": "qa",
    "max_batch_for": "qb",
    "CurrentTask": "qc",
    "TransferQueue": "qd",
    "FastScanner": "qe",
    "ProtocolReader": "qf",
    "MarginalBatch": "qg",
    "DelayPolicy": "qh",
    "ChunkPolicy": "qi",
    "TaskTimeTable": "qj",
    "READY_D_PRE": "qk",
    "READY_P_PRE": "ql",
    "PPRE_BUCKETS": "qm",
    "READY_P_PROC": "qn",
    "READY_D_PROC": "qo",
    "PREFILL_PRE": "qp",
    "PREFILL_POST": "qq",
    "READY_P_POST": "qr",
    "READY_D_POST": "qs",
    "PREFILL_PROC": "qt",
    "PPRE_MAXLIN": "qu",
    "DECODE_PROC": "qv",
    "DECODE_POST": "qw",
    "WAIT_INPUT_DOWN": "qx",
    "P_PROC_RUNNING": "qy",
    "marginal_tdr_v": "qz",
    "marginal_tpot_v": "qA",
    "marginal_tp_v": "qB",
    "up_inflight_ppre": "qC",
    "up_inflight_dpre": "qD",
    "prefill_urgent": "qE",
    "wave_cap": "qF",
    "slack_factor": "qG",
    "flow_slack": "qH",
    "pact": "qI",
    "dpre_full": "qJ",
    "ppre_full": "qK",
    "ppre_id": "qL",
    "ppost_id": "qM",
    "dpre_acc_skip": "qN",
    "combine_dpre": "qO",
    "xremote_cap": "qP",
    "rl_head": "qQ",
    "rl_pop": "qR",
    "rl_size": "qS",
    "ppre_pop": "qT",
    "ppre_push": "qU",
    "cfg_": "qV",
    "table_": "qW",
    "policies_": "qX",
    "num_clouds": "qY",
    "active_on_cloud": "qZ",
    "busy_until": "q10",
    "next_ready": "q11",
    "request": "q12",
    "cloud": "q13",
    "step_of": "q14",
    "busy": "q15",
    # Patch A renames (median straggler + per-cloud Lout) — keep leader + win small
    "c_lout_ewma_": "vb",
    "c_lout_ewma": "vc",
    "median_active_lin": "q8e",
    "ewma_lout": "vy",
    "listed": "vz",
    "S_cache": "vB",
    "max_m_": "vC",
    "alpha_": "vD",
    "State": "St",
    "Config": "Cg",
    "Scheduler": "Se",
    "table": "tb",
    "SLO1": "s1",
    "SLO2": "s2",
    "dist_base": "v0",
    "next_token": "v1",
    "EventKind": "v2",
    "other_cloud": "v3",
    "NUM_PHASES": "v4",
    "set_marginal_fast_mode": "v5",
    "g_marginal_fast": "v6",
    "phase_idx": "v7",
    "min_lin": "v8",
    "max_lin": "v9",
    "ppre_will_block_dpre": "va",
    "BatchPolicy": "vd",
    "other_sz": "vf",
    "n_ready": "vg",
    "CloudPolicy": "vh",
    "PPreBuckets": "vi",
    "rl_push": "vj",
    "next_double": "q9x",
    "next_int": "q9y",
    "ewma_lin": "vx",
    "state": "st",
    "admitted_count": "qac",
    "admitted": "qad",
    "rl_empty": "qE0",
    "excess_tpot": "qAt",
    "target_m": "tm",
    "target_d": "td",
    "target_c": "tc",
    "drain_mode": "dm",
    "max_interarrival": "mi",
    "admitted_cloud": "ac",
    "up_proj_dec_members": "updm",
    "down_proj_pre": "dpp",
    "down_proj_dec": "dpd",
    "refresh_oppoint": "rop",
    "update_targets": "ut",
    "margins": "mg",
    "tail_crit": "tac",
    "link_crit": "lic",
    "num_arrived": "na",
    "num_unfinished": "nu",
    "dec_down_inflight": "ddi",
    "down_dec_head": "ddh",
    "up_dec_members": "udm",
    "admitted_on": "aon",
    "op_P": "opP",
    "op_j": "opj",
    "op_lam": "opl",
    "prev_ready": "pr",
    "group_size": "gs",
    "next_piece": "np",
    "hold_dpre": "hd",
    "excess_tdr": "et",
    "excess_dist": "ed",
    "total_gaps": "tg",
    "ewma_lout_": "el",
    "last_token": "lt",
    "count_tdr": "ct",
    "admitted_cloud_": "ac_",
    "target_m_": "tm_",
    "target_d_": "td_",
    "target_c_": "tc_",
    "dpre_pool": "dp",
    "dpost_take": "dpt",
    "best_cost": "bc",
    "max_m_slo": "mms",
    "num_layers": "nl",
    "latency_ms": "lm",
    "bytes_per_token": "bpt",
    "projected_tp": "ptp",
    "DECODE_PRE": "DP",
    "DECODE_POST": "DQ",
    "DECODE_PROC": "DR",
    "PREFILL_PRE": "PP",
    "PREFILL_POST": "PQ",
    "PREFILL_PROC": "PR",
    "SloAwareDelay": "SL",
    "psgo_max_m": "pm",
    "best_score": "bs",
    "rl_remove": "rr",
}


def shorten_names(text: str) -> str:
    for long, short in _RENAME.items():
        text = re.sub(r"\b" + re.escape(long) + r"\b", short, text)
    return text


def minify(text: str) -> str:
    """Codeforces rejects submissions over 65535 characters. The artifact keeps
    every compiled code token but drops dead-source and presentation bytes:
    - #ifdef LOCAL_DIAG / #ifdef DEBUG / #ifndef PSGO_STATIC blocks (never
      compiled into the artifact: LOCAL_DIAG is never defined, NDEBUG kills
      DEBUG, PSGO_STATIC removes the engine by design — D-049/D-059/D-061);
    - V11_PPRECAP #else arms (flag baked in — D-104 size recovery);
    - vestigial ICPC2026_*_HPP guard #defines (D-062 drops the guard opens);
    - #ifndef PPA_ALPHA/PPA_BETA/PPRE_MAX_INFLIGHT wrappers (never defined
      before those lines in the merged TU);
    - blank lines; then REINDENT at one space per brace level so the file
      stays human-readable (the layered sources hold the full formatting).
    Safety: no line-continuation backslashes, no multi-line strings, and none
    of the stripped blocks contain nested #if directives (all verified)."""
    for pat in (r"#ifdef LOCAL_DIAG\n.*?\n#endif\n",
                r"#ifdef DEBUG\n.*?\n#endif\n",
                r"#ifndef PSGO_STATIC\n.*?\n#endif\n"):
        text = re.sub(pat, "", text, flags=re.S)
    text = _strip_v11_else(text)
    text = shorten_names(text)
    text = re.sub(r"#ifdef V11_PPRECAP\n#endif\n", "", text)
    text = re.sub(r"#ifndef PPA_ALPHA\n#define PPA_ALPHA 1\.0\n#endif\n",
                  r"#define PPA_ALPHA 1.0\n", text)
    text = re.sub(r"#ifndef PPA_BETA\n#define PPA_BETA 1\.0\n#endif\n",
                  r"#define PPA_BETA 1.0\n", text)
    text = re.sub(r"#ifndef PPRE_MAX_INFLIGHT\n#define PPRE_MAX_INFLIGHT 2\n#endif\n",
                  r"#define PPRE_MAX_INFLIGHT 2\n", text)
    # D-062: include guards are meaningless in one TU; drop guard opens and
    # their matching #endif via preprocessor-depth tracking (funds the PSGO
    # engine's return under the 65,535 cap).
    outl = []
    depth = 0
    guard_stack = []
    for line in text.split("\n"):
        st = line.strip()
        if re.match(r"#ifndef ICPC2026_\w+_HPP$", st):
            guard_stack.append(True)
            continue
        if st == "#endif" and guard_stack and guard_stack[-1]:
            guard_stack.pop()
            continue
        if st.startswith(("#if", "#ifdef", "#ifndef")):
            guard_stack.append(False)
        elif st == "#endif" and guard_stack:
            guard_stack.pop()
        outl.append(line)
    # D-104: the ICPC2026_*_HPP guard opens were dropped above, so their
    # #define lines are dead in a single-TU artifact.
    outl = [l for l in outl if not re.match(r"#define ICPC2026_\w+_HPP$", l.strip())]
    text = "\n".join(outl)
    out = []
    depth = 0
    for line in text.split("\n"):
        s = line.strip()
        if not s:
            continue
        if s.startswith("#"):
            out.append(s)  # preprocessor at column 0
            continue
        code = _strip_strings(s)
        lead_close = len(code) - len(code.lstrip("}"))
        emit_depth = max(0, depth - lead_close)
        out.append(" " * emit_depth + s)
        depth += code.count("{") - code.count("}")
        if depth < 0:
            depth = 0
    return "\n".join(out) + "\n"


parts = []
parts.append("// ICPC 2026 — single-file submission (strategy flags baked in).\n\n")
for inc in collect_sys_includes():
    parts.append(f"#include <{inc}>\n")
parts.append("\n")

for f in FLAGS:
    parts.append(f"#define {f}\n")
parts.append("\nnamespace icpc2026 {\n")

# Headers + all units except Solution.cpp live inside one outer namespace.
for h in HEADERS:
    b = body(h, keep_using=False)
    parts.append(b)
    parts.append("\n\n")
for u in UNITS:
    if u == "Solution.cpp":
        continue
    b = body(u, keep_using=False)
    parts.append(b)
    parts.append("\n\n")
parts.append("}\n\n")

# Solution.cpp (holds main()) is emitted at GLOBAL scope so the linker finds
# main; its `using namespace icpc2026;` is kept to resolve the namespace names.
sol = body("Solution.cpp", keep_using=True)
parts.append(sol)
parts.append("\n")

final = minify("".join(parts))
# HARD INVARIANT (D-061): Codeforces rejects submissions over 65,535 chars.
# Fail the generation loudly instead of letting a source edit silently bust
# the cap — the next edit must then shrink (strip more dead source) or trim.
CF_MAX = 65535
if len(final) > CF_MAX:
    raise SystemExit(
        f"FATAL: submission would be {len(final)} chars (CF limit {CF_MAX}). "
        f"Trim {len(final) - CF_MAX} chars before submitting (see "
        f"minify() in this script and D-060/D-061).")
OUT.write_text(final)
print(f"wrote {OUT} ({len(final)} chars, CF limit {CF_MAX}, margin {CF_MAX - len(final)})")
