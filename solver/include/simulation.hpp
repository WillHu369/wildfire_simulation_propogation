#pragma once

#include <vector>

#include "config.hpp"
#include "grid.hpp"

namespace wildfire {

constexpr double kNeverIgnited = -1.0;

// One row of stats.csv, recorded after each timestep.
struct StepStats {
  double time_s = 0.0;
  int burning = 0;        // cells currently shown as burning
  int burned_out = 0;     // cells whose burn duration has expired
  int new_ignitions = 0;  // cells that ignited during this step
  int pending = 0;        // cells with a scheduled future ignition
};

// Everything the run produces. Per-cell vectors use the Grid layout.
struct SimResult {
  std::vector<double> ignition_time_s;  // s since start, -1 = never
  std::vector<double> burn_duration_s;  // s, -1 = never
  std::vector<StepStats> stats;
  double end_time_s = 0.0;
  long long steps = 0;
  long long spread_attempts = 0;
  long long successful_spreads = 0;
  int cells_ignited = 0;
  bool hit_max_duration = false;
};

// In:  grid (Grid): static environment
//      cfg (SimConfig): ignitions, timestep, seed, max/burn duration
// Out: SimResult (ignition grid, burn durations, per-step stats, counters)
// Why: the serial simulation. Every timestep:
//   1. Activate cells whose scheduled ignition time has arrived.
//   2. Each NEWLY ignited cell tries each eligible neighbor ONCE (Eq. 1 +
//      random draw); successes schedule an ignition after the travel time
//      (Eq. 3-6, using the SOURCE cell's wind). The earliest arrival wins.
//   3. Previously ignited cells keep burning, with no repeated checks.
//   4. Cells whose burn duration has expired are extinguished.
// Throws std::invalid_argument if an ignition is outside the grid, on a
// non-burnable cell, or at a negative time, before any simulation runs.
SimResult run_simulation(const Grid& grid, const SimConfig& cfg);

}  // namespace wildfire
