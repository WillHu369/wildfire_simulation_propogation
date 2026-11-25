#pragma once

namespace wildfire {

// Travel time of the fire front between neighboring cells (PPR Eq. 3-6).
// With no wind, flat ground, and zero moisture every factor equals 1.

// PPR Eq. 5: K_w = exp(0.1783 * V)
// In:  wind_along_m_s (double): wind component along the spread direction,
//      m/s (negative when spreading against the wind)
// Out: double in [0.01, 10], wind speed-up factor K_w (clamped as in the
//      CIMA reference code [REF])
// Why: wind makes fire travel faster downwind, slower upwind.
double wind_speed_factor(double wind_along_m_s);

// PPR Eq. 6: K_phi = exp(3.533 * tan(phi)^1.2); downhill uses
// exp(sign(tan_phi) * 3.533 * |tan_phi|^1.2), and the result is clamped to
// [0.01, 10], both as in the CIMA reference code [REF].
// In:  tan_phi (double): rise / run from source to target
// Out: double in [0.01, 10], slope speed-up factor K_phi
// Why: fire runs faster uphill, slower downhill.
double slope_speed_factor(double tan_phi);

// PPR Eq. 4: f_m = exp(-0.014 * M)
// In:  moisture (double): fuel moisture, fraction 0-1
//      TODO(verify): units of M in the paper (the reference code uses a fraction)
// Out: double in (0, 1], moisture slow-down factor f_m
// Why: wet fuel slows the fire.
double moisture_speed_factor(double moisture);

// PPR Eq. 3: delta_t = d / (v_n * K_w * K_phi * f_m)
// In:  distance_m (double, m): along the ground, i.e. including the climb
//      (sqrt(run^2 + rise^2)), as in the reference code [REF];
//      v_n_m_per_min (double, m/min);
//      k_w, k_phi, f_m (double): factors from the functions above
// Out: double, travel time in SECONDS
// Why: how long until a successfully targeted neighbor ignites.
// Throws std::invalid_argument if the resulting speed is not positive.
double travel_time_seconds(double distance_m, double v_n_m_per_min,
                           double k_w, double k_phi, double f_m);

// In:  wind_speed_m_s (double, m/s)
//      wind_toward_deg (double): direction the wind blows TOWARD,
//      degrees clockwise from north [OURS]
//      bearing_deg (double): spread direction, degrees clockwise from north
// Out: double, wind component along the spread direction (m/s)
// Why: Eq. 5 needs only the part of the wind pushing along source -> target.
double wind_component_along(double wind_speed_m_s, double wind_toward_deg,
                            double bearing_deg);

}  // namespace wildfire
