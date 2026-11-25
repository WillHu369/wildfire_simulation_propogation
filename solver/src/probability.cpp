#include "probability.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wildfire {

double slope_probability_factor(double tan_phi) {
  const double sign = tan_phi > 0.0 ? 1.0 : (tan_phi < 0.0 ? -1.0 : 0.0);
  const double s = 3.0 * tan_phi;
  return std::pow(2.0, std::tanh(s * s * sign));
}

double wind_probability_factor(double wind_speed_m_s, double wind_toward_deg,
                               double bearing_deg) {
  // Reference-code constants D1..D5 [REF].
  constexpr double kD1 = 0.5, kD2 = 1.4, kD3 = 8.2, kD4 = 2.0, kD5 = 50.0;
  constexpr double kMaxWindKmh = 60.0;
  constexpr double kPi = 3.14159265358979323846;
  // Chosen so that module = 1 (no effect) when there is no wind.
  static const double kA = 1.0 - kD1 * (kD2 * std::tanh(-kD4));

  const double v = std::clamp(wind_speed_m_s * 3.6, 0.0, kMaxWindKmh);
  const double module = kA + kD1 * (kD2 * std::tanh(v / kD3 - kD4)) + v / kD5;
  const double a = (module - 1.0) / 4.0;
  const double theta = (wind_toward_deg - bearing_deg) * kPi / 180.0;
  return (a + 1.0) * (1.0 - a * a) / (1.0 - a * std::cos(theta));
}

double spread_probability(double p_n, const SpreadModifiers& mods) {
  if (!std::isfinite(p_n) || p_n < 0.0 || p_n > 1.0) {
    throw std::invalid_argument("spread_probability: p_n must be in [0, 1]");
  }
  if (!std::isfinite(mods.alpha_w) || mods.alpha_w < 0.0 ||
      !std::isfinite(mods.alpha_h) || mods.alpha_h < 0.0) {
    throw std::invalid_argument(
        "spread_probability: alpha_w and alpha_h must be finite and >= 0");
  }
  if (!std::isfinite(mods.e_m) || mods.e_m < 0.0 || mods.e_m > 1.0) {
    throw std::invalid_argument("spread_probability: e_m must be in [0, 1]");
  }

  // Eq. 2: the combined exponent alpha_wh = alpha_w * alpha_h.
  const double alpha_wh = mods.alpha_w * mods.alpha_h;
  // Eq. 1.
  const double p = (1.0 - std::pow(1.0 - p_n, alpha_wh)) * mods.e_m;
  return std::clamp(p, 0.0, 1.0);
}

double spread_probability(Terrain source, Terrain target,
                          const SpreadModifiers& mods) {
  if (!is_burnable(source) || !is_burnable(target)) return 0.0;
  return spread_probability(nominal_probability(source, target), mods);
}

bool spread_succeeds(double probability, double random_draw) {
  return random_draw < probability;
}

}  // namespace wildfire
