#include "probability.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wildfire {

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
