#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wildfire {

struct Ignition {
  int row = 0;
  int col = 0;
  double time_s = 0.0;  // settings.json ignitions all start at t = 0
};

// File names inside a scenario folder (inputs/examples/<name>/).
constexpr const char* kSettingsFile = "settings.json";
constexpr const char* kTerrainFile = "terrain_type.csv";
constexpr const char* kElevationFile = "elevation_m.csv";
constexpr const char* kMoistureFile = "fuel_moisture.csv";
constexpr const char* kWindSpeedFile = "wind_speed_kmh.csv";
constexpr const char* kWindFromFile = "wind_from_deg.csv";

// Everything read from a scenario folder: settings.json plus the paths of
// the per-cell CSV layers next to it.
struct SimConfig {
  std::string scenario_dir;   // folder holding settings.json
  std::string scenario_name;  // folder name, e.g. "size_032"

  // Per-cell layers, same shape as terrain_type.csv. terrain_file is
  // required; an empty path for any other layer means "file not present,
  // use the neutral default" (flat, dry, no wind).
  std::string terrain_file;
  std::string elevation_file;   // m
  std::string moisture_file;    // fraction 0-1
  std::string wind_speed_file;  // km/h
  std::string wind_from_file;   // direction wind blows FROM, cw from north

  // From settings.json.
  std::vector<Ignition> ignitions;  // "ignition_points": [[row, col], ...]
  double cell_size_m = 20.0;
  double timestep_s = 1.0;
  std::uint64_t seed = 67;          // default seed [OURS]
  double max_duration_s = 3600.0;

  // Things the user should know about: defaults applied, optional layer
  // files not found, settings keys the solver does not use.
  std::vector<std::string> notes;
};

// In:  path (string): a scenario folder, or the settings.json inside one
// Out: SimConfig with defaults filled in for missing optional settings
// Why: single entry point for run settings and input file locations.
// Throws std::runtime_error naming the file on: unreadable or malformed
// JSON, a missing or empty "ignition_points", ignition points that are not
// [row, col] pairs of non-negative integers, settings of the wrong type,
// out-of-range values, or a missing terrain_type.csv.
SimConfig load_settings(const std::string& path);

}  // namespace wildfire
