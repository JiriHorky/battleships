// Benchmark driver.
// usage: bench [--solver dp|brute] [--repeat N] [--threads N] [--show] [scenario-substring | =exact-name ...]
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sys/resource.h>

#include "brute.h"
#include "dp.h"
#include "scenarios.h"

using namespace bs;

int main(int argc, char ** argv) {
	std::string solver = "dp";
	int repeat = 1;
	int threads = 0;
	bool show = false;
	std::vector<std::string> filters;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--solver") && i + 1 < argc) solver = argv[++i];
		else if (!strcmp(argv[i], "--repeat") && i + 1 < argc) repeat = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--threads") && i + 1 < argc) threads = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--show")) show = true;
		else filters.push_back(argv[i]);
	}
	printf("%-24s %-6s %10s %12s %12s %12s %9s  %s\n", "scenario", "solver", "best[s]", "max-layer", "states", "dead", "peakMB", "total");
	for (auto & sc : all_scenarios()) {
		if (!filters.empty()) {
			bool m = false;
			for (auto & f : filters)  // '=name' matches exactly, otherwise substring
				m |= f[0] == '=' ? sc.name == f.substr(1) : sc.name.find(f) != std::string::npos;
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
				DpOptions o;
				o.threads = threads;
				r = solve_dp(p, &st, o);
			}
			best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
		}
		struct rusage ru;
		getrusage(RUSAGE_SELF, &ru);  // ru_maxrss is in bytes on macOS (process-wide peak so far)
		printf("%-24s %-6s %10.3f %12zu %12zu %12zu %9ld  %s\n", sc.name.c_str(), solver.c_str(), best, st.max_layer_states,
		       st.total_states, st.dead_states, (long)(ru.ru_maxrss >> 20), to_string(r.total).c_str());
		fflush(stdout);
		if (solver != "brute")
			printf("    forward %.3fs  backward %.3fs  stored %.0f MB (%.2f B/state)%s\n", st.forward_s, st.backward_s,
			       st.stored_bytes / 1048576.0, st.total_states ? (double)st.stored_bytes / st.total_states : 0.0,
			       st.transposed ? "  (transposed)" : "");
		if (show) print_heatmap(p, r, true);
	}
}
