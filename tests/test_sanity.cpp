// Sanity tests: hand-countable instances, invariants and the legacy golden output.
// Each instance is checked against every solver in solvers().
#include <fstream>
#include <sstream>

#include "../src/brute.h"
#include "../src/dp.h"
#include "../src/fleets.h"
#include "test_util.h"

using namespace bs;

typedef Result (*SolverFn)(const Problem &);
struct NamedSolver {
	const char * name;
	SolverFn fn;
};
static Result dp_default(const Problem & p) { return solve_dp(p); }
static std::vector<NamedSolver> solvers() { return {{"brute", solve_brute}, {"dp", dp_default}}; }

static Fleet fl(std::initializer_list<std::pair<Shape, int>> ships) {
	Fleet f;
	int i = 0;
	for (auto & s : ships) f.push_back({"s" + std::to_string(i++), s.first, s.second});
	return f;
}

// Check total for every solver; returns the brute-force result for further checks.
static Result expect_total(const GameState & gs, const Fleet & f, bool rotate, u128 expected) {
	Problem p = make_problem(gs, f, rotate);
	Result first;
	for (auto & s : solvers()) {
		Result r = s.fn(p);
		if (r.total != expected) fprintf(stderr, "  solver %s:\n", s.name);
		CHECK_EQ(r.total, expected);
		first = r;
	}
	return first;
}

TEST(orientation_counts) {
	CHECK_EQ(orientations(ship_straight(1), true).size(), 1u);
	CHECK_EQ(orientations(ship_straight(5), true).size(), 2u);
	CHECK_EQ(orientations(ship_parnik(), true).size(), 4u);
	CHECK_EQ(orientations(ship_kriznik(), true).size(), 4u);
	CHECK_EQ(orientations(ship_kriznik(), false).size(), 1u);
	// rotating 4 times is the identity
	Shape k = ship_kriznik();
	CHECK(rotate90(rotate90(rotate90(rotate90(k)))) == k);
	Shape r = rotate90(ship_parnik());  // T pointing left: 2 wide, 3 tall
	CHECK_EQ(r.w, 2);
	CHECK_EQ(r.h, 3);
}

TEST(hand_counted_small_boards) {
	GameState g33(3, 3);
	// one 1x1 on 3x3: 9 cells
	expect_total(g33, fl({{ship_straight(1), 1}}), true, 9);
	// two 1x1 on 3x3: C(9,2)=36 pairs minus 20 king-adjacent pairs = 16
	expect_total(g33, fl({{ship_straight(1), 2}}), true, 16);
	// 1x2 on 2x2: 2 horizontal + 2 vertical; without rotation only 2
	GameState g22(2, 2);
	expect_total(g22, fl({{ship_straight(2), 1}}), true, 4);
	expect_total(g22, fl({{ship_straight(2), 1}}), false, 2);
	// T (parnik) on 3x2 board: up/down only fit -> 2; on 3x3: 4 orientations x 2 positions
	expect_total(GameState(3, 2), fl({{ship_parnik(), 1}}), true, 2);
	expect_total(g33, fl({{ship_parnik(), 1}}), true, 8);
	// three 1x1 on 3x3: any 3 of the 4 corners (4 sets), or two corners on one
	// side + the middle of the opposite side (4 sets) -> 8
	expect_total(g33, fl({{ship_straight(1), 3}}), true, 8);
	// fleet that cannot fit
	expect_total(g33, fl({{ship_straight(4), 1}}), true, 0);
	// 1x3 on 1x... 3x1 board: horizontal fits once
	expect_total(GameState(3, 1), fl({{ship_straight(3), 1}}), true, 1);
}

TEST(identical_ships_are_not_permuted) {
	// two distinct ship types with the same shape count ordered pairs,
	// two copies of one type count unordered pairs.
	GameState g(5, 1);
	expect_total(g, fl({{ship_straight(1), 2}}), true, 6);  // {0,2},{0,3},{0,4},{1,3},{1,4},{2,4}
	expect_total(g, fl({{ship_straight(1), 1}, {ship_straight(1), 1}}), true, 12);
}

TEST(misses) {
	GameState g(3, 3);
	g.cells[4] = MISS;
	expect_total(g, fl({{ship_straight(1), 1}}), true, 8);
	// 1x3 on 3x3 with centre miss: only the 4 border lines
	expect_total(g, fl({{ship_straight(3), 1}}), true, 4);
}

TEST(hits) {
	GameState g(3, 3);
	g.cells[4] = HIT;  // centre hit: second 1x1 would have to touch it
	expect_total(g, fl({{ship_straight(1), 2}}), true, 0);
	GameState g2(3, 3);
	g2.cells[0] = HIT;  // corner hit: other 1x1 in row 2 or column 2 -> 5 cells
	Result r = expect_total(g2, fl({{ship_straight(1), 2}}), true, 5);
	CHECK_EQ(r.cell_counts[0], r.total);  // hit cell is always occupied
	CHECK_EQ(r.cell_counts[1], (u128)0);
	// a 1x2 on a 4x1 board has 3 placements, a hit at the end leaves only one
	GameState g3(4, 1);
	g3.cells[0] = HIT;
	expect_total(g3, fl({{ship_straight(2), 1}}), true, 1);
	// two hits that only a 1x2 could explain, but the fleet has only 1x1s
	GameState g4(4, 1);
	g4.cells[1] = HIT;
	g4.cells[2] = HIT;
	expect_total(g4, fl({{ship_straight(1), 2}}), true, 0);
}

TEST(sunk_ships) {
	// fleet {1x2, 1x1} on 3x3, the 1x2 is sunk at cells 0,1: the 1x1 must avoid rows 0-1
	Fleet f = fl({{ship_straight(2), 1}, {ship_straight(1), 1}});
	GameState g(3, 3);
	g.sunk.push_back({0, {0, 1}});
	Result r = expect_total(g, f, true, 3);
	CHECK_EQ(r.cell_counts[0], r.total);  // sunk cells reported as certain
	CHECK_EQ(r.cell_counts[6], (u128)1);
	// sinking more ships than the fleet has is infeasible
	g.sunk.push_back({0, {6, 7}});
	expect_total(g, f, true, 0);
	// sunk ship with wrong size is infeasible
	GameState g2(3, 3);
	g2.sunk.push_back({0, {0}});
	expect_total(g2, f, true, 0);
	// sunk 1x2 given as non-adjacent cells, or wrapping around the row end
	GameState g3(3, 3);
	g3.sunk.push_back({0, {0, 2}});
	expect_total(g3, f, true, 0);
	GameState g4(3, 3);
	g4.sunk.push_back({0, {2, 3}});
	expect_total(g4, f, true, 0);
	// vertical sunk ship is fine with rotations, infeasible without
	GameState g5(3, 3);
	g5.sunk.push_back({0, {0, 3}});
	expect_total(g5, f, true, 3);  // 1x1 in column 2
	expect_total(g5, f, false, 0);
}

TEST(sunk_ship_interactions) {
	Fleet f = fl({{ship_straight(2), 1}, {ship_straight(1), 2}});
	// 4x3 board, 1x2 sunk at (0,0)-(0,1). Free cells for the 1x1s:
	// (0,3),(1,3),(2,0),(2,1),(2,2),(2,3). Hit at (2,0) -> other 1x1 at
	// (0,3),(1,3),(2,2),(2,3) -> 4.
	GameState g(4, 3);
	g.sunk.push_back({0, {0, 1}});
	g.cells[8] = HIT;
	Result r = expect_total(g, f, true, 4);
	CHECK_EQ(r.cell_counts[8], r.total);
	CHECK_EQ(r.cell_counts[9], (u128)0);
	CHECK_EQ(r.cell_counts[3], (u128)1);
	// hit on a sunk ship's cell is already explained: same as no hit there
	GameState g2(4, 3);
	g2.sunk.push_back({0, {0, 1}});
	g2.cells[0] = HIT;
	expect_total(g2, f, true, 9);  // 15 pairs of the 6 free cells minus 6 touching pairs
	// hit touching a sunk ship
	GameState g3(4, 3);
	g3.sunk.push_back({0, {0, 1}});
	g3.cells[2] = HIT;
	expect_total(g3, f, true, 0);
	// sunk ship on a miss
	GameState g4(4, 3);
	g4.sunk.push_back({0, {0, 1}});
	g4.cells[1] = MISS;
	expect_total(g4, f, true, 0);
	// two sunk ships touching diagonally
	GameState g5(4, 3);
	g5.sunk.push_back({0, {0, 1}});
	g5.sunk.push_back({1, {6}});
	expect_total(g5, f, true, 0);
	// out-of-range sunk cell is an input error
	GameState g6(4, 3);
	g6.sunk.push_back({1, {12}});
	bool threw = false;
	try {
		make_problem(g6, f, true);
	} catch (const std::invalid_argument &) {
		threw = true;
	}
	CHECK(threw);
}

TEST(asymmetric_shape_rotations) {
	Shape L = make_shape(2, 2, {1, 1, 1, 0});
	// 4 orientations x 4 positions in a 3x3 board
	expect_total(GameState(3, 3), fl({{L, 1}}), true, 16);
	expect_total(GameState(3, 3), fl({{L, 1}}), false, 4);
	// kriznik on 10x10: 6*9 positions for each horizontal orientation (2) and
	// 9*6 for each vertical one (2)
	expect_total(GameState(10, 10), fl({{ship_kriznik(), 1}}), true, 216);
	expect_total(GameState(5, 2), fl({{ship_kriznik(), 1}}), true, 2);
	// L-trominoes on an empty square board: heatmap invariant under rotation
	Problem p = make_problem(GameState(5, 5), fl({{L, 2}, {ship_straight(2), 1}}), true);
	for (auto & s : solvers()) {
		Result r = s.fn(p);
		CHECK(r.total > 0);
		for (int y = 0; y < 5; y++)
			for (int x = 0; x < 5; x++) CHECK_EQ(r.cell_counts[y * 5 + x], r.cell_counts[x * 5 + (4 - y)]);
	}
}

// Sum of all per-cell counts = total * number of ship cells in the fleet.
TEST(cell_counts_sum_to_total_times_fleet_cells) {
	Fleet f = fl({{ship_parnik(), 1}, {ship_straight(3), 1}, {ship_straight(1), 2}});
	Problem p = make_problem(GameState(6, 5), f, true);
	for (auto & s : solvers()) {
		Result r = s.fn(p);
		u128 sum = 0;
		for (auto v : r.cell_counts) sum += v;
		CHECK(r.total > 0);
		CHECK_EQ(sum, r.total * (u128)(4 + 3 + 2));
	}
}

// On an empty square board with all rotations, the heatmap has the full
// symmetry of the square (all ships here are mirror-symmetric).
TEST(empty_square_board_is_symmetric) {
	Fleet f = fl({{ship_parnik(), 1}, {ship_straight(3), 1}, {ship_straight(2), 1}, {ship_straight(1), 2}});
	const int n = 6;
	Problem p = make_problem(GameState(n, n), f, true);
	for (auto & s : solvers()) {
		Result r = s.fn(p);
		CHECK(r.total > 0);
		for (int y = 0; y < n; y++)
			for (int x = 0; x < n; x++) {
				u128 v = r.cell_counts[y * n + x];
				CHECK_EQ(v, r.cell_counts[x * n + (n - 1 - y)]);        // rotate 90
				CHECK_EQ(v, r.cell_counts[y * n + (n - 1 - x)]);        // mirror
				CHECK_EQ(v, r.cell_counts[x * n + y]);                  // transpose
			}
	}
}

// The original solver (main.cpp in commit 2dfcdb7; fixed orientations) on its
// default 9x8 setup.
// The golden file holds its grid and frequency table with the debug lines removed.
SLOW_TEST(matches_legacy_golden_9x8) {
	std::ifstream in(SRC_ROOT "/tests/golden/legacy_9x8_default.txt");
	CHECK(in.good());
	std::string line;
	std::vector<u128> freqs;
	u128 golden_total = 0;
	int table = 0;
	while (std::getline(in, line)) {
		if (line.rfind("---", 0) == 0) {
			table++;
			continue;
		}
		if (line.rfind("Total hits: ", 0) == 0) {
			golden_total = std::stoull(line.substr(12));
			continue;
		}
		if (table == 3 && line[0] == '|') {  // second table: frequencies
			std::stringstream ss(line);
			std::string cell;
			std::getline(ss, cell, '|');
			while (std::getline(ss, cell, '|'))
				if (!cell.empty()) freqs.push_back(std::stoull(cell));
		}
	}
	CHECK_EQ(freqs.size(), 72u);
	CHECK_EQ(golden_total, (u128)224781180);
	Problem p = make_problem(GameState(9, 8), fleet_legacy(), /*rotate=*/false);
	for (auto & s : solvers()) {
		Result r = s.fn(p);
		CHECK_EQ(r.total, golden_total);
		if (freqs.size() == 72)
			for (int i = 0; i < 72; i++) CHECK_EQ(r.cell_counts[i], freqs[i]);
	}
}
