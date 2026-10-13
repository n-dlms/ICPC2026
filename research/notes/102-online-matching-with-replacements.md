> **Post-contest archive note (Oct 2026):** Pre-contest literature review completed before the Aug 14–28 2026 contest window. Contest ended Aug 28 2026 — final rank #56 worldwide (top 1.3%). "Port plan" proposals below are preserved as historical working notes; what was adopted or rejected is recorded in `research/notes/experiments-log.md`, `research/notes/rejected-ideas.md`, and `docs/DECISIONS.md`. Author: Ntokozo Dlamini (with assistant support for fetching/summaries/checks). Technical content unchanged.

# 102 — Online Bipartite Matching with Replacements (SAP protocol, b-matching line)

**Claim.** In online bipartite matching *with replacements* (clients arrive one by one; the matching may be changed), the **SAP protocol** — always augment along a **shortest augmenting path** from the newly arrived client to a free server — maintains a **maximum** matching at all times and makes only **amortized O(log²n) replacements** per insertion (total O(n log²n); almost matching the Ω(log n) lower bound; the previous best was O(√n) per server, Bosek–Leniowski–Sankowski–Zych FOCS'14).
The same bound holds for the **capacitated assignment problem** (each server has a capacity u(s) — the b-matching case). A (1−ε)-approximation version runs in O(m/ε) total time. (Bernstein–Holm–Rotenberg, SODA 2018.)

**Relevance.** This is the theory that our *planned bookings* make available to us. In our solver, `edge_occ`/`cloud_occ` window bookings for a request are internal state only — nothing is emitted to the judge until the stage starts. So the admission problem is precisely an **online b-matching with augmentations**: "slots" (tick, resource) with capacities Ecap/Ccap are the servers, requests are the clients, and a request "matched to a slot" = has its stage booked there.
When a higher-priority request arrives and its windows are blocked by lower-priority *planned* bookings, an augmenting path can displace those bookings and re-book them later — legally free, exactly the b-matching re-optimization line. This is the strongest theoretical justification for the "replacements are safe and cheap" idea; doc-05's "transactional acceptance" gets a provable churn bound for free.

**Algorithm sketch.** Servers S (slots), capacities u(s); clients arrive with edge set (all legal start-times for their windows). Maintain maximum b-matching. On arrival of client c: BFS over the alternating graph from c (edges: free capacity forward, booked client backward) until a server with free capacity is found; flip the path (SAP). Bound: every augmentation goes down a shortest path; paths of length > h are few (Lemma 6: ≤ 4n·ln n / h of them), so the total flip cost telescopes to O(n log²n).

**Port plan.** Extend `admit_prefills` (Solution.cpp:123) and `grant_decodes` (Solution.cpp:96): when `free_window` fails for candidate id (lines 140–144 / 113–114), run a **bounded BFS augmenting search** instead of `continue`:

- Nodes: candidate request + a set of *planned-booking owners* whose booked windows overlap the candidate's needed window.
- Edge forward: candidate/owner can be re-booked into a free window (deadline re-checked with the same `est ≤ D && ≤ T` discipline as line 135–139).
- Edge backward: owner's current booking.
- Flip = un-book owner's old windows, re-book at new windows, book the candidate. **Transactional**: validate every re-book exactly like `apply_action_set` (Solution.cpp:214–256) and only accept the whole chain; never displace a *started* stage (phase > WAIT/DW), never displace a request that has no later feasible window.
- Budget: per-tick tranche on BFS nodes (reuse `plan_tranche` machinery, Solution.cpp:195–201); no `clock()`.
- A/B: `-DAUGMENT` vs baseline; theory predicts the *number of replacements* stays tiny (log²-ish), so this must be cheap — the value comes from admission quality on burst profiles where arrival order ≠ priority order.

**Weaknesses / mis-transfers.**

- The guarantee is about **number of replacements**, not about score; our priorities (which client "should" be matched) are exogenous, while the theory assumes all clients are equal and the graph is given. We must supply the priority ourselves (cmp_key), and an augmenting path that flips a *higher*-priority booking for a lower-priority one must be forbidden (path direction: only displace lower-priority owners).
- Their slots are unit vertices with sparse edge sets; ours are *interval* slots (a request can start at any tick in a window) — degree is huge, so per-BFS scanning must be window-bounded (scan only `free_window` over the O(pe+PCLOUD)-length window, not the whole timeline).
- A request occupies **two resource types at fixed offsets** (edge [t,t+pe) and cloud [t+pe+COMM, …)) — this is a *two-layer* b-matching, not the single-side matching of the paper; the churn bound is a heuristic guide, not a proof, here.
- Revoking a *started* stage is an illegal action (judge kills the case) — the theory's "replacements" are unrestricted; ours are restricted to not-yet-started bookings. All displacement must happen before the first emitted action for that stage.
- Worst-case competitive/cardinality framing ignores the 1e-6·latency terms; keep the incumbent greedy plan as fallback when no improving path is found.

**Sources.** Bernstein, Holm, Rotenberg, "Online Bipartite Matching with Amortized O(log²n) Replacements", SODA 2018. arXiv:1707.06063 (`research/papers/1707.06063.txt`; abstract + Theorem 1, Theorem 3, Lemma 6, implementation §5). Lineage: Grove–Kao–Krishnan–Vitter 1995 (degree-2 case); Chaudhuri–Daskalakis–Kleinberg–Lin INFOCOM 2009 (switching cost; random order O(n log n); forest O(n log n)) — author PDF at cseweb.ucsd.edu/~kamalika/pubs/cdkl09.pdf; Bosek–Leniowski–Sankowski–Zych FOCS'14 (DOI 10.1109/FOCS.2014.48, paywalled).
