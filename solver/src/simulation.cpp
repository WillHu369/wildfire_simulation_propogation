#include "simulation.hpp"

#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>

#include "probability.hpp"
#include "rng.hpp"
#include "travel_time.hpp"

namespace wildfire {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Moore neighborhood, in order N, NE, E, SE, S, SW, W, NW.
// Row 0 is north, so "north" is row - 1.
constexpr int kDRow[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
constexpr int kDCol[8] = {0, 1, 1, 1, 0, -1, -1, -1};

constexpr long long kNoPending = std::numeric_limits<long long>::max();

// Converts a travel time to whole timesteps [OURS]: round to nearest, at
// least 1 step so fire never crosses a cell within the step it ignited.
long long delay_to_steps(double delay_s, double dt) {
  const long long steps = std::llround(delay_s / dt);
  return steps < 1 ? 1 : steps;
}

// Bearing of a neighbor offset, degrees clockwise from north.
double bearing_deg(int d_row, int d_col) {
  return std::atan2(static_cast<double>(d_col), static_cast<double>(-d_row)) *
         180.0 / kPi;
}

}  // namespace

SimResult run_simulation(const Grid& grid, const SimConfig& cfg) {
  if (!(cfg.timestep_s > 0.0)) {
    throw std::invalid_argument("timestep_s must be > 0");
  }
  if (cfg.max_duration_s < 0.0) {
    throw std::invalid_argument("max_duration_s must be >= 0");
  }

  const int n = grid.size();
  const double dt = cfg.timestep_s;

  SimResult res;
  res.ignition_time_s.assign(n, kNeverIgnited);

  // Earliest scheduled ignition step per cell, plus a min-heap of events.
  // A heap entry is stale if the cell already ignited or a newer, earlier
  // arrival replaced it; stale entries are skipped when popped.
  std::vector<long long> pending_step(n, kNoPending);
  using Event = std::pair<long long, int>;  // (step, cell)
  std::priority_queue<Event, std::vector<Event>, std::greater<Event>> events;
  int pending_cells = 0;

  auto schedule = [&](int cell, long long step) {
    if (step >= pending_step[cell]) return;  // keep the earlier arrival
    if (pending_step[cell] == kNoPending) ++pending_cells;
    pending_step[cell] = step;
    events.push({step, cell});
  };

  for (const Ignition& ig : cfg.ignitions) {
    const std::string where =
        "ignition (row " + std::to_string(ig.row) + ", col " +
        std::to_string(ig.col) + ")";
    if (!grid.in_bounds(ig.row, ig.col)) {
      throw std::invalid_argument(
          where + " is outside the " + std::to_string(grid.rows) + " x " +
          std::to_string(grid.cols) + " grid (rows/cols start at 0)");
    }
    const int cell = grid.index(ig.row, ig.col);
    if (!is_burnable(grid.terrain[cell])) {
      throw std::invalid_argument(
          where + " is on " + terrain_name(grid.terrain[cell]) +
          " terrain, which cannot burn");
    }
    if (ig.time_s < 0.0) {
      throw std::invalid_argument(where + " has a negative start time");
    }
    schedule(cell, std::llround(ig.time_s / dt));
  }

  const long long max_step =
      static_cast<long long>(std::floor(cfg.max_duration_s / dt + 1e-9));

  std::vector<int> newly_ignited;
  long long step = 0;

  for (; step <= max_step; ++step) {
    const double t = static_cast<double>(step) * dt;

    // 1. Activate cells whose scheduled ignition time has arrived.
    newly_ignited.clear();
    while (!events.empty() && events.top().first <= step) {
      const auto [event_step, cell] = events.top();
      events.pop();
      if (res.ignition_time_s[cell] != kNeverIgnited) continue;
      if (event_step != pending_step[cell]) continue;  // stale entry
      res.ignition_time_s[cell] = t;
      pending_step[cell] = kNoPending;
      --pending_cells;
      newly_ignited.push_back(cell);
    }
    res.cells_ignited += static_cast<int>(newly_ignited.size());

    // 2. Each newly ignited cell tries each eligible neighbor exactly once.
    for (const int src : newly_ignited) {
      const int row = src / grid.cols;
      const int col = src % grid.cols;
      const Terrain src_terrain = grid.terrain[src];

      for (int k = 0; k < 8; ++k) {
        const int nrow = row + kDRow[k];
        const int ncol = col + kDCol[k];
        if (!grid.in_bounds(nrow, ncol)) continue;
        const int tgt = grid.index(nrow, ncol);
        const Terrain tgt_terrain = grid.terrain[tgt];
        // Burning or burned-out cells are not targets. Cells with only a
        // pending ignition are, and may receive an earlier arrival.
        if (!is_burnable(tgt_terrain)) continue;
        if (res.ignition_time_s[tgt] != kNeverIgnited) continue;

        ++res.spread_attempts;

        // Slope from source to target: rise over horizontal run, + uphill.
        const bool diagonal = kDRow[k] != 0 && kDCol[k] != 0;
        const double run_m =
            grid.cell_size_m * (diagonal ? std::sqrt(2.0) : 1.0);
        const double rise_m = grid.elevation_m[tgt] - grid.elevation_m[src];
        const double tan_phi = rise_m / run_m;
        // Wind at the SOURCE cell, as in the CIMA reference code [REF].
        const double wind_speed = grid.wind_speed_m_s[src];
        const double wind_toward = grid.wind_direction_deg[src];
        const double bearing = bearing_deg(kDRow[k], kDCol[k]);

        // Eq. 1-2: alpha_wh = alpha_w * alpha_h; e_m from the TARGET cell's
        // fuel moisture, the fuel that has to catch.
        SpreadModifiers mods;
        mods.alpha_w = wind_probability_factor(wind_speed, wind_toward, bearing);
        mods.alpha_h = slope_probability_factor(tan_phi);
        mods.e_m = moisture_probability_factor(grid.moisture[tgt]);
        const double p = spread_probability(src_terrain, tgt_terrain, mods);
        const double r = spread_draw(cfg.seed, static_cast<std::uint64_t>(src),
                                     static_cast<std::uint64_t>(tgt));
        if (!spread_succeeds(p, r)) continue;

        ++res.successful_spreads;

        const double wind_along =
            wind_component_along(wind_speed, wind_toward, bearing);
        // The fire travels along the ground, including the climb [REF].
        const double distance = std::hypot(run_m, rise_m);
        // Moisture of the TARGET cell, as in the CIMA reference code [REF].
        const double delay_s = travel_time_seconds(
            distance, nominal_speed_m_per_min(src_terrain),
            wind_speed_factor(wind_along), slope_speed_factor(tan_phi),
            moisture_speed_factor(grid.moisture[tgt]));
        if (res.min_travel_time_s < 0.0 || delay_s < res.min_travel_time_s) {
          res.min_travel_time_s = delay_s;
        }
        if (delay_s < 2.0 * dt) ++res.short_crossings;

        schedule(tgt, step + delay_to_steps(delay_s, dt));
      }
    }

    // 3. Ignited cells never retry: spread is decided once, at ignition.
    // With nothing left scheduled, no further cell can ever ignite.
    if (pending_cells == 0) break;
  }

  res.hit_max_duration = step > max_step;
  if (res.hit_max_duration) step = max_step;
  res.steps = step + 1;
  res.end_time_s = static_cast<double>(step) * dt;
  return res;
}

bool timestep_too_coarse(const SimResult& result) {
  return result.successful_spreads > 0 &&
         static_cast<double>(result.short_crossings) >
             kMaxShortCrossingShare *
                 static_cast<double>(result.successful_spreads);
}

}  // namespace wildfire
