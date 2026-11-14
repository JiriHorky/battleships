// Reference solver: plain backtracking over the precomputed placements.
// Deliberately simple so it can serve as the test oracle for faster solvers.
#pragma once
#include "core.h"

namespace bs {
Result solve_brute(const Problem & p);
}
