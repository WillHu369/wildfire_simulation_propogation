#pragma once

#include <vector>

#include "config.hpp"
#include "grid.hpp"

namespace wildfire {

constexpr double kNeverIgnited = -1.0;

// Everything the run produces. Per-cell vectors use the Grid layout.
// (How long a cell is SHOWN burning is the visualizer's concern.)
struct SimResult {
  std::vector<double> ignition_time_s;  // s since start, -1 = never
  double end_time_s = 0.0;              // last ignition, or the hard stop
  long long steps = 0;
  long long spread_attempts = 0;
  long long successful_spreads = 0;
  int cells_ignited = 0;
  bool hit_max_duration = false;
};

// In:  grid (Grid): static environment
//      cfg (SimConfig): ignitions, timestep, seed, max duration
// Out: SimResult (ignition grid, counters)
// Why: the serial simulation. Every timestep:
//   1. Activate cells whose scheduled ignition time has arrived.
//   2. Each NEWLY ignited cell tries each eligible neighbor ONCE (Eq. 1 +
//      random draw); successes schedule an ignition after the travel time
//      (Eq. 3-6, using the SOURCE cell's wind). The earliest arrival wins.
//   3. Previously ignited cells make no further attempts, so the run ends
//      as soon as no ignitions are pending (or at max_duration_s).
// Throws std::invalid_argument if an ignition is outside the grid, on a
// non-burnable cell, or at a negative time, before any simulation runs.
SimResult run_simulation(const Grid& grid, const SimConfig& cfg);

}  // namespace wildfire
