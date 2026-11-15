# Optimization experiments

Four ways to make the DP solver (`src/dp.cpp`) faster or leaner were tried
side by side, each starting from commit `45f9ce5`. Two of them were combined
and merged (commits `1d2f0c0` and `71bc085`); the other two were not kept in
the repository and are described here for reference.

All variants give **identical** totals and per-cell counts on every benchmark
scenario, pass `make test-all`, and were reviewed independently.

## Results

Apple M4 Pro, 12 threads, best of 3, each scenario in its own process (peak
memory = peak RSS). Run one after another on an idle machine; repeated runs
vary by about ±10 %.

| scenario | baseline `45f9ce5` | 1 · lookahead | 2 · fit table | 3 · checkpoint | 4 · compact | **1 + 4 (merged)** |
|---|---|---|---|---|---|---|
| game fleet, empty 10x10 | 1.36 s · 4.9 GB | 0.39 s · 1.4 GB | 1.36 s · 4.9 GB | 1.94 s · 1.5 GB | 1.43 s · 0.87 GB | 0.42 s · 0.30 GB |
| game fleet, 3 shots | 0.89 s · 3.3 GB | 0.27 s · 0.95 GB | 0.93 s · 3.4 GB | 1.31 s · 1.2 GB | 0.98 s · 0.64 GB | 0.30 s · 0.23 GB |
| game fleet, 10 shots | 0.064 s · 203 MB | 0.043 s · 60 MB | 0.062 s · 213 MB | 0.091 s · 99 MB | 0.075 s · 69 MB | 0.052 s · 25 MB |
| czech fleet, 5 shots | 1.27 s · 5.2 GB | 0.44 s · 1.6 GB | 1.28 s · 5.2 GB | 1.72 s · 1.8 GB | 1.35 s · 1.0 GB | 0.50 s · 0.42 GB |
| czech fleet, empty 10x10 | 5.29 s · 16.1 GB | 1.26 s · 4.2 GB | 5.33 s · 16.3 GB | 7.76 s · 4.9 GB | 5.42 s · 2.8 GB | 1.34 s · 0.92 GB |

States stored on the empty 10x10 board (game fleet): baseline 298M, of which
63M dead (never completed); with lookahead 80M, of which 8M dead.

## 1. Lookahead pruning and mask canonicalization — merged as part of 1 + 4 (`1d2f0c0`)

From the mask of taken cells, compute with a few 64-bit operations which
cells ahead can still become a ship cell (P: the cell and its 3 grown
neighbours are free and some remaining ship type can still use it), and which
grown cells any future ship can still cover (C = P grown by one cell right
and down). Then:

* a state whose uncovered hit is not in P can never be completed: drop it;
* grown cells outside C can never be used, so they are filled in.

Pruning alone changed nothing measurable. The fill is what pays off: states
that differ only in unusable cells become identical and merge, so 3.7x fewer
states are stored, and the filled cells also tighten the wasted-area bound.
Every stored mask and every backward lookup goes through the same `canon()`
function, which keeps both passes consistent.

## 2. Fleet fit table — not merged

Precompute, for every remaining fleet, the last cell from which it can still
be placed on an empty board suffix (ignoring the frontier mask and hits, so
it is a valid relaxation), and drop states that start later. Per-type tables
are cheap; a joint table over whole fleets grows as fast as the DP itself and
was capped.

It prunes 22–36 % of states in late game, where everything already takes
under 50 ms, but only 0.02 % on the big boards: most dead states die in the
middle rows because of interactions with already placed ships and hits, which
a mask-free relaxation cannot see.

## 3. Checkpointing — not merged

Keep only every K-th forward layer (K ≈ sqrt(G/3), 6 on 10x10) and recompute
the missing layers segment by segment during the backward pass. It was
switched on only above a state threshold, so small games were unaffected.

Cuts peak memory ~3x but costs 40–50 % more time. Lookahead + compact storage
cut memory further without that cost, so it is not needed today. It stacks
with them and would be the next step if a much larger fleet or board ever
runs out of memory.

## 4. Compact layer storage — merged as part of 1 + 4 (`71bc085`)

Once a layer is no longer needed by the forward pass, each group is stored
sorted by mask as varint mask deltas plus varint counts; counts are omitted
for states whose mask has bit 0 set (the backward pass never reads them).
The "no ship" part of a group comes out of the forward step almost sorted, so
only the rest is sorted and the two runs are merged. The backward pass
decodes each group in order. The state budget counts packed bytes.

~2 bytes per state instead of 16 (5.7x less memory), 5–7 % slower on its
own.

## 1 + 4 combined — merged

Canonicalization breaks the guarantee that
the "no ship" run is already sorted, so packing uses the longest sorted
prefix of that run and sorts the rest. 2.5–4x faster than the baseline on the
big boards and 12–17x less memory; slightly slower than lookahead alone
(5–20 %), in exchange for ~4.6x less memory.

## Micro-optimizations (profiling pass)

Profiled with `sample` and by switching off parts of the hot loops one at a
time (single thread, game fleet, empty 10x10). Time is spread over the
backward loop (45 %), the forward loop and its hash inserts (35 %), building
the backward index (8 %), packing and sorting (8 %). Per state the work is a
varint decode, ~1.4 `canon()` calls and ~1.4 random hash accesses, about
20 ns in total: no single instruction dominates.

Kept:

* **Persistent thread pool** (`src/parallel.h`). The DP runs four parallel
  loops per board cell, and starting threads for each one cost ~20–40 ms per
  solve whatever its size. Workers now spin briefly, then sleep; a solve
  that finds the pool busy (the TUI can overlap solves) starts its own
  threads as before.
* **Small layers on the calling thread.** Layers under 16k states are not
  worth waking the workers for.
* **`canon()` context hoisted.** Its per-layer constants are loaded once per
  group into a small struct that stays in registers, not reloaded per call.
* **Sorted run in the forward accumulator.** 87 % of states have their
  current cell already taken and make only the "no ship" transition, whose
  output comes in mask order: on the empty board 68M of the 80M no-ship
  candidates (85 %) extend the run, 7.4M repeat its last mask and 3.9M (5 %)
  are out of order. In-order states are appended to a sorted run without
  hashing; only ship placements and out-of-order states go to the hash
  table, which is sorted and merged into the run at the end. The memory
  budget is checked on the merged size.

| scenario | before | after |
|---|---|---|
| game fleet, empty 10x10 | 0.43 s | 0.38–0.39 s |
| game fleet, 3 / 10 / 20 / 50 shots | 0.31 / 0.053 / 0.047 / 0.037 s | 0.26 / 0.030 / 0.022 / 0.001 s |
| czech fleet, empty 10x10 | 1.33–1.39 s | 1.24–1.28 s |

Big boards gain 5–10 %; mid and late game, where thread start-up dominated,
1.2–37x (the legacy 9x8 setups in the README went from 0.04 s to 0.015 s for
the same reason). Peak memory on the game fleet rose from ~0.30 to ~0.33 GB.

Tried and not kept:

* **Backward table with mask and count in one slot**, filled during the pass
  (no separate index build), lookups computed for a batch of 32 states and
  prefetched before probing, branch-free candidate filtering: no measurable
  change at load factor 2/3 against the existing index. A tighter table
  (load 0.8) saved memory but was ~10 % slower.
* **SIMD / inline assembly:** the hot code is a dozen 64-bit bit operations
  next to dependent random memory accesses; 2-lane NEON cannot beat the
  scalar units on that, and once its constants stay in registers `canon()` is
  not the bottleneck.

## Ideas evaluated earlier and not built

* **Precomputed tables (up to 100 GB):** the answer depends on every shot on
  the board, so no table can cover the possible game states; and every state
  already solves in under 1.5 s.
* **Symmetry (rotating/mirroring the board):** only the empty board is
  symmetric, and the first shot breaks it. Used as a test invariant instead.
* **Compiler flags:** `-mcpu=native` gave ~5 %, LTO and PGO nothing
  measurable, so the build stays portable.
