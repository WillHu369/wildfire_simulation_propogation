#pragma once

#include <string>
#include <vector>

#include "config.hpp"
#include "grid.hpp"
#include "simulation.hpp"

namespace wildfire {

// In:  cfg (SimConfig): terrain file path plus constants/files for the
//      elevation, moisture, wind speed and wind direction layers
// Out: Grid with every layer filled (rows * cols values each)
// Why: build the static environment the simulation runs on.
// Throws std::runtime_error (with file:line) on: missing files, ragged rows,
// unknown terrain IDs, non-numeric values, a layer whose size differs from
// terrain.csv, or out-of-range values (moisture outside 0-1, negative wind).
Grid load_grid(const SimConfig& cfg);

// In:  path (string); rows, cols (int); values (vector<double>, row-major)
// Out: none; writes a rows x cols CSV, row 0 first
// Why: ignition_time.csv and burn_duration.csv.
void write_grid_csv(const std::string& path, int rows, int cols,
                    const std::vector<double>& values);

// In:  path (string); stats (vector<StepStats>)
// Out: none; writes stats.csv, one row per timestep
// Why: optional summary for plots (burning/burned counts over time).
void write_stats_csv(const std::string& path,
                     const std::vector<StepStats>& stats);

// In:  path (string); grid, cfg, result; config_path (string);
//      wall_time_s (double, compute time)
// Out: none; writes metadata.json
// Why: tells the visualizer the grid layout, units, sentinels and settings.
void write_metadata_json(const std::string& path, const Grid& grid,
                         const SimConfig& cfg, const SimResult& result,
                         const std::string& config_path,
                         double wall_time_s);

}  // namespace wildfire
