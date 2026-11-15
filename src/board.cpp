#include "board.h"

#include <stdexcept>

namespace bs {

Board read_board(std::istream & in) {
	std::vector<std::string> rows;
	std::string line;
	while (std::getline(in, line)) {
		std::string r;
		for (char ch : line)
			if (ch != ' ' && ch != '\t' && ch != '\r') r.push_back(ch);
		if (!r.empty()) rows.push_back(r);
	}
	if (rows.empty()) throw std::invalid_argument("empty state");
	int H = (int)rows.size(), W = (int)rows[0].size();
	if (W * H > 128) throw std::invalid_argument("board larger than 128 cells");
	Board b(W, H);
	for (int r = 0; r < H; r++) {
		if ((int)rows[r].size() != W) throw std::invalid_argument("rows have different lengths");
		for (int c = 0; c < W; c++) {
			char ch = rows[r][c];
			if (ch == 'O') ch = 'o';
			if (ch == 'X') ch = 'x';
			if (ch != '.' && ch != 'o' && ch != 'x' && ch != 'S')
				throw std::invalid_argument(std::string("unknown cell character: ") + ch);
			b.at(r, c) = ch;
		}
	}
	return b;
}

std::string board_to_text(const Board & b) {
	std::string s;
	for (int r = 0; r < b.H; r++) {
		for (int c = 0; c < b.W; c++) {
			if (c) s.push_back(' ');
			s.push_back(b.at(r, c));
		}
		s.push_back('\n');
	}
	return s;
}

std::vector<int> component(const Board & b, int start) {
	char ch = b.cells[start];
	std::vector<uint8_t> seen(b.W * b.H, 0);
	std::vector<int> stack{start}, out;
	seen[start] = 1;
	while (!stack.empty()) {
		int x = stack.back();
		stack.pop_back();
		out.push_back(x);
		for (int dr = -1; dr <= 1; dr++)
			for (int dc = -1; dc <= 1; dc++) {
				int rr = x / b.W + dr, cc = x % b.W + dc;
				if (rr < 0 || rr >= b.H || cc < 0 || cc >= b.W) continue;
				int y = rr * b.W + cc;
				if (!seen[y] && b.cells[y] == ch) {
					seen[y] = 1;
					stack.push_back(y);
				}
			}
	}
	return out;
}

GameState board_to_state(const Board & b, const Fleet & fleet, bool rotate) {
	GameState gs(b.W, b.H);
	std::vector<uint8_t> done(b.W * b.H, 0);
	for (int i = 0; i < b.W * b.H; i++) {
		char ch = b.cells[i];
		if (ch == 'o') gs.cells[i] = MISS;
		else if (ch == 'x') gs.cells[i] = HIT;
		if (ch != 'S' || done[i]) continue;
		std::vector<int> cells = component(b, i);
		Mask m = 0;
		for (int x : cells) {
			done[x] = 1;
			m |= bit(x);
		}
		int type = -1;
		for (int t = 0; t < (int)fleet.size() && type < 0; t++)
			for (const auto & o : orientations(fleet[t].shape, rotate))
				for (int r = 0; r + o.h <= b.H && type < 0; r++)
					for (int c = 0; c + o.w <= b.W && type < 0; c++) {
						Mask om;
						shape_mask(b.W, b.H, o, r, c, om);
						if (om == m) type = t;
					}
		if (type < 0)
			throw std::invalid_argument("sunk cells at row " + std::to_string(i / b.W + 1) + ", column " +
			                            std::to_string(i % b.W + 1) + " do not form a ship of the fleet");
		gs.sunk.push_back({type, cells});
	}
	return gs;
}

}  // namespace bs
