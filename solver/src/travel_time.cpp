#include "travel_time.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wildfire {
namespace {

constexpr double kPi = 3.14159265358979323846;

// PPR constants.
constexpr double kWindCoeff = 0.1783;      // Eq. 5
constexpr double kSlopeCoeff = 3.533;      // Eq. 6
constexpr double kSlopeExponent = 1.2;     // Eq. 6
// Bounds on the Eq. 6 factor, as in the reference code's Wang model [REF].
// Eq. 6 is exponential, so without them steep or cliff-like cells give
// absurd speeds. The upper bound takes effect above about 30 degrees.
constexpr double kMinSlopeFactor = 0.01;
constexpr double kMaxSlopeFactor = 10.0;
constexpr double kMoistureCoeff = -0.014;  // Eq. 4

}  // namespace

double wind_speed_factor(double wind_along_m_s) {
  return std::exp(kWindCoeff * wind_along_m_s);
}

double slope_speed_factor(double tan_phi) {
  if (tan_phi == 0.0) return 1.0;
  const double sign = tan_phi > 0.0 ? 1.0 : -1.0;
  const double k = std::exp(sign * kSlopeCoeff *
                            std::pow(std::fabs(tan_phi), kSlopeExponent));
  return std::clamp(k, kMinSlopeFactor, kMaxSlopeFactor);
}

double moisture_speed_factor(double moisture) {
  return std::exp(kMoistureCoeff * moisture);
}

double travel_time_seconds(double distance_m, double v_n_m_per_min,
                           double k_w, double k_phi, double f_m) {
  const double v_prop = v_n_m_per_min * k_w * k_phi * f_m;
  if (!(v_prop > 0.0) || !std::isfinite(v_prop) || !(distance_m >= 0.0)) {
    throw std::invalid_argument(
        "travel_time_seconds: need distance >= 0 and a positive speed");
  }
  return distance_m / v_prop * 60.0;  // minutes -> seconds
}

double wind_component_along(double wind_speed_m_s, double wind_toward_deg,
                            double bearing_deg) {
  const double diff_rad = (wind_toward_deg - bearing_deg) * kPi / 180.0;
  return wind_speed_m_s * std::cos(diff_rad);
}

}  // namespace wildfire
