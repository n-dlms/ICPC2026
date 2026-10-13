# Heuristic Programming, Explained Without Code

> Post-contest guide. Author: Ntokozo Dlamini — final rank #56 worldwide (top 1.3%)
> in the ICPC 2026 Online Challenge 1 powered by Huawei (Codeforces 2251-A).
> Contest ended Aug 28 2026. AI sped up drafting and checks; all ideas and
> decisions are mine.

## The one-sentence version

Heuristic programming is solving a problem where there is no perfect answer key —
only better and worse answers — and your job is to build a good decision-maker
that scores as highly as possible.

## Exact problems vs heuristic problems

Most people meet their first kind of computer problem at school:

- **Exact problem:** `2 + 2 = ?` There is one right answer: `4`. The program is
  either correct or wrong.

- **Heuristic problem:** you run a busy hospital waiting room for a day. Patients
  keep arriving. You have limited doctors, beds, and time. There is no single
  "correct" schedule. There are only schedules that are kinder, faster, fairer,
  or more wasteful than others.

My contest problem was the second kind.

## The story I was given

Imagine this, with no computers involved:

- You own **one small kitchen** (the "edge") and can borrow up to **8 big
  catering kitchens** (the "clouds").
- Customers (requests) keep phoning in orders all day — up to 2,000 of them.
- Every order has two stages:
  1. **Prep** — read the order and get it started.
  2. **Serve** — bring out plates one by one, sometimes 1 plate, sometimes 512.
- You do not know how many plates an order will need until it is finished.
- There is **one shared road** into the big kitchens and **one shared road**
  back. Only one van can use each road at a time, and vans queue up.
- Every start of a task costs you a setup delay, called `S`.

You stand at the door all day. Every time a kitchen becomes free, you must point
at the next job instantly. You cannot see the future. You cannot pause time.

At the end of the day you get a score from `0` to `1000`:

```text
Score = 1000 * (w_tp * throughput_part + w_c * waiting_part)
```

In plain words:

- `throughput_part` — did you get a lot of plates out per minute, from the first
  phone call to the last plate served?
- `waiting_part` — on average, did customers wait about as long as they were
  promised, or much longer?

Different test days weigh those two parts differently. Some days care only about
speed. Some days care only about waiting. Most days care about a mix.

Twenty-two such days counted for practice feedback. Twenty unseen days decided
the final ranking. My practice total was `16,147.866 / 22,000`. My final rank
was **#56 in the world**.

## Why there is no perfect solution

Three reasons make perfection impossible:

1. **You decide without knowing the future.** How many plates will this order
   need? When will the next 100 orders arrive? You must guess from patterns.

2. **Everything fights over the same resources.** The small kitchen, the big
   kitchens, and the two roads are all shared. Helping one order always delays
   another somewhere.

3. **The math explodes.** With 2,000 orders and millions of possible timings,
   trying every combination would take longer than the universe has existed.
   This class of puzzle is called NP-hard: we can check a schedule quickly, but
   we cannot prove the best one quickly.

So you do not hunt for "the answer." You build a **rulebook that usually decides
well**. That rulebook is called a **heuristic** — from the Greek for "I find."
A practical rule that finds good answers fast, without promising the best one.

## What my rulebook actually did

In everyday terms, my program kept asking five small questions, thousands of
times a day:

1. **Which big kitchen gets this new order?**
   Send it where it will finish earliest, not just where the line looks shortest
   right now. Keep heavy orders away from each other so one slow giant does not
   block a whole kitchen.

2. **Should I split the prep into pieces?**
   Usually no — each extra piece pays the setup cost `S` again. Only split when
   the small kitchen is busy and a big kitchen sits idle, so both can work at
   once.

3. **Should I serve plates in groups?**
   Yes, when grouping saves more setup time than it costs in waiting. Big groups
   waste less setup per plate but make early plates wait for late ones. My
   program grew each group only while the next plate still paid for itself.

4. **Should I wait a moment before serving, to let one more order join?**
   Sometimes. Waiting is free only if someone is already on their way and nobody
   is starving. My program priced the gain (saved setup) against the loss
   (extra waiting) and waited only when the gain won.

5. **What matters most today?**
   Read the day's weights at opening time. Pure-speed day? Batch big, ignore
   promises. Pure-promises day? Serve small groups fast, protect every arrival.
   Mixed day? Blend.

That is all of heuristic programming: small, fast, explainable rules, applied
over and over, tuned by evidence.

## How I knew a rule was good

Because there is no answer key, I built my own exam hall before trusting anything:

- **A local judge** that replayed days exactly like the official one and scored
  with the same formula.
- **101 practice days:** 27 everyday cases, 47 nasty trap cases, 21 cases shaped
  like the real contest, 6 speed cases.
- **A decision log (D-001 to D-162):** every rule change recorded with what I
  tried, what else I considered, what the evidence said, and how to undo it.
  No decision meant no code.
- **Safety checks every time:** nothing crashes, the score oracle matches the
  judge to about `1e-10`, sanitizer builds are clean, and a new rule ships only
  if it beats a frozen snapshot without breaking anything else.

Rejected ideas stayed in the log on purpose. Knowing what lost is part of
knowing why what won is trustworthy.

## Why #56 matters

Out of thousands of entrants, top 1.3% means the rulebook generalized. It did
not memorize 22 practice days — the finals were 20 unseen days. General rules
about batching, balancing, and waiting survived new arrivals, new tables, and
new weights.

That is the skill heuristic programming teaches: deciding well under
uncertainty, with limited time, where every choice has a price.

## If you remember three things

- **Exact programming** asks "is it right?" **Heuristic programming** asks
  "how good is it, on average, under pressure?"
- A heuristic is a fast rulebook, not a perfect proof.
- You improve it the way I reached #56: guess a rule, test it fairly, keep what
  survives, write down why, repeat.

No coding background needed to understand that loop. The code in `src/` is just
that loop, running very fast.
