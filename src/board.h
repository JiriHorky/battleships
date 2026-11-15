// Text board <-> game state, shared by the command-line tools.
//
// One character per cell:
//   .  unknown        o  miss (water)
//   x  hit            S  cell of a sunk ship
// Sunk ships are identified by shape: S cells that touch (incl. diagonally)
// belong to the same ship, since ships never touch.
#pragma once
#include <istream>
#include <string>

#include "core.h"

namespace bs {

struct Board {
	int W = 0, H = 0;
	std::vector<char> cells;  // row-major, one of . o x S

	Board() = default;
	Board(int w, int h) : W(w), H(h), cells(w * h, '.') {}
	char & at(int r, int c) { return cells[r * W + c]; }
	char at(int r, int c) const { return cells[r * W + c]; }
};

// Reads one line per row; spaces are ignored, 'O'/'X' are accepted as 'o'/'x'.
// Throws std::invalid_argument on malformed input.
Board read_board(std::istream & in);
std::string board_to_text(const Board & b);

// Cells 8-connected to `start` that hold the same character.
std::vector<int> component(const Board & b, int start);

// Throws std::invalid_argument if a group of S cells is not a ship of the fleet.
GameState board_to_state(const Board & b, const Fleet & fleet, bool rotate);

}  // namespace bs
