// Frontier (transfer-matrix) dynamic programming solver.
//
// Cells are processed in row-major order. Every placement is decided at its
// anchor (its first cell in row-major order), so after processing cell i only
// two things about the past matter for the future:
//   * which cells >= i are already blocked (ship cells or their surroundings),
//     stored as a 64-bit mask relative to i, and
//   * how many ships of each type are still to be placed.
// Many different partial configurations share the same (mask, fleet) state,
// which is what makes this exponentially faster than enumerating them.
//
// A forward pass counts the ways to reach each state, a backward pass counts
// the ways to complete it; fwd * bwd summed over every transition that uses a
// placement gives the number of configurations containing that placement,
// from which the per-cell counts follow.
#pragma once
#include "core.h"

namespace bs {

struct DpStats {
	size_t max_layer_states = 0;
	size_t total_states = 0;
	size_t dead_states = 0;  // states that cannot be completed (bwd == 0)
};

Result solve_dp(const Problem & p, DpStats * stats = nullptr);

}  // namespace bs
