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
each move, and the remaining fleet. The keys are listed on screen;
`./bin/tui --help` lists the options.

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

It prints a heatmap of hit probabilities, the number of consistent
configurations and the best shot. Exit code 1: bad input; 2: no
configuration fits the state.

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

A frontier dynamic program scans the board cell by cell, deciding each ship
at its first cell. The state is (cells ahead already taken, ships still to
place), and the no-touch rule is a single bitmask test thanks to ships grown
by one cell right and down. A forward and a backward pass over these states
give exact counts without listing configurations. Details are in
[ALGORITHM.md](ALGORITHM.md). The code: `src/core.*` (model and legal
placements), `src/dp.*` (the solver), `src/brute.*` (backtracking reference
used by the tests).

## Performance (Apple M4 Pro, 12 threads)

| scenario | time, peak memory |
|---|---|
| legacy 9x8, fixed orientation | 0.015 s |
| game fleet 10x10, empty board (worst case) | 0.39 s, 0.33 GB |
| game fleet 10x10, after 3 / 10 / 20 / 50 shots | 0.26 s / 0.03 s / 0.02 s / 0.001 s |
| czech fleet 10x10, empty board | 1.25 s, 0.95 GB |

The original hand-written solver is in the git history: `main.cpp` in commit
`2dfcdb7`. It took 8 s on the legacy 9x8 setup, placed ships only in their
given orientation, and supported neither hits nor sunk ships.

[OPTIMIZATIONS.md](OPTIMIZATIONS.md) describes the optimization experiments,
including those that were not kept.
