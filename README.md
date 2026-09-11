# Battleships

A brute-force C++ solver that enumerates every valid placement of a fixed
fleet of ships on a Battleship-style grid and computes, for each cell, how
often it ends up occupied by a ship. The result is a heatmap of "hit
probability" per cell, along with the total number of valid full-fleet
placements.

## How it works

- The board is a `WIDTH x HEIGHT` grid (9x8 by default, see constants in
  `main.cpp`).
- Each ship is defined by a shape (a bitmap over its bounding box), e.g. a
  straight 1x1, 3x1, 4x1, or an L-shaped/cross-shaped multi-cell ship.
- Standard Battleship adjacency rules apply: ships cannot touch each other,
  not even diagonally. Each `ship` computes a "surrounding shape" mask used
  to block adjacent cells once placed.
- `place_ships` recursively tries every valid position for each ship in
  turn (skipping placements that would overlap another ship or its
  surrounding cells), backtracking after each attempt.
- Whenever a full, valid placement of all ships is found, `add_frequencies`
  increments a per-cell counter for every cell covered by a ship, and the
  total placement count (`hits`) is incremented.
- `shapes_map` deduplicates ships that share the same shape so that
  identical ships are only tried in strictly increasing grid positions,
  avoiding redundant permutations of otherwise-interchangeable ships.

## Build

Requires `g++` with C++ support for `<bits/stdc++.h>` (GCC).

```sh
make
```

This produces `bin/main`.

## Usage

```sh
./bin/main
```

The program prints:
1. The empty grid (with cell indices) at startup.
2. The frequency grid once the search completes — the number of valid
   placements in which each cell was occupied by a ship.
3. `Total hits: <N>` — the total number of valid full-fleet placements
   found.

## Configuring the fleet or board

- Board size: adjust the `WIDTH` and `HEIGHT` macros near the top of
  `main.cpp`.
- Fleet composition: ships are constructed and pushed onto the `ships`
  vector in `main()`. Comment/uncomment or add `ship_t` instances to change
  which ships are placed. Each `ship_t` takes a name, bounding-box width and
  height, and a row-major `vector<bool>` shape.

Note: the search space grows very quickly with board size and fleet size,
so larger configurations can take a long time to run.
