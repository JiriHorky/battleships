// Compute hit probabilities for a game state.
//
// usage: play [--fleet game|czech|legacy] [--no-rotate] [--threads N] [state-file]
// exit code: 0 ok, 1 bad input, 2 no configuration fits the state
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

#include "board.h"
#include "dp.h"
#include "fleets.h"

using namespace bs;

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
	try {
		Fleet fleet = fleet_by_name(fleet_name);
		Board b;
		if (file.empty()) b = read_board(std::cin);
		else {
			std::ifstream in(file);
			if (!in) throw std::invalid_argument("cannot open " + file);
			b = read_board(in);
		}
		GameState gs = board_to_state(b, fleet, rotate);
		auto t0 = std::chrono::steady_clock::now();
		Problem p = make_problem(gs, fleet, rotate);
		Result r = solve_dp(p, nullptr, opt);
		double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		if (r.total == 0) {
			printf("No fleet configuration is consistent with this state%s%s.\n", p.infeasible ? ": " : "",
			       p.infeasible_reason.c_str());
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
