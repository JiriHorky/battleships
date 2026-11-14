#include "brute.h"

namespace bs {

namespace {

struct Brute {
	const Problem & p;
	std::vector<std::vector<int>> by_type;  // placement indices per type
	std::vector<int> remaining;             // ships of each type still to place
	Result res;

	explicit Brute(const Problem & pr) : p(pr), by_type(pr.types.size()), remaining(pr.types.size()) {
		for (int i = 0; i < (int)p.placements.size(); i++) by_type[p.placements[i].type].push_back(i);
		for (size_t t = 0; t < p.types.size(); t++) remaining[t] = p.types[t].count;
		res.cell_counts.assign(p.N, 0);
	}

	// Place the remaining ships of type t, choosing placements with index >= from
	// (strictly increasing indices so identical ships are not permuted).
	void rec(size_t t, size_t from, Mask blocked, Mask occupied) {
		while (t < p.types.size() && remaining[t] == 0) {
			t++;
			from = 0;
		}
		if (t == p.types.size()) {
			if ((p.hits & ~occupied) != 0) return;
			res.total++;
			for (int i = 0; i < p.N; i++)
				if (occupied & bit(i)) res.cell_counts[i]++;
			return;
		}
		const auto & list = by_type[t];
		for (size_t k = from; k < list.size(); k++) {
			const Placement & pl = p.placements[list[k]];
			if (pl.cells & blocked) continue;
			remaining[t]--;
			rec(t, k + 1, blocked | pl.halo, occupied | pl.cells);
			remaining[t]++;
		}
	}
};

}  // namespace

Result solve_brute(const Problem & p) {
	Brute b(p);
	if (!p.infeasible) b.rec(0, 0, 0, 0);
	for (int i = 0; i < p.N; i++)
		if (p.sunk_cells & bit(i)) b.res.cell_counts[i] = b.res.total;
	return b.res;
}

}  // namespace bs
