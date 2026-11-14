// Benchmark driver.
// usage: bench [--solver dp|brute] [--repeat N] [--show] [scenario-substring ...]
#include <chrono>
#include <cstdio>
#include <cstring>

#include "brute.h"
#include "dp.h"
#include "scenarios.h"

using namespace bs;

int main(int argc, char ** argv) {
	std::string solver = "dp";
	int repeat = 1;
	bool show = false;
	std::vector<std::string> filters;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--solver") && i + 1 < argc) solver = argv[++i];
		else if (!strcmp(argv[i], "--repeat") && i + 1 < argc) repeat = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--show")) show = true;
		else filters.push_back(argv[i]);
	}
	printf("%-24s %-6s %10s %12s %12s %12s  %s\n", "scenario", "solver", "best[s]", "max-layer", "states", "dead", "total");
	for (auto & sc : all_scenarios()) {
		if (!filters.empty()) {
			bool m = false;
			for (auto & f : filters) m |= sc.name.find(f) != std::string::npos;
			if (!m) continue;
		}
		Problem p = make_problem(sc.state, sc.fleet, sc.rotate);
		double best = 1e30;
		Result r;
		DpStats st;
		for (int k = 0; k < repeat; k++) {
			auto t0 = std::chrono::steady_clock::now();
			if (solver == "brute") r = solve_brute(p);
			else {
				st = DpStats();
				r = solve_dp(p, &st);
			}
			best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
		}
		printf("%-24s %-6s %10.3f %12zu %12zu %12zu  %s\n", sc.name.c_str(), solver.c_str(), best, st.max_layer_states,
		       st.total_states, st.dead_states, to_string(r.total).c_str());
		fflush(stdout);
		if (show) print_heatmap(p, r, true);
	}
}
