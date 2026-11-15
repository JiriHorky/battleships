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
make             # builds bin/tui, bin/play, bin/bench, bin/tests and the original bin/main
make test        # fast tests
make test-all    # + slow test reproducing the original program's output (~15s)
make bench       # benchmark scenarios (legacy + 10x10 game fleet)
```

## Playing interactively

```sh
./bin/tui                     # empty 10x10 board, game fleet
./bin/tui game.txt            # load/save game.txt (created on first save)
./bin/tui --fleet czech --size 10x10
```

The board shows every cell's hit probability in %, coloured from cold to
hot, and marks the best next shot with `*`. It is recomputed after every
change, in the background, so the cursor keeps moving while a slow position
is being solved.

| key | action |
|---|---|
| arrows / `h j k l` | move the cursor |
| `x` | hit (press again to clear) |
| `o` | miss (press again to clear) |
| `s` | sunk: turns the group of hits under the cursor into a sunk ship, or back into hits |
| space / `.` / backspace | clear the cell |
| `b` / enter | jump to the best shot |
| `u` | undo |
| `r` | reset the board |
| `w` | save to the state file (`state.txt` if none was given) |
| `q` | quit |

To record a sunk ship, mark all of its cells as hits, then press `s` on one of
them. The ship type is recognised by its shape, and the fleet line at the top
shows what is still afloat. A shape that is not a ship of the fleet, or a
board no fleet layout fits, is reported in red.

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
| `czech` (stress test) | every ship from the original `main.cpp`: 14 ships, 40 cells |
| `legacy` | the fleet active in the original `main()`: 5, 4, 3, 3, 1, 1, 1 |

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
  * A forward pass (ways to reach each state) and a backward pass (ways to
    complete it) give exact per-placement and per-cell counts.
  * States are grouped by remaining fleet, which makes both passes
    parallel without locks.
  * The board is transposed when that makes the frontier narrower.
  * Counts are exact: 64-bit, retried in 128-bit on overflow.

The original solver is kept in `main.cpp` (`bin/main`) for reference. It only
places ships in their given orientation and supports neither hits nor sunk
ships.

## Performance (Apple M4 Pro, 12 threads)

| scenario | original `main.cpp` | now |
|---|---|---|
| legacy 9x8, fixed orientation | 8.0 s | 0.01–0.04 s |
| game fleet 10x10, empty board (worst case) | — | 1.7 s, 4.8 GB |
| game fleet 10x10, after 3 / 10 / 20 shots | — | 1.1 s / 0.07 s / 0.05 s |
| czech fleet 10x10, empty board | — | 6.2 s, 16 GB |

`./bin/bench` prints time, DP state counts, dead states, peak memory and
forward/backward split; `./bin/bench --solver brute` runs the reference
solver instead.
