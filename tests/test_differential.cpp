// Randomized differential test: every fast solver must agree exactly with the
// brute-force oracle (total and every per-cell count) on random small
// instances including misses, hits and sunk ships.
#include <random>

#include "../src/brute.h"
#include "../src/dp.h"
#include "../src/fleets.h"
#include "test_util.h"

using namespace bs;

static Shape random_shape(std::mt19937 & rng) {
	switch (rng() % 6) {
		case 0: return ship_straight(1);
		case 1: return ship_straight(2);
		case 2: return ship_straight(3);
		case 3: return ship_straight(4);
		case 4: return ship_parnik();
		default: return make_shape(2, 2, {1, 1, 1, 0});  // L-tromino (asymmetric)
	}
}

// Generates a random instance; sunk ships and hits are taken from a random
// valid configuration so that many instances are feasible.
static GameState random_state(std::mt19937 & rng, int W, int H, const Fleet & f, bool rotate) {
	GameState gs(W, H);
	Problem full = make_problem(gs, f, rotate);
	// greedy random configuration
	std::vector<int> order(full.placements.size());
	for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
	std::shuffle(order.begin(), order.end(), rng);
	std::vector<int> left;
	for (auto & t : f) left.push_back(t.count);
	Mask blocked = 0, ships = 0;
	std::vector<std::pair<int, Mask>> placed;
	for (int k : order) {
		const Placement & pl = full.placements[k];
		if (left[pl.type] == 0 || (pl.cells & blocked)) continue;
		left[pl.type]--;
		blocked |= pl.halo;
		ships |= pl.cells;
		placed.push_back({pl.type, pl.cells});
	}
	// shoot some cells
	int shots = rng() % (W * H / 2 + 1);
	for (int s = 0; s < shots; s++) {
		int c = rng() % (W * H);
		gs.cells[c] = (ships >> c) & 1 ? HIT : MISS;
	}
	// sink some fully-hit ships (and occasionally declare one sunk outright)
	for (auto & pr : placed) {
		bool all = true;
		for (int c = 0; c < W * H; c++)
			if (((pr.second >> c) & 1) && gs.cells[c] != HIT) all = false;
		if (all || rng() % 8 == 0) {
			SunkShip sk{pr.first, {}};
			for (int c = 0; c < W * H; c++)
				if ((pr.second >> c) & 1) sk.cells.push_back(c);
			gs.sunk.push_back(sk);
		}
	}
	// occasionally a contradictory shot
	if (rng() % 10 == 0) gs.cells[rng() % (W * H)] = rng() % 2 ? HIT : MISS;
	return gs;
}

TEST(dp_matches_brute_random) {
	std::mt19937 rng(12345);
	int feasible = 0;
	for (int iter = 0; iter < 400; iter++) {
		int W = 2 + rng() % 6, H = 2 + rng() % 6;
		bool rotate = rng() % 4 != 0;
		Fleet f;
		int ntypes = 1 + rng() % 3;
		for (int t = 0; t < ntypes; t++) f.push_back({"t", random_shape(rng), 1 + (int)(rng() % 3)});
		GameState gs = iter % 3 == 0 ? GameState(W, H) : random_state(rng, W, H, f, rotate);
		Problem p = make_problem(gs, f, rotate);
		Result a = solve_brute(p);
		Result b = solve_dp(p);
		if (a.total) feasible++;
		bool same = a.total == b.total && a.cell_counts == b.cell_counts;
		if (!same) fprintf(stderr, "  mismatch at iter %d (%dx%d): brute %s dp %s\n", iter, W, H,
		                   to_string(a.total).c_str(), to_string(b.total).c_str());
		CHECK(same);
	}
	CHECK(feasible > 100);  // make sure the test is not vacuous
}

// Wide boards: the solver must transpose or use 128-bit masks (found in review:
// these used to throw).
TEST(dp_wide_boards_and_paths) {
	struct Case { int W, H; Fleet f; };
	std::vector<Case> cases = {
		{20, 6, {{"i4", ship_straight(4), 1}}},
		{40, 3, {{"i3", ship_straight(3), 1}}},
		{3, 40, {{"i3", ship_straight(3), 2}}},
		{30, 4, {{"t", ship_parnik(), 1}, {"i2", ship_straight(2), 1}}},
		{1, 7, {{"i2", ship_straight(2), 2}}},
		{7, 1, {{"i2", ship_straight(2), 2}}},
		{4, 4, {}},                                                          // empty fleet
		{4, 4, {{"i2", ship_straight(2), 0}, {"i1", ship_straight(1), 2}}},  // count-0 type
	};
	for (auto & c : cases) {
		Problem p = make_problem(GameState(c.W, c.H), c.f, true);
		Result a = solve_brute(p);
		for (int variant = 0; variant < 4; variant++) {
			DpOptions o;
			o.allow_transpose = variant & 1;
			o.force_wide_counts = variant & 2;
			o.threads = variant == 3 ? 1 : 0;
			Result b = solve_dp(p, nullptr, o);
			bool same = a.total == b.total && a.cell_counts == b.cell_counts;
			if (!same) fprintf(stderr, "  %dx%d variant %d: brute %s dp %s\n", c.W, c.H, variant,
			                   to_string(a.total).c_str(), to_string(b.total).c_str());
			CHECK(same);
		}
	}
}

// 128-bit count path, single-threaded and non-transposed runs agree with the default.
TEST(dp_count_width_threads_transpose_agree) {
	std::mt19937 rng(777);
	for (int iter = 0; iter < 60; iter++) {
		int W = 3 + rng() % 6, H = 3 + rng() % 6;
		Fleet f = {{"a", random_shape(rng), 1 + (int)(rng() % 2)}, {"b", random_shape(rng), 1 + (int)(rng() % 3)}};
		GameState gs = random_state(rng, W, H, f, true);
		Problem p = make_problem(gs, f, true);
		Result a = solve_dp(p);
		DpOptions o1;
		o1.force_wide_counts = true;
		DpOptions o2;
		o2.threads = 1;
		o2.allow_transpose = false;
		Result b = solve_dp(p, nullptr, o1), c = solve_dp(p, nullptr, o2);
		CHECK(a.total == b.total && a.cell_counts == b.cell_counts);
		CHECK(a.total == c.total && a.cell_counts == c.cell_counts);
	}
}

// Review follow-ups: stats survive the 128-bit retry; absurd fleets are
// rejected with an error instead of exhausting memory.
TEST(dp_stats_and_limits) {
	Problem p = make_problem(GameState(8, 3), Fleet{{"i2", ship_straight(2), 2}}, true);
	DpOptions o;
	o.force_wide_counts = true;
	DpStats st;
	solve_dp(p, &st, o);
	CHECK(st.transposed);
	Fleet many;
	for (int t = 0; t < 24; t++) many.push_back({"t", ship_straight(1), 1});
	bool threw = false;
	try {
		solve_dp(make_problem(GameState(10, 10), many, true));
	} catch (const std::invalid_argument &) {
		threw = true;
	}
	CHECK(threw);
}

// A state-budget overflow is thrown inside worker threads; it must reach the
// caller as std::runtime_error (multi- and single-threaded).
TEST(dp_state_budget_propagates_from_workers) {
	Problem p = make_problem(GameState(10, 10), fleet_game(), true);
	for (int threads : {0, 1}) {
		DpOptions o;
		o.threads = threads;
		o.max_states = 1000;
		bool threw = false;
		try {
			solve_dp(p, nullptr, o);
		} catch (const std::runtime_error &) {
			threw = true;
		}
		CHECK(threw);
	}
}
