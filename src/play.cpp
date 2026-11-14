// Compute hit probabilities for a game state.
//
// usage: play [--fleet game|czech|legacy] [--no-rotate] [--threads N] [state-file]
//
// The state is read from the file (or stdin): one line per row, one character
// per cell (spaces are ignored):
//   .  unknown        o  miss (water)
//   x  hit            S  cell of a sunk ship
// Sunk ships are identified by shape; touching S cells (incl. diagonally)
// belong to the same ship, since ships never touch.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>

#include "dp.h"
#include "fleets.h"

using namespace bs;

static GameState parse(std::istream & in, const Fleet & fleet, bool rotate) {
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
	GameState gs(W, H);
	std::vector<int> sunk;
	for (int r = 0; r < H; r++) {
		if ((int)rows[r].size() != W) throw std::invalid_argument("rows have different lengths");
		for (int c = 0; c < W; c++) {
			char ch = rows[r][c];
			int i = r * W + c;
			if (ch == 'o' || ch == 'O') gs.cells[i] = MISS;
			else if (ch == 'x' || ch == 'X') gs.cells[i] = HIT;
			else if (ch == 'S') sunk.push_back(i);
			else if (ch != '.') throw std::invalid_argument(std::string("unknown cell character: ") + ch);
		}
	}
	// group sunk cells into ships (8-connected components)
	std::vector<int> comp(W * H, -1);
	std::vector<uint8_t> is_sunk(W * H, 0);
	for (int i : sunk) is_sunk[i] = 1;
	for (int i : sunk) {
		if (comp[i] >= 0) continue;
		std::vector<int> stack{i}, cells;
		comp[i] = i;
		while (!stack.empty()) {
			int x = stack.back();
			stack.pop_back();
			cells.push_back(x);
			for (int dr = -1; dr <= 1; dr++)
				for (int dc = -1; dc <= 1; dc++) {
					int rr = x / W + dr, cc = x % W + dc;
					if (rr < 0 || rr >= H || cc < 0 || cc >= W) continue;
					int y = rr * W + cc;
					if (is_sunk[y] && comp[y] < 0) {
						comp[y] = i;
						stack.push_back(y);
					}
				}
		}
		Mask m = 0;
		for (int x : cells) m |= bit(x);
		int type = -1;
		for (int t = 0; t < (int)fleet.size() && type < 0; t++)
			for (const auto & o : orientations(fleet[t].shape, rotate))
				for (int r = 0; r + o.h <= H && type < 0; r++)
					for (int c = 0; c + o.w <= W && type < 0; c++) {
						Mask om;
						shape_mask(W, H, o, r, c, om);
						if (om == m) type = t;
					}
		if (type < 0) throw std::invalid_argument("sunk cells do not form a ship of the fleet");
		gs.sunk.push_back({type, cells});
	}
	return gs;
}

int main(int argc, char ** argv) {
	std::string fleet_name = "game", file;
	bool rotate = true;
	DpOptions opt;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--fleet") && i + 1 < argc) fleet_name = argv[++i];
		else if (!strcmp(argv[i], "--no-rotate")) rotate = false;
		else if (!strcmp(argv[i], "--threads") && i + 1 < argc) opt.threads = atoi(argv[++i]);
		else file = argv[i];
	}
	Fleet fleet = fleet_name == "czech" ? fleet_czech() : fleet_name == "legacy" ? fleet_legacy() : fleet_game();
	try {
		GameState gs(0, 0);
		if (file.empty()) gs = parse(std::cin, fleet, rotate);
		else {
			std::ifstream in(file);
			if (!in) throw std::invalid_argument("cannot open " + file);
			gs = parse(in, fleet, rotate);
		}
		auto t0 = std::chrono::steady_clock::now();
		Problem p = make_problem(gs, fleet, rotate);
		Result r = solve_dp(p, nullptr, opt);
		double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		if (r.total == 0) {
			printf("No fleet configuration is consistent with this state.\n");
			return 2;
		}
		print_heatmap(p, r, true);
		int best = -1;
		for (int i = 0; i < p.N; i++)
			if (gs.cells[i] == UNKNOWN && !((p.sunk_cells >> i) & 1) &&
			    (best < 0 || r.cell_counts[i] > r.cell_counts[best]))
				best = i;
		printf("configurations: %s   time: %.3fs\n", to_string(r.total).c_str(), secs);
		if (best >= 0)
			printf("best shot: row %d, column %d (%.2f%%)\n", best / p.W + 1, best % p.W + 1,
			       100.0 * (double)r.cell_counts[best] / (double)r.total);
	} catch (const std::exception & e) {
		fprintf(stderr, "error: %s\n", e.what());
		return 1;
	}
}
