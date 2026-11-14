#include "dp.h"

#include <algorithm>
#include <stdexcept>

namespace bs {

namespace {

struct Key {
	uint64_t mask;   // bit k = cell (i + k) is blocked
	uint32_t fleet;  // mixed-radix index of remaining ship counts
	bool operator==(const Key & o) const { return mask == o.mask && fleet == o.fleet; }
};

static inline uint64_t hash_key(const Key & k) {
	uint64_t h = k.mask * 0x9E3779B97F4A7C15ull ^ ((uint64_t)k.fleet * 0xC2B2AE3D27D4EB4Full);
	h ^= h >> 29;
	h *= 0xBF58476D1CE4E5B9ull;
	h ^= h >> 32;
	return h;
}

// Open-addressing map Key -> dense index.
class IndexMap {
   public:
	std::vector<Key> keys;

	IndexMap() { rehash(1024); }

	uint32_t find_or_insert(const Key & k) {
		if ((keys.size() + 1) * 2 > slots_.size()) rehash(slots_.size() * 2);
		size_t m = slots_.size() - 1;
		for (size_t s = hash_key(k) & m;; s = (s + 1) & m) {
			uint32_t v = slots_[s];
			if (v == EMPTY) {
				slots_[s] = (uint32_t)keys.size();
				keys.push_back(k);
				return slots_[s];
			}
			if (keys[v] == k) return v;
		}
	}

	uint32_t find(const Key & k) const {
		size_t m = slots_.size() - 1;
		for (size_t s = hash_key(k) & m;; s = (s + 1) & m) {
			uint32_t v = slots_[s];
			if (v == EMPTY) return EMPTY;
			if (keys[v] == k) return v;
		}
	}

	static constexpr uint32_t EMPTY = 0xFFFFFFFFu;

   private:
	std::vector<uint32_t> slots_;

	void rehash(size_t n) {
		slots_.assign(n, EMPTY);
		size_t m = n - 1;
		for (uint32_t i = 0; i < keys.size(); i++) {
			size_t s = hash_key(keys[i]) & m;
			while (slots_[s] != EMPTY) s = (s + 1) & m;
			slots_[s] = i;
		}
	}
};

struct Trans {
	uint64_t grown;  // grown shape (ship + one cell right/down), relative to anchor
	int type;
	int pidx;        // index into Problem::placements
};

// |S + {0,1}^2| for a shape S.
static int grown_area(const Shape & s) {
	std::vector<uint8_t> g((s.w + 1) * (s.h + 1), 0);
	for (int r = 0; r < s.h; r++)
		for (int c = 0; c < s.w; c++)
			if (s.at(r, c))
				for (int dr = 0; dr <= 1; dr++)
					for (int dc = 0; dc <= 1; dc++) g[(r + dr) * (s.w + 1) + c + dc] = 1;
	int n = 0;
	for (auto v : g) n += v;
	return n;
}

static inline bool add_ovf(uint64_t & a, uint64_t b) { return __builtin_add_overflow(a, b, &a); }
static inline bool add_ovf(u128 & a, u128 b) {
	a += b;
	return a < b;
}

// Returns false if CT overflowed (caller retries with a wider type).
template <typename CT>
static bool run_dp(const Problem & p, Result & res, DpStats * stats) {
	const int T = (int)p.types.size();
	const int GW = p.W + 1, G = GW * (p.H + 1);  // grown board

	// Fleet encoding: mixed radix with digit t = remaining ships of type t.
	std::vector<uint32_t> radix(T), stride(T);
	uint32_t nfleet = 1;
	for (int t = 0; t < T; t++) {
		radix[t] = std::max(0, p.types[t].count) + 1;
		stride[t] = nfleet;
		nfleet *= radix[t];
	}
	const uint32_t full = nfleet - 1;
	std::vector<int> need(nfleet, 0);  // grown area the remaining ships cover
	{
		std::vector<int> ga(T);
		for (int t = 0; t < T; t++) ga[t] = grown_area(p.types[t].shape);
		for (uint32_t f = 0; f < nfleet; f++)
			for (int t = 0; t < T; t++) need[f] += ((f / stride[t]) % radix[t]) * ga[t];
	}
	// A state at grown cell g is viable only if the free grown cells left
	// (G - g minus those already covered) can hold the remaining ships.
	auto viable = [&](int g, uint64_t mask, uint32_t f) { return need[f] <= G - g - __builtin_popcountll(mask); };

	std::vector<std::vector<Trans>> at(G);
	std::vector<uint8_t> hit(G, 0);
	for (int i = 0; i < p.N; i++)
		if ((p.hits >> i) & 1) hit[(i / p.W) * GW + i % p.W] = 1;
	for (int k = 0; k < (int)p.placements.size(); k++) {
		const Placement & pl = p.placements[k];
		int a = (pl.anchor / p.W) * GW + pl.anchor % p.W;
		uint64_t gm = 0;
		for (int i = 0; i < p.N; i++) {
			if (!((pl.cells >> i) & 1)) continue;
			int g = (i / p.W) * GW + i % p.W;
			for (int d : {0, 1, GW, GW + 1}) {
				int off = g + d - a;
				if (off >= 64) throw std::invalid_argument("ship reach exceeds 64 cells; board too wide");
				gm |= 1ull << off;
			}
		}
		at[a].push_back(Trans{gm, pl.type, k});
	}

	// Forward pass over grown cells.
	std::vector<IndexMap> layer(G + 1);
	std::vector<std::vector<CT>> fwd(G + 1);
	if (viable(0, 0, full)) {
		layer[0].find_or_insert(Key{0, full});
		fwd[0].push_back(1);
	}
	bool ovf = false;
	for (int g = 0; g < G; g++) {
		IndexMap & nx = layer[g + 1];
		std::vector<CT> & nf = fwd[g + 1];
		auto add = [&](uint64_t m, uint32_t f, CT v) {
			if (!viable(g + 1, m, f)) return;
			uint32_t idx = nx.find_or_insert(Key{m, f});
			if (idx >= nf.size()) nf.push_back(0);
			ovf |= add_ovf(nf[idx], v);
		};
		const auto & keys = layer[g].keys;
		for (size_t s = 0; s < keys.size(); s++) {
			const Key k = keys[s];
			const CT v = fwd[g][s];
			if (k.mask & 1) {  // covered by an earlier ship's grown shape
				add(k.mask >> 1, k.fleet, v);
				continue;
			}
			if (!hit[g]) add(k.mask >> 1, k.fleet, v);  // leave this cell uncovered
			for (const Trans & tr : at[g]) {
				if (tr.grown & k.mask) continue;
				if ((k.fleet / stride[tr.type]) % radix[tr.type] == 0) continue;
				add((k.mask | tr.grown) >> 1, k.fleet - stride[tr.type], v);
			}
		}
		if (ovf) return false;
	}

	// Backward pass, accumulating per-placement weights.
	std::vector<u128> weight(p.placements.size(), 0);
	std::vector<CT> bwd_next(layer[G].keys.size(), 0);
	for (size_t s = 0; s < layer[G].keys.size(); s++) bwd_next[s] = layer[G].keys[s].fleet == 0 ? 1 : 0;
	for (int g = G - 1; g >= 0; g--) {
		const IndexMap & nx = layer[g + 1];
		auto get = [&](uint64_t m, uint32_t f) -> CT {
			uint32_t idx = nx.find(Key{m, f});
			return idx == IndexMap::EMPTY ? 0 : bwd_next[idx];
		};
		const auto & keys = layer[g].keys;
		std::vector<CT> bwd(keys.size(), 0);
		for (size_t s = 0; s < keys.size(); s++) {
			const Key k = keys[s];
			if (k.mask & 1) {
				bwd[s] = get(k.mask >> 1, k.fleet);
				continue;
			}
			CT b = 0;
			if (!hit[g]) b = get(k.mask >> 1, k.fleet);
			for (const Trans & tr : at[g]) {
				if (tr.grown & k.mask) continue;
				if ((k.fleet / stride[tr.type]) % radix[tr.type] == 0) continue;
				CT b2 = get((k.mask | tr.grown) >> 1, k.fleet - stride[tr.type]);
				if (!b2) continue;
				ovf |= add_ovf(b, b2);
				weight[tr.pidx] += (u128)fwd[g][s] * (u128)b2;
			}
			bwd[s] = b;
		}
		if (ovf) return false;
		bwd_next.swap(bwd);
		std::vector<CT>().swap(fwd[g + 1]);
		if (stats) {
			stats->max_layer_states = std::max(stats->max_layer_states, keys.size());
			stats->total_states += keys.size();
			for (auto & b : bwd_next) stats->dead_states += b == 0;
		}
	}
	res.total = bwd_next.empty() ? 0 : bwd_next[0];

	for (size_t k = 0; k < p.placements.size(); k++) {
		if (!weight[k]) continue;
		Mask c = p.placements[k].cells;
		for (int i = 0; i < p.N; i++)
			if ((c >> i) & 1) res.cell_counts[i] += weight[k];
	}
	return true;
}

}  // namespace

Result solve_dp(const Problem & p, DpStats * stats) {
	Result res;
	res.cell_counts.assign(p.N, 0);
	if (p.infeasible) return res;
	if (stats) *stats = DpStats();
	if (!run_dp<uint64_t>(p, res, stats)) {
		res.total = 0;
		res.cell_counts.assign(p.N, 0);
		if (stats) *stats = DpStats();
		run_dp<u128>(p, res, stats);
	}
	for (int i = 0; i < p.N; i++)
		if ((p.sunk_cells >> i) & 1) res.cell_counts[i] = res.total;
	return res;
}

}  // namespace bs
