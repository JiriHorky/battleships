#include "dp.h"
#include "parallel.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace bs {

namespace {

static inline uint64_t mix64(uint64_t m) {
	m ^= m >> 31;
	m *= 0x9E3779B97F4A7C15ull;
	m ^= m >> 29;
	m *= 0xBF58476D1CE4E5B9ull;
	m ^= m >> 32;
	return m;
}
static inline uint64_t hash_mask(uint64_t m) { return mix64(m); }
static inline uint64_t hash_mask(u128 m) { return mix64((uint64_t)m ^ mix64((uint64_t)(m >> 64))); }
static inline int popc(uint64_t m) { return __builtin_popcountll(m); }
static inline int popc(u128 m) { return popcount(m); }

static inline bool add_ovf(uint64_t & a, uint64_t b) { return __builtin_add_overflow(a, b, &a); }
static inline bool add_ovf(u128 & a, u128 b) {
	a += b;
	return a < b;
}

static const uint32_t EMPTY = 0xFFFFFFFFu;

// Open-addressing index: mask -> position in an external mask array.
template <typename MT>
struct MaskIndex {
	std::vector<uint32_t> slots;
	uint64_t mod = 0;

	void build(const std::vector<MT> & masks) {
		size_t cap = 16;
		while (cap < masks.size() * 2) cap <<= 1;
		slots.assign(cap, EMPTY);
		mod = cap - 1;
		for (uint32_t i = 0; i < masks.size(); i++) {
			uint64_t s = hash_mask(masks[i]) & mod;
			while (slots[s] != EMPTY) s = (s + 1) & mod;
			slots[s] = i;
		}
	}
	uint32_t find(const std::vector<MT> & masks, MT m) const {
		if (slots.empty()) return EMPTY;
		for (uint64_t s = hash_mask(m) & mod;; s = (s + 1) & mod) {
			uint32_t v = slots[s];
			if (v == EMPTY || masks[v] == m) return v;
		}
	}
};

// Growable mask -> count accumulator used while building one group.
template <typename MT, typename CT>
struct Accumulator {
	std::vector<uint32_t> slots;
	uint64_t mod = 0;
	std::vector<MT> masks;
	std::vector<CT> vals;
	bool ovf = false;
	// state budget shared by all accumulators of one DP run
	std::atomic<size_t> * stored = nullptr;
	size_t budget = 0;
	size_t reported = 0;  // states of the current group already added to *stored

	void reset() {
		if (slots.size() > (1u << 16)) std::vector<uint32_t>().swap(slots);
		slots.assign(std::max<size_t>(slots.size(), 1024), EMPTY);
		mod = slots.size() - 1;
		masks.clear();
		vals.clear();
		reported = 0;
	}
	// Adds this group's not-yet-reported states to the shared counter, so
	// in-progress groups of all threads count against the budget.
	void report() {
		if (!stored) return;
		size_t total = stored->fetch_add(masks.size() - reported) + masks.size() - reported;
		reported = masks.size();
		if (total > budget) throw std::runtime_error("DP state budget exceeded (DpOptions::max_states)");
	}
	void grow() {
		slots.assign(slots.size() * 2, EMPTY);
		mod = slots.size() - 1;
		for (uint32_t i = 0; i < masks.size(); i++) {
			uint64_t s = hash_mask(masks[i]) & mod;
			while (slots[s] != EMPTY) s = (s + 1) & mod;
			slots[s] = i;
		}
	}
	inline void add(MT m, CT v) {
		for (uint64_t s = hash_mask(m) & mod;; s = (s + 1) & mod) {
			uint32_t i = slots[s];
			if (i == EMPTY) {
				slots[s] = (uint32_t)masks.size();
				masks.push_back(m);
				vals.push_back(v);
				if ((masks.size() & 4095) == 0) report();
				if (masks.size() * 2 > slots.size()) grow();
				return;
			}
			if (masks[i] == m) {
				ovf |= add_ovf(vals[i], v);
				return;
			}
		}
	}
};

template <typename MT>
struct Trans {
	MT grown;  // grown shape (ship + one cell right/down), relative to anchor
	int pidx;  // index into Problem::placements
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

template <typename MT, typename CT>
struct Group {
	std::vector<MT> mask;
	std::vector<CT> fwd;
	std::vector<CT> bwd;
	MaskIndex<MT> index;
};

// Largest offset (in grown-board cells) between a placement's anchor and the
// last cell of its grown shape.
static int max_reach(const Problem & p) {
	const int GW = p.W + 1;
	int r = 0;
	for (const Placement & pl : p.placements) {
		int a = (pl.anchor / p.W) * GW + pl.anchor % p.W;
		for (int i = 0; i < p.N; i++)
			if ((pl.cells >> i) & 1) r = std::max(r, (i / p.W) * GW + i % p.W + GW + 1 - a);
	}
	return r;
}

// Bitwise lookahead on a frontier mask m of layer g (bit j = grown cell g+j
// taken), a few word operations per state.
//
// A future placement (anchored at >= g, of a ship type still left) can use a
// board cell z as a ship cell only if some such placement covers z at all (V)
// and z's four grown cells z, z+1, z+GW, z+GW+1 are all still free. Call the
// set of such cells P; every future grown cell then lies in
// C = P + {0, 1, GW, GW+1}. canon() uses this to
//   * reject states with an uncovered hit outside P (it can never be covered);
//   * fill every real grown cell outside C into the mask. No future placement
//     can use such a cell, so states differing only there have the same
//     completions and now merge, and filled cells count as wasted area in
//     viable(), tightening the bound.
// Filling cannot remove a cell from P (a cell of P has all its grown cells in
// C), so canon is idempotent. The fill never hides a hit: an uncovered hit is
// in P (or the state is rejected), a covered one is already set. A placement
// that is legal for m never overlaps the filled cells, so transitions are the
// same for m and canon(m), and since P only shrinks as g advances and cells get
// taken, canonicalizing every stored and looked-up mask is consistent.
//
// Bits past the 64/128-bit window lie beyond every placement's reach and are
// always free, so the right shifts bring in ones there (the `top` masks).
template <typename MT>
struct Lookahead {
	static constexpr int BITS = sizeof(MT) * 8;
	int GW = 0;
	MT top1 = 0, topW = 0, topW1 = 0;  // window bits whose neighbour +1/+GW/+GW+1 lies past the window
	static constexpr int MAX_SPLIT = 10;  // up to this many ship types, V is kept per set of types left
	int nsub = 1;
	std::vector<MT> V;     // per layer and set of types left: cells a placement anchored at >= g can use as ship cell
	std::vector<MT> H;     // per layer: hit cells
	std::vector<MT> R;     // per layer: real grown cells (< G)

	void build(const Problem & p, const std::vector<uint8_t> & hit) {
		GW = p.W + 1;
		const int G = GW * (p.H + 1);
		const int T = (int)p.types.size();
		auto top = [](int k) { return k ? ~(MT)0 << (BITS - k) : (MT)0; };
		top1 = top(1), topW = top(GW), topW1 = top(GW + 1);
		// per type: last anchor (grown index) of a placement using grown cell z as a ship cell
		std::vector<std::vector<int>> last(T, std::vector<int>(G, -1));
		for (const Placement & pl : p.placements) {
			int a = (pl.anchor / p.W) * GW + pl.anchor % p.W;
			for (int i = 0; i < p.N; i++)
				if ((pl.cells >> i) & 1) {
					int z = (i / p.W) * GW + i % p.W;
					last[pl.type][z] = std::max(last[pl.type][z], a);
				}
		}
		nsub = T <= MAX_SPLIT ? 1 << T : 1;
		V.assign((size_t)(G + 1) * nsub, 0), H.assign(G + 1, 0), R.assign(G + 1, 0);
		for (int g = 0; g <= G; g++)
			for (int j = 0; j < BITS && g + j < G; j++) {
				const int z = g + j;
				R[g] |= (MT)1 << j;
				if (hit[z]) H[g] |= (MT)1 << j;
				for (int sub = 0; sub < nsub; sub++)
					for (int t = 0; t < T; t++)
						if ((nsub == 1 || (sub >> t) & 1) && last[t][z] >= g) V[(size_t)g * nsub + sub] |= (MT)1 << j;
			}
	}
	// Canonicalizes mask m of layer g in place; false if the state is dead.
	inline bool canon(int g, uint32_t sub, MT & m) const {
		const MT F = ~m;
		const MT P = V[(size_t)g * nsub + sub] & F & ((F >> 1) | top1) & ((F >> GW) | topW) & ((F >> (GW + 1)) | topW1);
		if (H[g] & F & ~P) return false;
		const MT C = P | (P << 1) | (P << GW) | (P << (GW + 1));
		m |= R[g] & ~C;
		return true;
	}
};

// Returns false if CT overflowed (caller retries with a wider type).
template <typename MT, typename CT>
static bool run_dp(const Problem & p, Result & res, DpStats * stats, int nthreads, size_t max_states) {
	const int T = (int)p.types.size();
	const int GW = p.W + 1, G = GW * (p.H + 1);  // grown board

	// Fleet encoding: mixed radix with digit t = remaining ships of type t.
	// States are grouped by fleet value; a state inside a group is just its mask.
	std::vector<uint32_t> radix(T), stride(T);
	uint64_t nfleet64 = 1;
	for (int t = 0; t < T; t++) {
		radix[t] = std::max(0, p.types[t].count) + 1;
		stride[t] = (uint32_t)nfleet64;
		nfleet64 *= radix[t];
		if (nfleet64 > (1u << 24)) throw std::invalid_argument("fleet too varied for the DP solver");
	}
	// every layer keeps one (small) group object per fleet value
	if (nfleet64 * (uint64_t)(G + 1) > (1u << 22)) throw std::invalid_argument("fleet too varied for the DP solver");
	const uint32_t nfleet = (uint32_t)nfleet64;
	auto digit = [&](uint32_t f, int t) { return (f / stride[t]) % radix[t]; };
	const uint32_t full = nfleet - 1;
	std::vector<int> need(nfleet, 0);  // grown area the remaining ships cover
	{
		std::vector<int> ga(T);
		for (int t = 0; t < T; t++) ga[t] = grown_area(p.types[t].shape);
		for (uint32_t f = 0; f < nfleet; f++)
			for (int t = 0; t < T; t++) need[f] += digit(f, t) * ga[t];
	}
	// A state at grown cell g is viable only if the free grown cells left
	// (G - g minus those already covered) can hold the remaining ships.
	auto viable = [&](int g, MT mask, uint32_t f) { return need[f] <= G - g - popc(mask); };

	// Transitions per grown cell and ship type.
	std::vector<std::vector<std::vector<Trans<MT>>>> at(G, std::vector<std::vector<Trans<MT>>>(T));
	std::vector<uint8_t> hit(G, 0);
	for (int i = 0; i < p.N; i++)
		if ((p.hits >> i) & 1) hit[(i / p.W) * GW + i % p.W] = 1;
	for (int k = 0; k < (int)p.placements.size(); k++) {
		const Placement & pl = p.placements[k];
		int a = (pl.anchor / p.W) * GW + pl.anchor % p.W;
		MT gm = 0;
		for (int i = 0; i < p.N; i++) {
			if (!((pl.cells >> i) & 1)) continue;
			int g = (i / p.W) * GW + i % p.W;
			for (int d : {0, 1, GW, GW + 1}) gm |= (MT)1 << (g + d - a);
		}
		at[a][pl.type].push_back(Trans<MT>{gm, k});
	}
	// Every mask is passed through la.canon before it is stored or looked up.
	Lookahead<MT> la;
	la.build(p, hit);
	// index of the set of ship types left in fleet f (into Lookahead::V)
	std::vector<uint32_t> left(nfleet, 0);
	if (la.nsub > 1)
		for (uint32_t f = 0; f < nfleet; f++)
			for (int t = 0; t < T; t++)
				if (digit(f, t)) left[f] |= 1u << t;
	// Sources feeding destination group d: d itself (no ship placed) and
	// d + stride[t] for every type t that still had a ship to place.
	std::vector<std::vector<int>> feeders(nfleet);
	for (uint32_t d = 0; d < nfleet; d++)
		for (int t = 0; t < T; t++)
			if (digit(d, t) + 1 < radix[t]) feeders[d].push_back(t);

	std::vector<std::vector<Group<MT, CT>>> L(G + 1);
	for (auto & l : L) l.resize(nfleet);
	MT m0 = 0;
	if (la.canon(0, left[full], m0) && viable(0, m0, full)) {
		L[0][full].mask.push_back(m0);
		L[0][full].fwd.push_back(1);
	}

	auto secs_since = [](std::chrono::steady_clock::time_point t) {
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
	};
	auto t_fwd = std::chrono::steady_clock::now();
	// ---- Forward pass: build layer g+1 one destination group at a time. ----
	std::vector<Accumulator<MT, CT>> acc(nthreads);
	std::atomic<bool> ovf(false);
	std::atomic<size_t> stored(0);
	// max_states is calibrated for 16-byte states (64-bit mask + 64-bit count);
	// scale it so wider masks/counts get the same memory budget.
	const size_t budget = (size_t)((long double)max_states * 16 / (sizeof(MT) + sizeof(CT)));
	for (auto & a : acc) {
		a.stored = &stored;
		a.budget = budget;
	}
	for (int g = 0; g < G; g++) {
		auto & cur = L[g];
		auto & nxt = L[g + 1];
		const bool h = hit[g];
		parallel_for(nthreads, nfleet, [&](size_t di, int tid) {
			const uint32_t d = (uint32_t)di;
			if (!viable(g + 1, 0, d)) return;
			Accumulator<MT, CT> & A = acc[tid];
			A.reset();
			// no ship anchored here
			const auto & same = cur[d];
			for (size_t s = 0; s < same.mask.size(); s++) {
				MT m = same.mask[s];
				if (!(m & 1) && h) continue;  // an uncovered hit must be covered now
				MT m2 = m >> 1;
				if (la.canon(g + 1, left[d], m2) && viable(g + 1, m2, d)) A.add(m2, same.fwd[s]);
			}
			// a ship of type t anchored here
			for (int t : feeders[d]) {
				const auto & trs = at[g][t];
				if (trs.empty()) continue;
				const auto & src = cur[d + stride[t]];
				for (size_t s = 0; s < src.mask.size(); s++) {
					MT m = src.mask[s];
					if (m & 1) continue;
					for (const auto & tr : trs) {
						if (tr.grown & m) continue;
						MT m2 = (m | tr.grown) >> 1;
						if (la.canon(g + 1, left[d], m2) && viable(g + 1, m2, d)) A.add(m2, src.fwd[s]);
					}
				}
			}
			if (A.ovf) ovf = true;
			A.report();
			nxt[d].mask.assign(A.masks.begin(), A.masks.end());
			nxt[d].fwd.assign(A.vals.begin(), A.vals.end());
		});
		if (ovf) return false;
	}
	std::vector<Accumulator<MT, CT>>().swap(acc);
	if (stats) stats->forward_s = secs_since(t_fwd);
	auto t_bwd = std::chrono::steady_clock::now();

	// ---- Backward pass, accumulating per-placement weights per thread. ----
	std::vector<std::vector<u128>> weight(nthreads, std::vector<u128>(p.placements.size(), 0));
	for (uint32_t f = 0; f < nfleet; f++) {
		auto & grp = L[G][f];
		grp.bwd.assign(grp.mask.size(), f == 0 ? 1 : 0);
		grp.index.build(grp.mask);
	}
	for (int g = G - 1; g >= 0; g--) {
		auto & cur = L[g];
		auto & nxt = L[g + 1];
		const bool h = hit[g];
		std::atomic<size_t> a_states(0), a_dead(0);
		parallel_for(nthreads, nfleet, [&](size_t fi, int tid) {
			const uint32_t f = (uint32_t)fi;
			auto & grp = cur[f];
			const size_t n = grp.mask.size();
			if (!n) return;
			grp.bwd.assign(n, 0);
			// destination masks are looked up in the same canonical form the
			// forward pass stored them in
			auto get = [&](uint32_t f2, MT m) -> CT {
				if (!la.canon(g + 1, left[f2], m)) return 0;
				const auto & ng = nxt[f2];
				uint32_t idx = ng.index.find(ng.mask, m);
				return idx == EMPTY ? 0 : ng.bwd[idx];
			};
			std::vector<u128> & W = weight[tid];
			size_t local_dead = 0;
			for (size_t s = 0; s < n; s++) {
				const MT m = grp.mask[s];
				CT b = 0;
				if ((m & 1) || !h) b = get(f, m >> 1);
				if (!(m & 1)) {
					const CT fw = grp.fwd[s];
					for (int t = 0; t < T; t++) {
						if (digit(f, t) == 0) continue;
						for (const auto & tr : at[g][t]) {
							if (tr.grown & m) continue;
							CT b2 = get(f - stride[t], (m | tr.grown) >> 1);
							if (!b2) continue;
							if (add_ovf(b, b2)) ovf = true;
							W[tr.pidx] += (u128)fw * (u128)b2;
						}
					}
				}
				grp.bwd[s] = b;
				local_dead += b == 0;
			}
			a_states += n;
			a_dead += local_dead;
		});
		if (ovf) return false;
		// layer g+1 is no longer needed; index layer g for the next step
		parallel_for(nthreads, nfleet, [&](size_t fi, int) {
			nxt[fi] = Group<MT, CT>();
			if (!cur[fi].mask.empty()) cur[fi].index.build(cur[fi].mask);
			std::vector<CT>().swap(cur[fi].fwd);
		});
		if (stats) {
			stats->max_layer_states = std::max(stats->max_layer_states, a_states.load());
			stats->total_states += a_states;
			stats->dead_states += a_dead;
		}
	}
	res.total = L[0][full].bwd.empty() ? 0 : L[0][full].bwd[0];
	if (stats) stats->backward_s = secs_since(t_bwd);

	for (size_t k = 0; k < p.placements.size(); k++) {
		u128 w = 0;
		for (int t = 0; t < nthreads; t++) w += weight[t][k];
		if (!w) continue;
		Mask c = p.placements[k].cells;
		for (int i = 0; i < p.N; i++)
			if ((c >> i) & 1) res.cell_counts[i] += w;
	}
	return true;
}

template <typename MT>
static void run_with_fallback(const Problem & p, Result & res, DpStats * stats, int nthreads, bool wide_counts,
                              size_t max_states) {
	if (!wide_counts && run_dp<MT, uint64_t>(p, res, stats, nthreads, max_states)) return;
	res.total = 0;
	res.cell_counts.assign(p.N, 0);
	if (stats) {
		bool tr = stats->transposed;
		*stats = DpStats();
		stats->transposed = tr;
	}
	if (!run_dp<MT, u128>(p, res, stats, nthreads, max_states)) throw std::overflow_error("DP counts overflow 128 bits");
}

// The same instance with rows and columns swapped.
static Problem transpose(const Problem & p) {
	auto tr = [&](Mask m) {
		Mask o = 0;
		for (int i = 0; i < p.N; i++)
			if ((m >> i) & 1) o |= bit((i % p.W) * p.H + i / p.W);
		return o;
	};
	Problem q = p;
	std::swap(q.W, q.H);
	q.hits = tr(p.hits);
	q.sunk_cells = tr(p.sunk_cells);
	for (auto & pl : q.placements) {
		pl.cells = tr(pl.cells);
		pl.halo = tr(pl.halo);
		pl.anchor = 0;
		while (!((pl.cells >> pl.anchor) & 1)) pl.anchor++;
	}
	// solve_dp does not rely on placement order, but keep the invariant
	std::stable_sort(q.placements.begin(), q.placements.end(),
	                 [](const Placement & a, const Placement & b) { return a.anchor < b.anchor; });
	return q;
}

}  // namespace

int default_threads() {
	unsigned n = std::thread::hardware_concurrency();
	return n ? (int)n : 1;
}

Result solve_dp(const Problem & p0, DpStats * stats, const DpOptions & opt) {
	int nthreads = opt.threads > 0 ? opt.threads : default_threads();
	Result res;
	res.cell_counts.assign(p0.N, 0);
	if (p0.infeasible) return res;
	if (stats) *stats = DpStats();

	// Scan along the longer side so the frontier (one row of the grown board
	// plus the ships' vertical reach) is as narrow as possible.
	bool transposed = opt.allow_transpose && p0.W > p0.H;
	Problem pt;
	if (transposed) pt = transpose(p0);
	const Problem & p = transposed ? pt : p0;
	if (stats) stats->transposed = transposed;

	int reach = max_reach(p);
	if (reach < 64) run_with_fallback<uint64_t>(p, res, stats, nthreads, opt.force_wide_counts, opt.max_states);
	else if (reach < 128) run_with_fallback<u128>(p, res, stats, nthreads, opt.force_wide_counts, opt.max_states);
	else throw std::invalid_argument("ship reach exceeds 128 grown cells; board too wide");

	if (transposed) {
		std::vector<u128> back(p0.N);
		for (int i = 0; i < p0.N; i++) back[i] = res.cell_counts[(i % p0.W) * p0.H + i / p0.W];
		res.cell_counts = back;
	}
	for (int i = 0; i < p0.N; i++)
		if ((p0.sunk_cells >> i) & 1) res.cell_counts[i] = res.total;
	return res;
}

}  // namespace bs
