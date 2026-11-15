# Optimization experiments

Four ways to make the DP solver (`src/dp.cpp`) faster or leaner were tried
side by side, each on its own branch from commit `45f9ce5`. Two of them were
combined and merged; the rest are kept here for reference.

All variants give **identical** totals and per-cell counts on every benchmark
scenario, pass `make test-all`, and were reviewed independently.

## Results

Apple M4 Pro, 12 threads, best of 3, each scenario in its own process (peak
memory = peak RSS). Run one after another on an idle machine.

| scenario | baseline `45f9ce5` | 1 · lookahead | 2 · fit table | 3 · checkpoint | 4 · compact | **1 + 4 (merged)** |
|---|---|---|---|---|---|---|
| game fleet, empty 10x10 | 1.36 s · 4.9 GB | 0.39 s · 1.4 GB | 1.36 s · 4.9 GB | 1.94 s · 1.5 GB | 1.43 s · 0.87 GB | 0.42 s · 0.30 GB |
| game fleet, 3 shots | 0.89 s · 3.3 GB | 0.27 s · 0.95 GB | 0.93 s · 3.4 GB | 1.31 s · 1.2 GB | 0.98 s · 0.64 GB | 0.30 s · 0.23 GB |
| game fleet, 10 shots | 0.064 s · 203 MB | 0.043 s · 60 MB | 0.062 s · 213 MB | 0.091 s · 99 MB | 0.075 s · 69 MB | 0.052 s · 25 MB |
| czech fleet, 5 shots | 1.27 s · 5.2 GB | 0.44 s · 1.6 GB | 1.28 s · 5.2 GB | 1.72 s · 1.8 GB | 1.35 s · 1.0 GB | 0.50 s · 0.42 GB |
| czech fleet, empty 10x10 | 5.29 s · 16.1 GB | 1.26 s · 4.2 GB | 5.33 s · 16.3 GB | 7.76 s · 4.9 GB | 5.42 s · 2.8 GB | 1.34 s · 0.92 GB |

States stored on the empty 10x10 board (game fleet): baseline 298M, of which
63M dead (never completed); with lookahead 80M, of which 8M dead.

## 1. Lookahead pruning and mask canonicalization — merged

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

Branch `worktree-agent-a4c6d450eff2239bc`, commit `54abee8`.

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

Branch `worktree-agent-a3a1323a7ca3b034a`, commit `a92e304`.

## 3. Checkpointing — not merged

Keep only every K-th forward layer (K ≈ sqrt(G/3), 6 on 10x10) and recompute
the missing layers segment by segment during the backward pass. Options
`DpOptions::checkpoint_every` / `checkpoint_above` (only kicks in above a
state threshold) and `bench --checkpoint K --checkpoint-above N`.

Cuts peak memory ~3x but costs 40–50 % more time. Lookahead + compact storage
cut memory further without that cost, so it is not needed today. It stacks
with them and would be the next step if a much larger fleet or board ever
runs out of memory.

Branch `worktree-agent-a3185b4b43c387c63`, commit `3e4d2d7`.

## 4. Compact layer storage — merged

Once a layer is no longer needed by the forward pass, each group is stored
sorted by mask as varint mask deltas plus varint counts; counts are omitted
for states whose mask has bit 0 set (the backward pass never reads them).
The "no ship" part of a group comes out of the forward step almost sorted, so
only the rest is sorted and the two runs are merged. The backward pass
decodes each group in order. The state budget counts packed bytes.

~2 bytes per state instead of 16 (5.7x less memory), 5–7 % slower on its
own.

Branch `worktree-agent-a5d49b5d352a0e6f6`, commit `ca07e37`.

## 1 + 4 combined — merged

Commits `1d2f0c0` and `71bc085`. Canonicalization breaks the guarantee that
the "no ship" run is already sorted, so packing uses the longest sorted
prefix of that run and sorts the rest. 3.2–4x faster than the baseline on the
big boards and 13–17x less memory; slightly slower than lookahead alone
(5–20 %), in exchange for ~4x less memory.

## Ideas evaluated earlier and not built

* **Precomputed tables (up to 100 GB):** the answer depends on every shot on
  the board, so no table can cover the possible game states; and every state
  already solves in under 1.5 s.
* **Symmetry (rotating/mirroring the board):** only the empty board is
  symmetric, and the first shot breaks it. Used as a test invariant instead.
* **Compiler flags:** `-mcpu=native` gave ~5 %, LTO and PGO nothing
  measurable, so the build stays portable.
