#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wildfire {

struct Ignition {
  int row = 0;
  int col = 0;
  double time_s = 0.0;
};

// Everything read from config.txt. File paths are resolved relative to the
// config file's folder.
struct SimConfig {
  // Required.
  std::string terrain_file;
  std::vector<Ignition> ignitions;

  // Per-cell layers: one constant for every cell, OR a CSV file with the
  // same shape as terrain.csv. Setting both for one layer is an error.
  double elevation_m = 0.0;          std::string elevation_file;
  double moisture = 0.0;             std::string moisture_file;
  double wind_speed_m_s = 0.0;       std::string wind_speed_file;
  double wind_direction_deg = 0.0;   std::string wind_direction_file;

  // Run settings.
  double cell_size_m = 20.0;
  double timestep_s = 1.0;
  std::uint64_t seed = 67;           // default seed [OURS]
  double max_duration_s = 3600.0;
  double burn_duration_s = 60.0;     // fixed visual burn time [OURS]

  // Optional keys missing from the file, as "key = default value".
  std::vector<std::string> defaults_used;
};

// In:  path (string) to a "key = value" config file (see solver/README.md)
// Out: SimConfig with defaults filled in for missing optional keys
// Why: single entry point for run settings.
// Throws std::runtime_error naming the file and line on: unknown or
// duplicate keys, empty or non-numeric values, a layer given as both a
// constant and a file, missing terrain_file or ignition, out-of-range values.
SimConfig load_config(const std::string& path);

}  // namespace wildfire
