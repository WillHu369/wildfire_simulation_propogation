#pragma once

#include <vector>

#include "terrain.hpp"

namespace wildfire {

// Static per-cell environment. Every vector has rows * cols entries, stored
// row-major; row 0 is the NORTH edge, column 0 is the WEST edge [OURS].
// The flat index is also the global cell id used for random draws, so it
// must stay the same in the parallel version.
struct Grid {
  int rows = 0;
  int cols = 0;
  double cell_size_m = 20.0;               // cell width, m
  std::vector<Terrain> terrain;            // fuel type
  std::vector<double> elevation_m;         // m (slope -> travel time)
  std::vector<double> moisture;            // fraction 0-1 (travel time)
  // Wind, converted on load from the input files' km/h and "blowing FROM"
  // (wind_speed_kmh.csv, wind_from_deg.csv) to the units used internally:
  std::vector<double> wind_speed_m_s;      // m/s (travel time)
  std::vector<double> wind_direction_deg;  // blowing TOWARD, cw from north

  // Out: int, number of cells
  int size() const { return rows * cols; }
  // In: row, col (int) -> Out: int, flat index (also the global cell id)
  int index(int row, int col) const { return row * cols + col; }
  // In: row, col (int) -> Out: bool, true if inside the grid
  bool in_bounds(int row, int col) const {
    return row >= 0 && row < rows && col >= 0 && col < cols;
  }
};

}  // namespace wildfire
