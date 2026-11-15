# Battleships

An exact solver for "where are the ships?" in Battleship-style games. Given a
board, a fleet and the shots so far (misses, hits, sunk ships), it counts
**every** fleet configuration consistent with the state and, for every cell,
how many of those configurations have a ship there. That ratio is the exact
hit probability for each cell.

Rules: ships are polyominoes, may be rotated by 90° (optional), may not
overlap and may not touch, not even diagonally. Identical ships are
interchangeable, so swapping two identical ships is not a new configuration.

## Build, test, benchmark

Needs a C++17 compiler (Apple clang or GCC).

```sh
make             # builds bin/tui, bin/play, bin/bench and bin/tests
make test        # fast tests
make test-all    # + slow golden-output test on the legacy 9x8 setup (~15s)
make bench       # benchmark scenarios (legacy + 10x10 game fleet)
```

## Playing interactively

```sh
./bin/tui                     # empty 10x10 board, game fleet
./bin/tui game.txt            # load/save game.txt (created on first save)
```

Shows every cell's hit probability and the best next shot, recomputed after
each move, with the remaining fleet beside or below the board. The keys are listed on
screen; `./bin/tui --help` lists the options.

## Playing from a file

```sh
./bin/play examples/midgame.txt          # or pipe the state on stdin
./bin/play --fleet czech state.txt       # other fleets: game (default), czech, legacy
```

State format, one line per row (spaces are ignored):

| char | meaning |
|---|---|
| `.` | unknown |
| `o` | miss |
| `x` | hit |
| `S` | cell of a sunk ship (the ship type is recognised by shape) |

Output: a heatmap of hit probabilities in %, the number of consistent
configurations, and the unknown cell most likely to hold a ship. Exit code 1
means bad input. Exit code 2 means no configuration fits the state, with the
contradiction named when it is a direct one (e.g. a hit touching a sunk ship).

## Fleets (`src/fleets.h`)

| fleet | ships |
|---|---|
| `game` (default, 10x10) | 1 kriznik, 1 petka (5), 2 parnik (T), 3 dvojka (2), 5 jednicka (1) |
| `czech` (stress test) | 1 kriznik, 1 petka, 1 ctyrka (4), 2 parnik, 2 trojka (3), 3 dvojka, 4 jednicka: 14 ships, 40 cells |
| `legacy` | 5, 4, 3, 3, 1, 1, 1 |

```
kriznik  . # . # .     parnik  . # .
         # # # # #             # # #
```

## How it works

* `src/core.*`: the model. Builds the list of legal placements of every
  remaining ship, after removing misses, sunk ships and their surroundings.
* `src/brute.*`: plain backtracking over placements. Slow but obviously
  correct; the tests use it as the reference.
* `src/dp.*`: the fast exact solver, a frontier (transfer-matrix) dynamic
  program:
  * Cells are visited in row-major order and each ship is decided at its
    first cell. The state is (cells ahead already taken, ships still to
    place), and many partial layouts share the same state.
  * Every ship is grown by one cell right and down onto a (W+1)x(H+1) board.
    Two ships touch iff their grown shapes overlap, so the no-touch rule is a
    single bitmask test. The mask only covers the cells from the current one
    up to a ship's farthest reach (57 cells on 10x10), so it fits in 64 bits;
    very wide boards use 128 bits. The grown cells left over also give an
    exact bound on wasted space, which prunes hopeless states early.
  * Lookahead: from the mask, a few bit operations find the cells ahead that
    can still hold a ship. States with a hit that can no longer be covered
    are dropped, and cells no future ship can use are filled in, so states
    that differ only in such cells merge (~4x fewer states).
  * A forward pass (ways to reach each state) and a backward pass (ways to
    complete it) give exact per-placement and per-cell counts.
  * States are grouped by remaining fleet, which makes both passes
    parallel without locks.
  * The board is transposed when that makes the frontier narrower.
  * Counts are exact: 64-bit, retried in 128-bit on overflow.
  * The forward pass keeps every layer for the backward pass. Layers not in
    use are packed: states sorted by mask, stored as varint mask deltas and
    varint counts (~2 bytes per state instead of 16 on 10x10).

## Performance (Apple M4 Pro, 12 threads)

| scenario | time |
|---|---|
| legacy 9x8, fixed orientation | 0.015 s |
| game fleet 10x10, empty board (worst case) | 0.39 s, 0.33 GB |
| game fleet 10x10, after 3 / 10 / 20 / 50 shots | 0.26 s / 0.03 s / 0.02 s / 0.001 s |
| czech fleet 10x10, empty board | 1.25 s, 0.95 GB |

`./bin/bench` prints time, DP state counts, dead states, peak memory and
forward/backward split; `./bin/bench --solver brute` runs the reference
solver instead.

The original hand-written solver is in the git history: `main.cpp` in commit
`2dfcdb7`. It took 8 s on the legacy 9x8 setup, placed ships only in their
given orientation, and supported neither hits nor sunk ships.

[OPTIMIZATIONS.md](OPTIMIZATIONS.md) describes the optimization experiments,
including those that were measured and not kept.
