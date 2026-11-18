#pragma once

#include "terrain.hpp"

namespace wildfire {

// Environmental modifiers of the spread probability (PPR Eq. 1-2).
// The paper gives these only as figures (Figs. 6, 7, 9). Until the project
// picks a documented form, the draft solver keeps them neutral (= 1).
struct SpreadModifiers {
  double alpha_w = 1.0;  // wind effect (PPR Fig. 7), >= 0
  double alpha_h = 1.0;  // slope effect (PPR Fig. 6), >= 0
  double e_m = 1.0;      // moisture effect (PPR Fig. 9), in [0, 1]
};

// PPR Eq. 1:  p = [1 - (1 - p_n)^(alpha_w * alpha_h)] * e_m
// In:  p_n (double, 0-1): nominal probability from Table 1
//      mods (SpreadModifiers): wind/slope/moisture factors
// Out: double in [0, 1], chance this spread attempt succeeds
// Why: core of the probability solver.
// Throws std::invalid_argument on out-of-range or NaN inputs.
double spread_probability(double p_n, const SpreadModifiers& mods = {});

// In:  source, target (Terrain); mods (SpreadModifiers)
// Out: double in [0, 1]; Table 1 lookup + Eq. 1 (0 if either can't burn)
// Why: convenience overload used by the simulation loop.
double spread_probability(Terrain source, Terrain target,
                          const SpreadModifiers& mods = {});

// In:  probability (double, 0-1); random_draw (double, uniform [0, 1))
// Out: bool, true iff random_draw < probability (strict) [OURS]
// Why: the stochastic yes/no decision for one spread attempt.
bool spread_succeeds(double probability, double random_draw);

}  // namespace wildfire
