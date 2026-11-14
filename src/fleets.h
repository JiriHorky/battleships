// Ship definitions used by the original main.cpp, and the fleets built from them.
#pragma once
#include "core.h"

namespace bs {

inline Shape ship_straight(int n) { return make_shape(n, 1, std::vector<int>(n, 1)); }
inline Shape ship_parnik() { return make_shape(3, 2, {0, 1, 0, 1, 1, 1}); }
inline Shape ship_kriznik() { return make_shape(5, 2, {0, 1, 0, 1, 0, 1, 1, 1, 1, 1}); }

// The fleet that is active in the original main(): 5, 4, 3, 3, 1, 1, 1.
inline Fleet fleet_legacy() {
	return {
		{"petka", ship_straight(5), 1},
		{"ctyrka", ship_straight(4), 1},
		{"trojka", ship_straight(3), 2},
		{"jednicka", ship_straight(1), 3},
	};
}

// The 10x10 game fleet: 12 ships, 31 cells.
inline Fleet fleet_game() {
	return {
		{"kriznik", ship_kriznik(), 1},
		{"petka", ship_straight(5), 1},
		{"parnik", ship_parnik(), 2},
		{"dvojka", ship_straight(2), 3},
		{"jednicka", ship_straight(1), 5},
	};
}

// Every ship defined in the original main(): 14 ships, 40 cells (stress test).
inline Fleet fleet_czech() {
	return {
		{"kriznik", ship_kriznik(), 1},
		{"petka", ship_straight(5), 1},
		{"ctyrka", ship_straight(4), 1},
		{"parnik", ship_parnik(), 2},
		{"trojka", ship_straight(3), 2},
		{"dvojka", ship_straight(2), 3},
		{"jednicka", ship_straight(1), 4},
	};
}

}  // namespace bs
