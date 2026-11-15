# How the solver works

This document explains the exact solver in `src/dp.cpp` (`solve_dp`): what
it counts, why the counts give exact hit probabilities, and how it gets
through ~3.5 · 10¹⁴ fleet configurations of a 10x10 board in under half a
second without enumerating them. The model it works on is built in
`src/core.cpp`; `src/brute.cpp` is a plain backtracking solver used as the
reference in tests.

## 1. What is computed

A **configuration** is a set of ship placements that

* places exactly the remaining fleet (each ship type the given number of
  times, in any of its distinct rotations if rotation is allowed),
* has no two ships overlapping or touching, not even diagonally,
* covers every hit, avoids every miss, and keeps clear of sunk ships and
  their surroundings.

Identical ships are interchangeable: a configuration is a *set* of
placements, so swapping two identical ships does not make a new one.

The solver returns

* `total`: the number of configurations, and
* `cell_counts[i]`: the number of configurations with a ship on cell `i`.

If every configuration consistent with what is known is equally likely, the
probability that cell `i` holds a ship is exactly `cell_counts[i] / total`.
That is the percentage the tools show, and the best shot is the unknown
cell with the highest count.

The empty 10x10 board with the game fleet (12 ships) has
348,089,088,216,792 configurations. Enumerating them one by one at a billion
per second would take four days; the solver counts them without listing
any.

## 2. Preprocessing: the Problem (`make_problem`)

The game state (misses, hits, sunk ships) is turned into a `Problem`:

1. **Sunk ships** are checked (their cells must form the declared ship, must
   not lie on a miss, must not touch each other), removed from the fleet,
   and their cells plus all 8 neighbours become **forbidden** for other
   ships. Their own cells are reported as certain (count = `total`).
2. **Misses** are forbidden cells.
3. **Hits** not explained by a sunk ship must each be covered by some
   remaining ship. A hit next to a sunk ship is a contradiction.
4. Every **legal placement** of every remaining ship type is listed: each
   distinct rotation, at each position where it fits on the board and
   covers no forbidden cell. A placement whose surroundings contain a hit
   it does not cover is dropped too: that hit could never be covered by
   another ship, because that ship would touch this one.
5. Each placement records its cells, its **halo** (cells plus neighbours)
   and its **anchor**: its first cell in row-major order (top row first,
   left to right).

A state that is self-contradictory is flagged as infeasible with the first
reason found, and `bin/play` / `bin/tui` report it.

## 3. The reference solver (`src/brute.cpp`)

Backtracking over placements: for each ship type, choose placements in
increasing index order (so identical ships are not permuted), skip any that
hit the halo of an already placed ship, and at the leaves check that all
hits are covered. It is obviously correct and hopeless beyond small boards,
which is exactly what the tests need: the DP is compared against it on
hundreds of random small instances.

## 4. The frontier dynamic program

### 4.1 Scanning cells in order

Cells are visited one by one in row-major order, and **each ship is decided
at its anchor**: when the scan reaches cell `a`, it decides whether some
ship has its first cell at `a`, and which one.

Every configuration corresponds to exactly one sequence of such decisions
(ships never overlap, so no two share an anchor), so counting decision
sequences counts configurations, each once. Swapping identical ships is not
a different sequence, because a decision only says "a ship of type t" goes
here.

After cell `a` has been decided, the past matters to the future in only two
ways:

* which cells **ahead** are already taken by ships placed earlier (or by
  their no-touch surroundings), and
* how many ships of each type are **still to be placed**.

That pair is the DP **state**. Huge numbers of different partial
configurations share the same state, which is where the speed comes from:
the solver counts how many ways lead to each state rather than following
each way separately.

### 4.2 The grown board: no-touch as one bitmask test

The no-touch rule involves the cells *around* a ship, including above and to
the left, which the scan has already passed. The trick is to grow every ship
by one cell to the right and one cell down, onto a board one column wider
and one row taller ((W+1) x (H+1), "grown cells"):

```
ship          grown shape (G = grown cell)
# # #         G G G G
              G G G G
```

Two ships touch or overlap **if and only if** their grown shapes overlap.
(Cells `a` and `b` are equal or neighbours exactly when `a + u = b + v` for
some offsets `u, v` in {0,1}²; growing a shape adds exactly those offsets.)
So the whole no-touch rule becomes: a new ship's grown shape must not
overlap any grown cell already taken. The extra column and row exist so
that ships along the right and bottom edges have room to grow.

Grown cells are numbered row-major on the grown board, `0 .. G-1` with
`G = (W+1)(H+1)` (121 on 10x10). The scan visits every grown cell; ships can
only be anchored on real cells, the padding column and row just pass
through.

### 4.3 The mask

The "cells ahead that are taken" part of the state is a bitmask **relative
to the current grown cell `g`**: bit `j` set means grown cell `g + j` is
taken. Everything a placement anchored at `g` can touch lies within its
**reach** (the largest offset from anchor to its last grown cell, over all
placements), and cells further ahead are always still free. On 10x10 the
reach is 56 grown cells (a 57-bit window), so the mask fits in a 64-bit
word; boards whose reach is 64–127 use 128-bit masks, wider ones are
rejected.

Because the mask is relative, moving from cell `g` to `g + 1` is a right
shift by one.

### 4.4 Transitions

Layer `g` holds the states just before grown cell `g` is decided. From a
state (mask `m`, remaining fleet `f`), cell `g` produces:

* **No ship anchored here:** new mask `m >> 1`, same fleet. Not allowed if
  `g` is a hit and bit 0 of `m` is clear: an uncovered hit can never be
  covered later, because every later ship is anchored further on. (A set
  bit 0 on a hit cell always means a ship cell: placements whose
  surroundings touch a hit without covering it were removed in
  preprocessing.)
* **A ship of type `t` anchored here,** for each placement of type `t` with
  anchor `g`, if `f` still has a ship of type `t`: allowed when bit 0 is free
  and the placement's grown shape `s` (relative to its anchor) does not
  overlap `m`. New mask `(m | s) >> 1`, fleet `f` minus one ship of type `t`.

States arriving at the same (mask, fleet) are merged and their counts
added. After the last grown cell, the only state that matters is the empty
fleet: every ship placed.

### 4.5 Pruning by area

Each ship covers a fixed number of grown cells (its grown area; e.g. 4 for a
single-cell ship). All future grown shapes lie at or after the current cell,
on cells not yet taken, so a state is **viable** only if

```
sum of grown areas of the remaining ships  <=  G - g - popcount(mask)
```

Non-viable states are never stored. This is tight in practice: the full
czech fleet covers 108 of the 121 grown cells of a 10x10 board, so states
that waste more than 13 cells early die immediately.

### 4.6 Lookahead and canonical masks

Many states differ only in cells that no future ship can use anyway, and
many are already doomed by a hit that can no longer be covered. A few 64-bit
operations per state detect both (`Lookahead::canon`).

From the free cells `F = ~m`, compute

* `V`: cells that some placement anchored at `g` or later, of a ship type
  still in the fleet, could use as a ship cell (precomputed per layer and,
  for up to 10 ship types, per set of types left);
* `P = V & F & F>>1 & F>>GW & F>>(GW+1)` (`GW` = grown width): cells that
  can still become a ship cell. A ship cell at `z` grows into `z`, `z+1`,
  `z+GW` and `z+GW+1`, so all four must be free;
* `C = P | P<<1 | P<<GW | P<<(GW+1)`: every grown cell a future ship could
  still cover.

Then:

* a state with an uncovered hit outside `P` is **dead** and dropped;
* every real grown cell outside `C` is **filled in** (marked taken).

The fill is what pays off. States that differed only in unusable cells now
have the same mask and merge, and filled cells count as wasted area in the
viability bound. On the empty 10x10 board this cuts the stored states from
298 million to 80 million.

Why it is safe: a placement that is legal for `m` never uses a cell outside
`C`, so `m` and its canonical form have exactly the same completions.
Filling never removes a cell from `P`, so canonicalizing twice changes
nothing. `P` only shrinks as the scan advances, so the forward pass (which
stores canonical masks) and the backward pass (which looks them up) always
agree. Bits beyond the mask window are always free; the shifts bring in ones
there.

### 4.7 Grouping by remaining fleet

The remaining fleet is encoded as one number (mixed radix: digit `t` is the
number of ships of type `t` left; the game fleet has 2·2·3·4·6 = 288
values). A layer is an array of **groups**, one per fleet value, each
holding masks and counts.

A destination group `d` receives states only from group `d` itself (no ship
placed) and from groups with one more ship of some type. So every
destination group of layer `g + 1` can be built independently from layer
`g`, by one thread, with no locks. The backward pass is parallel in the same
way.

### 4.8 Forward and backward passes

* **Forward:** `fwd(state)` = number of ways to reach the state from the
  start (empty mask, full fleet). Built layer by layer as above.
* **Backward:** `bwd(state)` = number of ways to complete the state into a
  full configuration: 1 for the empty fleet after the last cell, otherwise
  the sum of `bwd` over the state's transitions.

Then:

* `total = bwd(start)`;
* for every transition that places a placement `p` (from state `s` to `s'`),
  `fwd(s) · bwd(s')` configurations contain `p`. Summed over all such
  transitions this is the number of configurations containing `p`;
* `cell_counts[i]` is the sum of that over all placements covering cell `i`.

The backward pass recomputes each transition's destination mask and looks
it up (canonicalized the same way) in the next layer through a hash index.
States with `bwd = 0` are dead ends: they were reachable but cannot be
completed (7.9 million of the 80 million on the empty game board).

### 4.9 Transposing

The mask window is about one row of the grown board plus the ships' vertical
reach, so a narrow scan is cheaper. If the board is wider than tall, it is
transposed (rows and columns swapped, placements remapped) before solving
and the per-cell counts are transposed back.

### 4.10 Exact counts

State counts are 64-bit, and every addition of them is checked for overflow;
on overflow the whole solve is repeated with 128-bit counts (and if those
overflow too, it fails with `std::overflow_error`). Per-placement weights
and per-cell counts are always 128-bit; they are bounded by the total.

## 5. Memory

The backward pass needs `fwd` of every layer, so the forward pass keeps all
122 layers (one per grown cell plus the final one; 80 million states on
the empty game board). Stored naively that is 16 bytes per state (64-bit
mask + 64-bit count), about 1.3 GB.

Layers not currently being written or read are **packed**:

* each group is sorted by mask and stored as varint (LEB128) deltas between
  consecutive masks, each followed by its count as a varint;
* the count is left out when bit 0 of the mask is set: no ship can be
  anchored on a taken cell, and the backward pass only needs `fwd` for
  placements.

That is about 2 bytes per state, 160 MB for the empty game board. The
backward pass decodes each layer in order as it goes.

`DpOptions::max_states` sets a memory budget (by default 1.5 · 10⁹ states'
worth at 16 bytes); a solve that would store more aborts with an error
instead of exhausting memory.

## 6. Implementation details that matter for speed

* **Building a group:** about 85 % of the no-ship transitions arrive in
  increasing mask order (the no-ship transition shifts sorted sources, and
  shifting keeps the order). They are appended to a sorted run directly.
  Ship placements and the few states canonicalization moves out of order go
  to a hash table, which is sorted and merged into the run at the end.
  Packing needs the sorted order anyway. * **Threads:** a persistent thread
  pool runs the four parallel loops per grown cell (build, pack, backward,
  index). Layers under 16k states run on the calling thread: waking the
  workers would cost more than the work. * **Fast varints:** 64-bit varints
  below 2⁵⁶ are encoded branch-free with a few shifts and masks.

See [OPTIMIZATIONS.md](OPTIMIZATIONS.md) for the measured effect of each
optimization and the ideas that were tried and not kept.

## 7. Numbers (empty boards, Apple M4 Pro, 12 threads)

| | game fleet 10x10 | czech fleet 10x10 |
|---|---|---|
| configurations | 348,089,088,216,792 | 979,492,952,432 |
| states stored (all layers) | 80 million | 230 million |
| largest layer | 2.5 million | 7.5 million |
| dead states | 7.9 million | 79 million |
| packed storage | 159 MB | 493 MB |
| time | 0.39 s | 1.25 s |

`./bin/bench` prints these statistics for every benchmark scenario.

## 8. How it is verified

`make test` / `make test-all` (`tests/`):

* **Hand-counted boards:** small boards whose counts can be checked on paper,
  including misses, hits, sunk ships, and identical ships not being counted
  twice.
* **DP vs brute force:** hundreds of random small instances (random fleets,
  shapes, misses, hits, sunk ships) must give identical totals and per-cell
  counts.
* **Invariants:** per-cell counts sum to `total` times the fleet's cell
  count; an empty square board gives counts invariant under rotation,
  mirroring and transposition; 64/128-bit counts, thread counts and
  transposition all agree.
* **Golden output:** the slow test reproduces the original solver's output on
  its 9x8 setup (`tests/golden/`).
* **Limits:** the memory budget and worker exceptions surface as errors.
