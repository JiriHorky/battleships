// Text board parsing and sunk-ship recognition (shared by bin/play and bin/tui).
#include <sstream>
#include <stdexcept>

#include "../src/board.h"
#include "../src/dp.h"
#include "../src/fleets.h"
#include "test_util.h"

using namespace bs;

static Board parse(const std::string & s) {
	std::istringstream in(s);
	return read_board(in);
}

static bool throws(const std::string & text, const Fleet & f) {
	try {
		board_to_state(parse(text), f, true);
	} catch (const std::invalid_argument &) {
		return true;
	}
	return false;
}

TEST(board_read_and_roundtrip) {
	Board b = parse("x . O\n\n S S X \n");
	CHECK_EQ(b.W, 3);
	CHECK_EQ(b.H, 2);
	CHECK(std::string(b.cells.begin(), b.cells.end()) == "x.oSSx");
	CHECK(board_to_text(b) == "x . o\nS S x\n");
	Board b2 = parse(board_to_text(b));
	CHECK(b2.cells == b.cells);

	bool bad_len = false, bad_char = false, empty = false;
	try { parse("..\n...\n"); } catch (const std::invalid_argument &) { bad_len = true; }
	try { parse(".q\n"); } catch (const std::invalid_argument &) { bad_char = true; }
	try { parse("\n \n"); } catch (const std::invalid_argument &) { empty = true; }
	CHECK(bad_len);
	CHECK(bad_char);
	CHECK(empty);
}

TEST(board_component_is_8_connected_same_char) {
	Board b = parse("x..x\n.x.x\n...x\n");
	auto c1 = component(b, 0);  // (0,0) and diagonal (1,1)
	CHECK_EQ(c1.size(), (size_t)2);
	auto c2 = component(b, 3);  // right column
	CHECK_EQ(c2.size(), (size_t)3);
}

TEST(board_sunk_ships_recognised_by_shape) {
	Fleet f = fleet_game();
	// kriznik (rotated: 2 wide, 5 tall), a vertical dvojka and a jednicka
	GameState gs = board_to_state(parse(
		".S......S.\n"
		"SS......S.\n"
		".S........\n"
		"SS......S.\n"
		".S........\n"
		"..........\n"
		"..........\n"
		"..........\n"
		"..........\n"
		".........."), f, true);
	CHECK_EQ(gs.sunk.size(), (size_t)3);
	std::vector<int> per_type(f.size(), 0);
	for (auto & s : gs.sunk) per_type[s.type]++;
	CHECK_EQ(per_type[0], 1);  // kriznik
	CHECK_EQ(per_type[3], 1);  // dvojka
	CHECK_EQ(per_type[4], 1);  // jednicka

	CHECK(throws("SSS\n...\n...\n", f));  // no 3-long straight ship in the game fleet
	CHECK(throws("SS.\n.S.\n...\n", f));  // L tromino
	// rotated kriznik is rejected when rotation is off
	bool norot = false;
	try {
		board_to_state(parse(".S\nSS\n.S\nSS\n.S\n"), f, false);
	} catch (const std::invalid_argument &) {
		norot = true;
	}
	CHECK(norot);
}

TEST(board_misses_and_hits_reach_the_solver) {
	// one dvojka on a 3x1 board: 2 placements; a miss in the middle leaves none,
	// a hit at the left end leaves one
	Fleet f = {{"d", ship_straight(2), 1}};
	auto total = [&](const char * s) {
		return solve_dp(make_problem(board_to_state(parse(s), f, true), f, true)).total;
	};
	CHECK(total("...") == 2);
	CHECK(total(".o.") == 0);
	CHECK(total("x..") == 1);
	CHECK(total("SS.") == 1);  // already sunk: only the empty configuration remains
}
