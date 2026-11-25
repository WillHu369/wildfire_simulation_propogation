#pragma once

#include "terrain.hpp"

namespace wildfire {

// Environmental modifiers of the spread probability (PPR Eq. 1-2).
// The paper gives these only as figures (Figs. 6, 7, 9). All three use
// closed forms from the reference code: slope_probability_factor,
// wind_probability_factor and moisture_probability_factor below.
struct SpreadModifiers {
  double alpha_w = 1.0;  // wind effect (PPR Fig. 7), >= 0
  double alpha_h = 1.0;  // slope effect (PPR Fig. 6), >= 0
  double e_m = 1.0;      // moisture effect (PPR Fig. 9), in [0, 1]
};

// PPR Fig. 6, topographic factor alpha_h. The paper only plots it; this is
// the formula from the CIMA reference code (propagator_sim, w_h_effect),
// which reproduces the figure exactly [REF]:
//   alpha_h = 2^tanh((3 * tan_phi)^2 * sign(tan_phi))
// In:  tan_phi (double): rise / run from source to target (0.5 = 50 %
//      slope); positive uphill
// Out: double in [0.5, 2]: 1 on flat ground, 2 steep uphill, 0.5 steep
//      downhill; flat near 0 and saturated beyond about +-60 %
// Why: fire is more likely to spread uphill than downhill.
double slope_probability_factor(double tan_phi);

// PPR Fig. 7, wind factor alpha_w, as in the CIMA reference code
// (propagator_sim, w_h_effect) [REF]. Wind speed v in km/h, capped at 60:
//   module = A + 0.5 * 1.4 * tanh(v / 8.2 - 2) + v / 50   (A: module = 1 at v = 0)
//   a      = (module - 1) / 4                              (0 calm .. 0.644)
//   alpha_w = (1 + a)(1 - a^2) / (1 - a cos(theta))
// theta is the angle between where the wind blows and the spread direction.
// The last line is an ellipse in polar form: the fire is stretched downwind.
// Matches Fig. 7 up to about 20 km/h; above that the figure grows further
// (to ~4 at 100 km/h) while the code is capped at 2.7. We follow the code.
// In:  wind_speed_m_s (double, m/s, >= 0; converted to km/h internally)
//      wind_toward_deg (double): direction the wind blows TOWARD, cw from N
//      bearing_deg (double): spread direction source -> target, cw from N
// Out: double: 1 with no wind; up to 2.70 straight downwind and down to
//      0.586 straight upwind (at >= 60 km/h)
// Why: fire is more likely to spread downwind than upwind.
double wind_probability_factor(double wind_speed_m_s, double wind_toward_deg,
                               double bearing_deg);

// Moisture of extinction: fuel wetter than this does not carry fire (PPR).
constexpr double kMoistureOfExtinction = 0.3;

// PPR Fig. 9, moisture factor e_m, as the CIMA reference code computes it by
// default (p_moisture_trucchia) [REF], with x = moisture / moisture of
// extinction:
//   e_m = clamp(-11.507 x^5 + 22.963 x^4 - 17.331 x^3 + 6.598 x^2
//               - 1.7211 x + 1.0003, 0, 1)
// This does NOT match Fig. 9 exactly: the figure is the reference code's
// optional cubic (p_moisture_baghino) on the same ratio, and the curves
// differ by up to 0.19 in the middle (0.78 vs 0.62 at 8 % moisture). The
// project chose the code's default, as it did for wind.
// In:  moisture (double): dead fine fuel moisture of the TARGET cell, as a
//      fraction (0.08 = 8 %)
// Out: double in [0, 1]: 1 when dry, 0.78 at 8 %, ~0 at 30 % and above;
//      never increases with moisture
// Why: wet fuel is less likely to catch.
double moisture_probability_factor(double moisture);

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
