#pragma once

#include <string>
#include <vector>

#include "config.hpp"
#include "grid.hpp"
#include "simulation.hpp"

namespace wildfire {

// In:  kmh (double): wind speed in km/h, as in wind_speed_kmh.csv
// Out: double, m/s
double kmh_to_m_s(double kmh);

// In:  from_deg (double): direction the wind blows FROM, as in
//      wind_from_deg.csv (meteorological convention, cw from north)
// Out: double in [0, 360): direction it blows TOWARD
double wind_from_to_toward_deg(double from_deg);

// In:  cfg (SimConfig): paths of terrain_type.csv and the optional layers
// Out: Grid with every layer filled (rows * cols values each); missing
//      optional layers use neutral defaults (flat, dry, no wind)
// Why: build the static environment the simulation runs on.
// Throws std::runtime_error (with file:line) on: missing files, ragged rows,
// unknown terrain IDs, non-numeric values, a layer whose size differs from
// terrain_type.csv, or out-of-range values (moisture outside 0-1, negative
// wind speed).
Grid load_grid(const SimConfig& cfg);

// In:  path (string); rows, cols (int); values (vector<double>, row-major)
// Out: none; writes a rows x cols CSV, row 0 first, 3 decimals
// Why: ignition_time_s.csv, in the same format the visualizer writes.
void write_grid_csv(const std::string& path, int rows, int cols,
                    const std::vector<double>& values);

// In:  path (string); grid, cfg, result; wall_time_s (double, compute time)
// Out: none; writes metadata.json
// Why: tells the visualizer which model produced the run, and records the
//      settings used and the compute time for serial/parallel comparisons.
void write_metadata_json(const std::string& path, const Grid& grid,
                         const SimConfig& cfg, const SimResult& result,
                         double wall_time_s);

}  // namespace wildfire
