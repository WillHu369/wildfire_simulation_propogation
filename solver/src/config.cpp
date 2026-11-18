#include "config.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace wildfire {
namespace {

struct NumberKey {
  const char* name;
  double SimConfig::*field;
};

constexpr NumberKey kNumberKeys[] = {
  {"cell_size_m", &SimConfig::cell_size_m},
  {"elevation_m", &SimConfig::elevation_m},
  {"moisture", &SimConfig::moisture},
  {"wind_speed_m_s", &SimConfig::wind_speed_m_s},
  {"wind_direction_deg", &SimConfig::wind_direction_deg},
  {"timestep_s", &SimConfig::timestep_s},
  {"max_duration_s", &SimConfig::max_duration_s},
  {"burn_duration_s", &SimConfig::burn_duration_s},
};

// Per-cell layers: constant key + file key.
struct LayerKeys {
  const char* constant_key;
  const char* file_key;
  std::string SimConfig::*file_field;
};

const LayerKeys kLayerKeys[] = {
  {"elevation_m", "elevation_file", &SimConfig::elevation_file},
  {"moisture", "moisture_file", &SimConfig::moisture_file},
  {"wind_speed_m_s", "wind_speed_file", &SimConfig::wind_speed_file},
  {"wind_direction_deg", "wind_direction_file", &SimConfig::wind_direction_file},
};

std::string trim(const std::string& s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const auto last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

[[noreturn]] void fail(const std::string& path, int line_no,
                       const std::string& msg) {
  throw std::runtime_error(path + ":" + std::to_string(line_no) + ": " + msg);
}

[[noreturn]] void fail(const std::string& path, const std::string& msg) {
  throw std::runtime_error(path + ": " + msg);
}

double parse_double(const std::string& value, const std::string& path,
                    int line_no) {
  std::istringstream in(value);
  double out = 0.0;
  std::string extra;
  if (!(in >> out) || (in >> extra) || !std::isfinite(out)) {
    fail(path, line_no, "expected a number, got '" + value + "'");
  }
  return out;
}

std::string format_default(double v) {
  std::ostringstream out;
  out << v;
  return out.str();
}

}  // namespace

SimConfig load_config(const std::string& path) {
  std::ifstream file(path);
  if (!file) throw std::runtime_error("cannot open config file: " + path);

  const std::filesystem::path base = std::filesystem::path(path).parent_path();
  auto resolve = [&](const std::string& v) { return (base / v).string(); };

  SimConfig cfg;
  std::set<std::string> seen;
  std::string line;
  int line_no = 0;

  while (std::getline(file, line)) {
    ++line_no;
    const auto hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    line = trim(line);
    if (line.empty()) continue;

    const auto eq = line.find('=');
    if (eq == std::string::npos) fail(path, line_no, "expected 'key = value'");
    const std::string key = trim(line.substr(0, eq));
    const std::string value = trim(line.substr(eq + 1));
    if (value.empty()) fail(path, line_no, "missing value for '" + key + "'");

    // Every key except "ignition" may appear only once.
    if (key != "ignition" && !seen.insert(key).second) {
      fail(path, line_no, "duplicate key '" + key + "'");
    }

    if (key == "terrain_file") {
      cfg.terrain_file = resolve(value);
      continue;
    }
    if (key == "ignition") {
      std::istringstream in(value);
      Ignition ig;
      std::string extra;
      if (!(in >> ig.row >> ig.col)) {
        fail(path, line_no, "ignition needs 'row col [time_s]'");
      }
      if (!(in >> ig.time_s)) {
        if (!in.eof()) fail(path, line_no, "bad ignition time '" + value + "'");
        ig.time_s = 0.0;
      } else if (in >> extra) {
        fail(path, line_no, "too many values for ignition: '" + value + "'");
      }
      cfg.ignitions.push_back(ig);
      seen.insert("ignition");
      continue;
    }
    if (key == "seed") {
      std::istringstream in(value);
      std::string extra;
      if (value[0] == '-' || !(in >> cfg.seed) || (in >> extra)) {
        fail(path, line_no, "seed must be a non-negative integer");
      }
      continue;
    }

    bool matched = false;
    for (const NumberKey& k : kNumberKeys) {
      if (key == k.name) {
        cfg.*(k.field) = parse_double(value, path, line_no);
        matched = true;
        break;
      }
    }
    for (const LayerKeys& k : kLayerKeys) {
      if (!matched && key == k.file_key) {
        cfg.*(k.file_field) = resolve(value);
        matched = true;
      }
    }
    if (!matched) fail(path, line_no, "unknown key '" + key + "'");
  }

  // Required keys.
  if (!seen.count("terrain_file")) fail(path, "missing required key 'terrain_file'");
  if (cfg.ignitions.empty()) {
    fail(path, "missing required key 'ignition' (at least one)");
  }

  // A layer is either a constant or a file, not both.
  for (const LayerKeys& k : kLayerKeys) {
    if (seen.count(k.constant_key) && seen.count(k.file_key)) {
      fail(path, std::string("set either '") + k.constant_key + "' or '" +
                 k.file_key + "', not both");
    }
  }

  // Record which optional settings fell back to defaults.
  for (const NumberKey& k : kNumberKeys) {
    bool from_file = false;
    for (const LayerKeys& layer : kLayerKeys) {
      if (k.name == std::string(layer.constant_key) && seen.count(layer.file_key)) {
        from_file = true;
      }
    }
    if (!seen.count(k.name) && !from_file) {
      cfg.defaults_used.push_back(std::string(k.name) + " = " +
                                  format_default(cfg.*(k.field)));
    }
  }
  if (!seen.count("seed")) {
    cfg.defaults_used.push_back("seed = " + std::to_string(cfg.seed));
  }

  // Range checks.
  if (!(cfg.cell_size_m > 0.0)) fail(path, "cell_size_m must be > 0");
  if (!(cfg.timestep_s > 0.0)) fail(path, "timestep_s must be > 0");
  if (cfg.max_duration_s < 0.0) fail(path, "max_duration_s must be >= 0");
  if (cfg.burn_duration_s < 0.0) fail(path, "burn_duration_s must be >= 0");
  if (cfg.moisture < 0.0 || cfg.moisture > 1.0) {
    fail(path, "moisture must be a fraction in [0, 1]");
  }
  if (cfg.wind_speed_m_s < 0.0) fail(path, "wind_speed_m_s must be >= 0");
  return cfg;
}

}  // namespace wildfire
