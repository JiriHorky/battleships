#include "core.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace bs {

std::string to_string(u128 v) {
	if (v == 0) return "0";
	std::string s;
	while (v) {
		s.push_back('0' + (int)(v % 10));
		v /= 10;
	}
	std::reverse(s.begin(), s.end());
	return s;
}

int Shape::size() const {
	int n = 0;
	for (auto c : cells) n += c;
	return n;
}

Shape make_shape(int w, int h, const std::vector<int> & rows) {
	if ((int)rows.size() != w * h) throw std::invalid_argument("shape size mismatch");
	Shape s;
	s.w = w;
	s.h = h;
	for (int v : rows) s.cells.push_back(v ? 1 : 0);
	for (int r = 0; r < h; r++) {
		bool any = false;
		for (int c = 0; c < w; c++) any |= s.at(r, c);
		if (!any) throw std::invalid_argument("shape has empty row");
	}
	for (int c = 0; c < w; c++) {
		bool any = false;
		for (int r = 0; r < h; r++) any |= s.at(r, c);
		if (!any) throw std::invalid_argument("shape has empty column");
	}
	return s;
}

Shape rotate90(const Shape & s) {
	// clockwise: new(r, c) = old(h-1-c, r)
	Shape o;
	o.w = s.h;
	o.h = s.w;
	o.cells.resize(o.w * o.h);
	for (int r = 0; r < o.h; r++)
		for (int c = 0; c < o.w; c++) o.cells[r * o.w + c] = s.at(s.h - 1 - c, r);
	return o;
}

std::vector<Shape> orientations(const Shape & s, bool rotate) {
	std::vector<Shape> out{s};
	if (!rotate) return out;
	Shape cur = s;
	for (int i = 0; i < 3; i++) {
		cur = rotate90(cur);
		if (std::find(out.begin(), out.end(), cur) == out.end()) out.push_back(cur);
	}
	return out;
}

bool shape_mask(int W, int H, const Shape & s, int r, int c, Mask & out) {
	if (r < 0 || c < 0 || r + s.h > H || c + s.w > W) return false;
	out = 0;
	for (int dr = 0; dr < s.h; dr++)
		for (int dc = 0; dc < s.w; dc++)
			if (s.at(dr, dc)) out |= bit((r + dr) * W + c + dc);
	return true;
}

Mask halo_of(int W, int H, Mask cells) {
	Mask out = 0;
	for (int i = 0; i < W * H; i++) {
		if (!(cells & bit(i))) continue;
		int r = i / W, c = i % W;
		for (int dr = -1; dr <= 1; dr++)
			for (int dc = -1; dc <= 1; dc++) {
				int rr = r + dr, cc = c + dc;
				if (rr >= 0 && rr < H && cc >= 0 && cc < W) out |= bit(rr * W + cc);
			}
	}
	return out;
}

static int lowest_bit(Mask m) {
	uint64_t lo = (uint64_t)m;
	if (lo) return __builtin_ctzll(lo);
	return 64 + __builtin_ctzll((uint64_t)(m >> 64));
}

Problem make_problem(const GameState & gs, const Fleet & fleet, bool rotate) {
	Problem p;
	p.W = gs.W;
	p.H = gs.H;
	p.N = gs.W * gs.H;
	if (p.N > 128) throw std::invalid_argument("board larger than 128 cells");
	p.types = fleet;
	p.hits = 0;
	p.sunk_cells = 0;

	Mask forbidden = 0;  // no remaining ship cell may be here
	for (int i = 0; i < p.N; i++) {
		if (gs.cells[i] == MISS) forbidden |= bit(i);
		if (gs.cells[i] == HIT) p.hits |= bit(i);
	}
	for (const auto & s : gs.sunk) {
		if (s.type < 0 || s.type >= (int)p.types.size()) throw std::invalid_argument("bad sunk type");
		Mask m = 0;
		for (int c : s.cells) {
			if (c < 0 || c >= p.N) throw std::invalid_argument("sunk ship cell out of range");
			m |= bit(c);
		}
		// the cells must form the declared ship in one of its allowed orientations
		bool shape_ok = false;
		for (const auto & o : orientations(p.types[s.type].shape, rotate))
			for (int r = 0; r + o.h <= p.H && !shape_ok; r++)
				for (int c = 0; c + o.w <= p.W && !shape_ok; c++) {
					Mask om;
					shape_mask(p.W, p.H, o, r, c, om);
					shape_ok = om == m && popcount(m) == (int)s.cells.size();
				}
		if (!shape_ok) p.infeasible = true;
		if (m & forbidden) p.infeasible = true;  // sunk ship on a miss
		if (--p.types[s.type].count < 0) p.infeasible = true;
		Mask h = halo_of(p.W, p.H, m);
		if (h & p.sunk_cells) p.infeasible = true;  // two sunk ships touching
		p.sunk_cells |= m;
		forbidden |= h;
	}
	if (p.hits & forbidden & ~p.sunk_cells) p.infeasible = true;  // hit next to a sunk ship
	p.hits &= ~p.sunk_cells;

	for (int t = 0; t < (int)p.types.size(); t++) {
		if (p.types[t].count <= 0) continue;
		for (const auto & o : orientations(p.types[t].shape, rotate)) {
			for (int r = 0; r + o.h <= p.H; r++)
				for (int c = 0; c + o.w <= p.W; c++) {
					Mask cells;
					shape_mask(p.W, p.H, o, r, c, cells);
					if (cells & forbidden) continue;
					Mask halo = halo_of(p.W, p.H, cells);
					// a hit next to (but not under) this ship could never be covered
					if (halo & ~cells & p.hits) continue;
					p.placements.push_back(Placement{t, lowest_bit(cells), cells, halo});
				}
		}
	}
	std::stable_sort(p.placements.begin(), p.placements.end(),
	                 [](const Placement & a, const Placement & b) { return a.anchor < b.anchor; });
	return p;
}

void print_heatmap(const Problem & p, const Result & r, bool percent) {
	int digits = 1;
	if (!percent) {
		u128 mx = 0;
		for (auto v : r.cell_counts) mx = std::max(mx, v);
		digits = (int)to_string(mx).size();
	} else {
		digits = 5;
	}
	int tw = p.W * (digits + 1) + 1;
	for (int i = 0; i < tw; i++) putchar('-');
	for (int row = 0; row < p.H; row++) {
		printf("\n|");
		for (int c = 0; c < p.W; c++) {
			u128 v = r.cell_counts[row * p.W + c];
			if (percent) {
				double pr = r.total ? 100.0 * (double)v / (double)r.total : 0.0;
				printf("%*.1f|", digits, pr);
			} else {
				printf("%*s|", digits, to_string(v).c_str());
			}
		}
	}
	printf("\n");
	for (int i = 0; i < tw; i++) putchar('-');
	printf("\n");
}

}  // namespace bs
