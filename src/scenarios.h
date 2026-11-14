// Named, reproducible benchmark / demo scenarios.
#pragma once
#include <random>
#include <string>
#include <vector>

#include "core.h"
#include "fleets.h"

namespace bs {

struct Scenario {
	std::string name;
	GameState state;
	Fleet fleet;
	bool rotate;
};

// Plays `shots` random shots against a random hidden fleet (seeded), marking
// hits/misses and declaring ships sunk once all their cells are hit.
inline GameState random_midgame(int W, int H, const Fleet & f, bool rotate, int shots, unsigned seed) {
	std::mt19937 rng(seed);
	GameState gs(W, H);
	Problem full = make_problem(gs, f, rotate);
	std::vector<std::pair<int, Mask>> placed;
	// retry until the whole fleet fits (dense fleets often fail greedily)
	for (int attempt = 0; attempt < 100000; attempt++) {
		placed.clear();
		std::vector<int> left;
		for (auto & t : f) left.push_back(t.count);
		// place largest ships first: random order among each type's placements
		Mask blocked = 0;
		bool ok = true;
		for (size_t t = 0; t < f.size() && ok; t++) {
			std::vector<int> cand;
			for (int k = 0; k < (int)full.placements.size(); k++)
				if (full.placements[k].type == (int)t) cand.push_back(k);
			for (int n = 0; n < f[t].count && ok; n++) {
				std::vector<int> fit;
				for (int k : cand)
					if (!(full.placements[k].cells & blocked)) fit.push_back(k);
				if (fit.empty()) {
					ok = false;
					break;
				}
				const Placement & pl = full.placements[fit[rng() % fit.size()]];
				blocked |= pl.halo;
				placed.push_back({(int)t, pl.cells});
			}
		}
		if (ok) break;
	}
	Mask ships = 0;
	for (auto & pr : placed) ships |= pr.second;
	std::vector<int> cells(W * H);
	for (int i = 0; i < W * H; i++) cells[i] = i;
	std::shuffle(cells.begin(), cells.end(), rng);
	for (int s = 0; s < shots && s < W * H; s++) gs.cells[cells[s]] = (ships >> cells[s]) & 1 ? HIT : MISS;
	for (auto & pr : placed) {
		bool all = true;
		SunkShip sk{pr.first, {}};
		for (int c = 0; c < W * H; c++)
			if ((pr.second >> c) & 1) {
				sk.cells.push_back(c);
				if (gs.cells[c] != HIT) all = false;
			}
		if (all) gs.sunk.push_back(sk);
	}
	return gs;
}

inline std::vector<Scenario> all_scenarios() {
	std::vector<Scenario> v;
	v.push_back({"legacy-9x8", GameState(9, 8), fleet_legacy(), false});
	v.push_back({"legacy-9x8-rot", GameState(9, 8), fleet_legacy(), true});
	v.push_back({"game-8x8", GameState(8, 8), fleet_game(), true});
	v.push_back({"game-9x9", GameState(9, 9), fleet_game(), true});
	v.push_back({"game-10x10", GameState(10, 10), fleet_game(), true});
	for (int shots : {3, 10, 20, 35, 50})
		v.push_back({"game-10x10-shots" + std::to_string(shots),
		             random_midgame(10, 10, fleet_game(), true, shots, 2000 + shots), fleet_game(), true});
	v.push_back({"czech-10x10", GameState(10, 10), fleet_czech(), true});
	for (int shots : {5, 15, 30, 50})
		v.push_back({"czech-10x10-shots" + std::to_string(shots),
		             random_midgame(10, 10, fleet_czech(), true, shots, 1000 + shots), fleet_czech(), true});
	return v;
}

}  // namespace bs
