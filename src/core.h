// Core model shared by all solvers: board, ship shapes, fleet, game state,
// and the precomputed list of legal placements ("Problem").
//
// Rules (same as the original main.cpp):
//   * ships are polyominoes given by a bitmap over their bounding box,
//   * ships may not overlap and may not touch, not even diagonally,
//   * identical ships are interchangeable (a configuration is a *set* of
//     placements, so swapping two identical ships is not a new configuration).
//
// Game state:
//   * MISS  - cell known to be water,
//   * HIT   - cell known to be a ship cell (which ship is unknown),
//   * sunk ships - exact placement known; the ship is removed from the fleet
//     and its cells + surroundings become unavailable to the other ships.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bs {

typedef unsigned __int128 u128;
typedef u128 Mask;  // one bit per cell, row-major; boards up to 128 cells

static inline Mask bit(int i) { return (Mask)1 << i; }
static inline int popcount(Mask m) {
	return __builtin_popcountll((uint64_t)m) + __builtin_popcountll((uint64_t)(m >> 64));
}
std::string to_string(u128 v);

struct Shape {
	int w = 0, h = 0;
	std::vector<uint8_t> cells;  // row-major, w*h entries, 1 = ship cell

	bool at(int r, int c) const { return cells[r * w + c] != 0; }
	int size() const;  // number of ship cells
	bool operator==(const Shape & o) const { return w == o.w && h == o.h && cells == o.cells; }
};

Shape make_shape(int w, int h, const std::vector<int> & rows);  // validates bbox is tight
Shape rotate90(const Shape & s);
// Distinct orientations of a shape. rotate=false -> only the shape as given.
std::vector<Shape> orientations(const Shape & s, bool rotate);

struct ShipType {
	std::string name;
	Shape shape;
	int count;
};
typedef std::vector<ShipType> Fleet;

enum CellState : uint8_t { UNKNOWN = 0, MISS = 1, HIT = 2 };

struct SunkShip {
	int type;                // index into Fleet
	std::vector<int> cells;  // board cell indices
};

struct GameState {
	int W, H;
	std::vector<uint8_t> cells;  // W*H CellState
	std::vector<SunkShip> sunk;

	GameState(int w, int h) : W(w), H(h), cells(w * h, UNKNOWN) {}
};

struct Placement {
	int type;     // index into Problem::types
	int anchor;   // lowest cell index covered (row-major first ship cell)
	Mask cells;   // ship cells
	Mask halo;    // ship cells + all 8-neighbours (cells other ships may not use)
};

// A fully preprocessed instance, independent of how it is solved.
struct Problem {
	int W, H, N;
	std::vector<ShipType> types;      // remaining (unsunk) ship types, count may be 0
	std::vector<Placement> placements;  // legal placements of remaining ships, sorted by anchor
	Mask hits;       // unexplained hits: must be covered by some remaining ship
	Mask sunk_cells; // cells of sunk ships (reported as certain)
	bool infeasible = false;  // state is self-contradictory
	std::string infeasible_reason;  // first contradiction found, if any
};

Problem make_problem(const GameState & gs, const Fleet & fleet, bool rotate);

struct Result {
	u128 total = 0;                // number of consistent configurations
	std::vector<u128> cell_counts; // per cell: configurations with a ship there
	// (sunk ship cells are reported with count == total)
};

// Cells covered by a shape whose bounding box top-left is at (r, c);
// returns false if it does not fit.
bool shape_mask(int W, int H, const Shape & s, int r, int c, Mask & out);
Mask halo_of(int W, int H, Mask cells);

void print_heatmap(const Problem & p, const Result & r, bool percent);

}  // namespace bs
