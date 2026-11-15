// Frontier (transfer-matrix) dynamic programming solver.
//
// Cells are processed in row-major order. Every placement is decided at its
// anchor (its first cell in row-major order), so after processing cell i only
// two things about the past matter for the future:
//   * which cells ahead are already taken, and
//   * how many ships of each type are still to be placed.
// Many different partial configurations share the same state, which is what
// makes this exponentially faster than enumerating them.
//
// "Taken" is tracked on a grown board: every ship is grown by one cell to the
// right and down onto a (W+1)x(H+1) board. Two ships touch (or overlap) iff
// their grown shapes overlap, so one mask captures the no-touch rule, and the
// grown cells left over give an exact lower bound on wasted area used for
// pruning (the full Czech fleet covers 108 of the 121 grown cells of 10x10).
// A bitwise lookahead also marks grown cells no future ship can use as taken,
// which tightens that bound and merges states that differ only there.
//
// A forward pass counts the ways to reach each state, a backward pass counts
// the ways to complete it; fwd * bwd summed over every transition that uses a
// placement gives the number of configurations containing that placement,
// from which the per-cell counts follow.
//
// States are grouped by remaining fleet; a destination group only receives
// from itself and from groups with one more ship, so groups are built in
// parallel without locks.
#pragma once
#include "core.h"

namespace bs {

struct DpStats {
	size_t max_layer_states = 0;
	size_t total_states = 0;
	size_t dead_states = 0;  // states that cannot be completed (bwd == 0)
	bool transposed = false;
	double forward_s = 0, backward_s = 0;
};

struct DpOptions {
	int threads = 0;               // 0 = all hardware threads
	bool allow_transpose = true;   // scan along the longer side
	bool force_wide_counts = false;  // skip the uint64 attempt (testing)
	// Abort with std::runtime_error once the forward pass has stored more
	// states than this. Calibrated for 16-byte states (~24 GB by default);
	// scaled down when 128-bit masks or counts are needed. The 10x10 game
	// fleet needs ~3e8.
	size_t max_states = 1500000000;
};

int default_threads();
Result solve_dp(const Problem & p, DpStats * stats = nullptr, const DpOptions & opt = DpOptions());

}  // namespace bs
