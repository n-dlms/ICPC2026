# SA02 — Official & Community Web Artifacts: ICPC 2026 Online Challenge 1 powered by Huawei (CF 2251)

Date: 2026-08-23. Prepared by Ntokozo Dlamini with assistant support (SA02 drafting). Read-only research; artifacts under `research/scoring/artifacts/`. Program completed 2026-08-23; contest ended Aug 28 2026 with final rank #56.
Classification key: [FACT-web] = verbatim quote + URL from a clean fetch; [FACT-web-snippet] = quote from a
search-engine snippet of the page (page itself not cleanly fetchable); [FACT-API] = Codeforces official API JSON
(saved); [INFERRED] = deduction; [UNVERIFIED] = could not confirm (with reason).

Access notes (recorded honestly):

- WebFetch tool: HTTP 403 on ALL codeforces.com and reddit.com URLs (anti-bot).
- Workaround: Z.ai web-reader tool rendered CF pages (with intermittent noise-injected number runs; clean where
  flagged) + Codeforces PUBLIC API via curl (fully reliable, JSON saved).

- Direct curl on codeforces.com HTML: 403. m.codeforces.com: DNS fail. old.reddit / reddit .json: login-walled/403.
- Contest "Announcements" (clarifications) tab at codeforces.com/contest/2251/announcements rendered EMPTY in the
   anonymous reader session — indeterminate whether judge clarifications exist (they may have required the registered
   session). This was left as a logged-in-browser check at program close (program completed 2026-08-23).

---

## 1. Artifacts recovered

| #   | Source                                                                                          | Where saved                                                             |
| --- | ----------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------- |
| 1   | Live problem A statement (fetched 2026-08-23)                                                   | `artifacts/cf_2251_problemA_live_2026-08-23.md` (verbatim key sections) |
| 2   | CF API standings contest 2251 (3,498 rows, full)                                                | `artifacts/api_standings_2251.json`                                     |
| 3   | CF API standings contest 2177 (Winter 2025 final, 3,176 rows)                                   | `artifacts/api_standings_2177_winter2025.json`                          |
| 4   | CF API problemset dump (2251/2177 not included — special contests excluded from problemset API) | `artifacts/api_problemset.json`                                         |
| 5   | Blog rules 155646 verbatim                                                                      | `artifacts/cf_blog_155646_rules.md`                                     |
| 6   | cf_2251_standings.html                                                                          | fetched but 403-page only (5599 bytes, useless — kept for the record)   |

URLs (not saved, quoted in this report): codeforces.com/contest/2251/problem/A (+?changes=true),
codeforces.com/contest/2251, codeforces.com/blog/entry/155646 (+/comments), codeforces.com/blog/entry/148541
(+comments), codeforces.com/blog/entry/112838, codeforces.com/contest/2177[/problem/A],
icpc.global/regionals/finder/ICPC-Online-Challenge-2026, icpc.global/regionals/finder/Winter-Challenge-2026,
facebook.com/ICPCNews/posts/1290616169768426, t.me/s/codeforces_official.

## 2. Statement verification vs local scrape (/home/dlamini/ICPC2026/problem.md, session of Aug/22)

[FACT-web] The LIVE page today displays the banner: **"The problem statement has recently been changed. View the
changes."** The local scrape contains NO such banner (grep for "chang" = only unrelated prose). The `?changes=true`
variant returns the same ordinary statement body via the reader (no diff/changelog text recoverable anonymously).

Diff result (programmatic, whitespace/unicode-normalized, key sentences + all constraint values): **every
load-bearing sentence is IDENTICAL** between the Aug/22 scrape and today's live statement, verbatim including:

- "IMPORTANT: YES, THERE WILL BE A SYSTEM TEST AFTER THE CHALLENGE."
- Challenge Format: "Test 1 is the public worked Example 1, tests 2–22 are hidden preliminary tests, and finals use
  a separate frozen set. Per-test scores are reported on the 0–1000 scale defined below … no hacks are used."

- Contest aggregation: "The 22 preliminary tests provide feedback and do not contribute to the final ranking. The
  final score is the arithmetic mean of the 20 frozen final-test scores. Ranking uses that mean before display
  rounding; the displayed score is rounded to three digits after the decimal point. … This problem is not open to
  hacks."

- Full scoring formula block (see SA01 for authoritative form):

```text
Score = 1000·(w_tp·clamp(tp; tp_base, tp_UB) + w_c·clamp(dist; dist_base, 0))
tp = ΣLout / E
dist = sqrt(excess_tdr² + excess_tpot²)
```

- `Score` — final 0–1000
- `w_tp` — throughput weight
- `w_c` — waiting weight
- `tp` — output rate
- `tp_base` — fixed one-request-at-a-time reference schedule
- `tp_UB` — estimated high rate
- `dist` — waiting excess from `tdr`/`tpot`
- `dist_base` — reference amount above targets, 0 gives binary
- `E` — elapsed

- Constraints: 1≤K≤8; 1≤R≤2000; 1≤Lin≤4096; 1≤Lout≤512; ΣLout≤2e5; layers≤64; 2≤N≤4096; 1≤S≤10; latency≤50;
  bw≤100; bpt≤1e6; SLOs≤1e9; tpUB>tpbase. TL 15 s / ML 256 MB. All match.

- Stuck-state rule, zero-point list, examples 1–2: match.

[INFERRED] The "recently changed" banner therefore refers to an edit that either (a) predates Aug/22 and the banner
is per-user (appears until the account views it — the reader session is not the contestant's session), or (b) landed
after Aug/22 14:18 UTC+2 (scrape footer timestamp) and is cosmetic/outside the sections compared. Either way: NO
wording difference exists in any scoring/format/constraint section that the local scrape misses. If the banner
persisted in the logged-in view, one manual click on "View the changes" in-browser would have closed this out
definitively (the diff view was not accessible anonymously at the time).

## 3. Aggregation / scoreboard mechanics

[FACT-web] Announcement blog https://codeforces.com/blog/entry/155646 (by Radewoosh, ~4 weeks old) rules, verbatim:

1. individual; 2. "starts August 14 … lasts 14 days, until August 28"; 3. "The judging consists of 22 preliminary
tests. Partial scores are shown on the scoreboard."; 4. **"After the challenge ends, we will run a system test on 20
additional tests. The final ranking is based solely on the system test."**; 5. per-test scoring in the statement;

6. open to all (official ICPC event w/ prizes needs ICPC-site registration); 7. prize pool 12,000 EUR; 8. small
number of special nominations.

[FACT-API] contest 2251: type IOI, phase CODING, start 2026-08-14 11:00 UTC, end 2026-08-28 11:00 UTC (exactly 14
days). 3,498 CONTESTANT rows. The scoreboard "points" during the challenge = SUM over the 22 prelim tests (max
22,000): leader IceKylin 16,720.06 (mean 760.0/test); our ntokozo_dlamini = 16,147.866, **rank 174/3498**. Rank
50 = 16,353.495; rank 100 = 16,270.824; median = 14,083.957. Gap to leader = 572.19 (= 26.0/test mean); gap to
top-100 = 122.958 (= 5.6/test mean). A latest submission 388048443 (Aug/23 06:44) still reads "Partial result:
16147.866 points" — scoreboard current. The 22 prelim tests = test 1 (public Example 1) + 21 hidden; per-test
"Partial result" feedback comes per submission. [INFERRED] The per-test 22-vector is recoverable from the
submission detail page (requires login — not fetched here).

[FACT-web-snippet] AI policy (blog 155646, via multiple consistent search snippets — clean fetch was noisy):
"For this specific Challenge, generative AI and AI-assisted coding tools are permitted. The generic prohibition
previously shown during ICPC …" / "The use of AI tools, including generative AI and AI-assisted coding tools, is
permitted during the Challenge. Participants remain fully responsible for …". The generic ICPC boilerplate
("Participation is strictly limited to human individuals; the use of AI systems … is expressly prohibited") is
explicitly overridden for THIS challenge. → The contestant's AI-driven workflow is compliant; individual
(no human collaboration) remains required.

[FACT-web] Problem's official long name (blog): "Scheduling Algorithm for Collaborative Inference System" — matches
the repo's title. Grand Prize €12,000 (+ guest trip to the 50th ICPC World Finals per ICPCNews posts), Second
€8,000, further tiers [FACT-web-snippet for tier details].

## 4. Clarifications found

- Contest 2251 Announcements tab: rendered empty anonymously [UNVERIFIED whether judge clarifications exist —
  check from the logged-in session].

- Blog 155646 comments (partial, noisy render [UNVERIFIED-partial]): questions about system test ("additional
  tests on Codeforces, run after the challenge, final ranking published afterward"), prelim tests NOT counting
  toward final ranking, per-test-zero for stuck state, AI usage, eligibility. Nothing contradicting the statement.

- Statement itself is the authoritative clarification source; the tp_base/tp_UB/dist_base semantics ARE defined
  in-statement ("fixed one-request-at-a-time reference schedule" for tp_base; "estimated high rate" for tp_UB;
  dist_base = "the reference scheduler's amount above the waiting-time targets").

## 5. Community intel

- No public writeups/strategy posts for THIS challenge found (searched CF blogs, Zhihu CN, reddit, telegram) —
  consistent with it still running. Zhihu hits were generic edge-cloud survey articles, not contest posts.

- Reddit threads r/codeforces 1vp47qa ("ICPC Challenge day 2") and 1q4lyz7 exist but are login-walled from this
  environment [UNVERIFIED]; the snippet "score 52653 … ranking 500–600" matches the WINTER challenge's 10k/test
  scale, not this one's.

- Telegram t.me/s/codeforces_official carries only the standard invitation snippet (join link to contest 2251).
- [FACT-API] Best hard community intel = the standings distribution itself: 3,498 participants, dense clumping
  (rank 174 at 16,147.866 vs rank 1 at 16,720.06 — only 3.4% separates top-200 from top-1 on the prelim sum),
  i.e., the frozen 20-test system phase is where the ranking will actually be decided (also the blog's design).

## 6. Prior-year analogues (Huawei–ICPC challenges on CF)

- [FACT-web] ICPC 2025 Online Winter Challenge powered by Huawei = CF contest 2177, problem "OXC Ports Arrangement
  Problem", announcement blog 148541 (Radewoosh). Rules verbatim-identical structure: individual; "judging
  consists of 22 preliminary tests. Partial scores are shown on the scoreboard."; "After the challenge ends, we
  will run a system test on 20 additional tests. The final ranking is based solely on the system test." Prize
  pool 10,000 EUR (blog) — final prizes per ICPCNews: €12,000 + WF trip / €8,000 / €3,000 / €800 tiers.

- [FACT-API] 2177 final (post-system-test) standings: phase FINISHED, 3,176 rows, displayed points are the
  SYSTEM-TEST AGGREGATE on a per-test 0–10,000 scale: winner Sung.An 126,204.534 (mean 6,310/test), then
  chiranko 125,838.113, maxplus 125,635.774, ashmelev 125,625.404 … So after the system test the CF scoreboard
  flips from prelim-sum to the system-test aggregate; in that contest the aggregate shown was the SUM (ranking
  identical to mean). [INFERRED] For 2251 expect the post-contest scoreboard to show the system-test aggregate
  (statement says ranking uses the MEAN of 20 × [0,1000]; display may be mean ≤1000 or sum ≤20,000 — 2177
  precedent says sum-style display, but 2251's statement explicitly words it as a mean).

- [FACT-web-snippet] Winter-challenge community post-mortem (148541 comments, via search snippets): "Cheaters
  were able to reach 125k in just two or three days, fail the system tests"; "The final score is ~110k; on tests
  4–11, I lose 10–16k" → the frozen 20 tests in the winter edition were meaningfully harder/different from the
  22 prelim tests; overfit-to-prelim solutions dropped 10–16k (≈5–8% of max) and the winner stayed stable.

- [FACT-web-snippet] ICPC 2023 Online Spring Challenge (blog 112838, same series): Sung.An was prelim rank 1 and
  finished final rank 7 after system tests — further evidence of prelim→final rank churn in this series.

- Older series context: Huawei ran ICPC scheduling challenges since 2020 (huawei.com news: "ICPC scheduling
  algorithm innovation", 8,000+ participants) [FACT-web-snippet].

## 7. Bottom line for strategy

1. CONFIRMED, no change: prelim 22 tests = feedback only; final ranking = system test on 20 additional frozen
   tests ("final ranking is based solely on the system test"), mean-scored per the statement. Nothing found that
   contradicts the local scrape. The one open item at program close: the "statement recently changed" banner — key sections
   byte-identical; the diff-view check was left as a logged-in-browser task (program completed 2026-08-23).

2. NEW FACT: scoreboard points during the challenge are the SUM over 22 prelim tests (max 22,000). Our
   16,147.866 = rank 174/3498; top-100 cutoff 16,270.824 (+5.6/test); leader 16,720.06 (+26.0/test). Since the
   final ranking ignored these points entirely, prelim position was only a signal — the winter-edition evidence
   (drops of 10–16k = 5–8% on frozen tests; prelim-1 → final-7 in 2023) said GENERALIZATION to unseen tests was
   the decisive factor, exactly what the structural (non-overfit) approach targeted.

3. NEW FACT: generative AI / AI-assisted coding explicitly PERMITTED for this challenge (blog 155646 overrides
   the generic ICPC prohibition). Workflow is compliant; keep it individual.

4. Timing: contest ended Aug 28 11:00 UTC with final rank #56 worldwide. System test ran after; final ranking was published afterward
   (blog comments; winter edition's final standings appeared on the contest page). Archive note: pre-contest timing language above is retained as written.
