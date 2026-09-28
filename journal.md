# Learning journal

My own notes on what I tried, what confused me, and what I got wrong. Experiment hypotheses go
here **before** I run the experiment, and each one gets a date.

Template for an entry:

```
## YYYY-MM-DD: <title>
What I did:
What confused me / what I got wrong:
What I understand now that I didn't before:
Questions to follow up:
```

---

## 2026-09-28: Session 1, project setup

Facts for reference (written by Claude, for me to rewrite in my own words):
- Picked the design: one `UnionFind<Link, Path>` template, giving 9 union-find combinations plus quick-find.
- Built with MSVC 2022 (already installed with Visual Studio). All tests pass under ASan. Python is not installed yet.

What I did:

What confused me / what I got wrong:

What I understand now that I didn't before:

Questions to follow up:

---

## Hypotheses (write before running each experiment)

> **Status: DRAFTS by Claude, 2026-09-28. No experiments have been run.**
> Before running anything, for each hypothesis: agree with it, change it, or replace it with my own
> prediction, then put my own date in "Confirmed". Where I disagree with a draft, the disagreement is
> worth writing down, because a wrong prediction is good material for "What I learned".
> ai_log.md records that Claude drafted these.

| # | Drafted | Confirmed (by me) | Short form | Result (after) |
|---|---|---|---|---|
| H1 | 2026-09-28 | [ ] | Rank vs size makes no real difference once compression is on | |
| H2 | 2026-09-28 | [ ] | Halving is at least as fast as full compression | |
| H3 | 2026-09-28 | [ ] | Time per operation for rank+compression rises with n even though α(n) is effectively constant | |

### H1: Rank vs size linking with path compression

**Prediction:** On `random_mixed`, `rank_compress` and `size_compress` are within 10% of each other in
median ns/op at every n from 2^10 to 2^24, with neither consistently faster. The same holds for
`rank_halve` vs `size_halve`.

**Reasoning:** Both linking rules give the same O(α(n)) amortized bound and keep height at most log2 n.
Once compression flattens the trees, almost every find is one or two hops whichever rule built them.
Both store one extra `uint32_t` per element, so memory traffic is the same.

**What would refute it:** a consistent gap above 10% at several sizes, or a gap that grows with n.

**Experiment:** `random_mixed`, 2^10 to 2^24, at least 5 reps, median. Also compare hops per operation
once the counters exist. If hops are equal but time differs, the cause is not the algorithm.

### H2: Path halving vs full path compression

**Prediction:** On `random_mixed`, halving is as fast as or faster than full compression (up to about
20% faster) for rank and size linking at every n. Halving does fewer parent writes per find.

**Reasoning:** Full compression walks the path twice (once to find the root, once to rewrite it), and
writes every node on it. Halving walks it once and writes only every other node. The asymptotic bounds
are the same (Tarjan & van Leeuwen 1984), so constant factors decide. With rank or size linking the paths
are short, so halving leaving the tree slightly less flat should cost little.

**What would refute it:** full compression consistently faster, which would suggest the flatter trees
pay for the second pass.

**Experiment:** `random_mixed`, both linking rules, 2^10 to 2^24. Once the counters exist, compare parent
writes and hops per operation. As a side check on `chain` with naive linking: full compression flattens
the whole chain on the first query, while halving needs about log n queries to do the same.

### H3: Theory (α(n) is effectively constant) vs practice (the memory hierarchy)

**Prediction:** For `rank_compress` on `random_mixed`, hops per operation stays roughly flat from 2^10
to 2^24, but median ns/op rises by at least 3× across that range. Most of the rise comes in steps where
the working set outgrows a cache level. At 8 bytes per element (parent plus rank):
- a smaller step around n = 2^17 to 2^18, where 8n bytes (1 to 2 MB) exceeds one core's 1.25 MB L2
- a larger step between n = 2^21 and 2^23, where 8n bytes (16 to 64 MB) exceeds the 24 MB L3 on this
  i9-13900H

**Reasoning:** α(n) ≤ 4 for any n that fits in memory, so the model predicts nearly constant cost per
operation. But random unions and queries touch random array positions, so once the arrays no longer fit
in cache most finds pay a cache miss (roughly 10 to 15 ns from L3, and 80 ns or more from DRAM), which costs far
more than the few hops themselves. The model counts pointer hops, but the hardware charges for memory
misses.

**What would refute it:** ns/op roughly flat (within 1.5×) across the whole range, or rising smoothly
with no visible steps near the cache sizes.

**Experiment:** `random_mixed`, `rank_compress`, n from 2^10 to 2^24 (run with `--max-log 24`), at least
5 reps. Plot ns/op and hops per operation against n on the same x-axis with vertical lines at the L2 and
L3 thresholds. For a follow-up, a sequential-order variant of the workload should reduce the rise if
cache misses are the cause.
