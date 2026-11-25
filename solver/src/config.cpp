#include "config.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "json.hpp"

namespace fs = std::filesystem;

namespace wildfire {
namespace {

struct NumberKey {
  const char* name;
  double SimConfig::*field;
};

constexpr NumberKey kNumberKeys[] = {
  {"cell_size_m", &SimConfig::cell_size_m},
  {"timestep_s", &SimConfig::timestep_s},
  {"max_duration_s", &SimConfig::max_duration_s},
};

// Keys in settings.json that only the visualizer uses. Accepted silently.
constexpr const char* kVisualizerKeys[] = {"burn_duration_s"};

// Largest integer a JSON number (a double) holds exactly.
constexpr double kMaxExactInteger = 9007199254740992.0;  // 2^53

[[noreturn]] void fail(const std::string& path, int line,
                       const std::string& msg) {
  throw std::runtime_error(path + ":" + std::to_string(line) + ": " + msg);
}

[[noreturn]] void fail(const std::string& path, const std::string& msg) {
  throw std::runtime_error(path + ": " + msg);
}

std::string read_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("cannot open settings file: " + path);
  std::ostringstream text;
  text << file.rdbuf();
  std::string s = text.str();
  // Tolerate a UTF-8 byte-order mark (some Windows editors add one).
  if (s.size() >= 3 && s.compare(0, 3, "\xEF\xBB\xBF") == 0) s.erase(0, 3);
  return s;
}

bool is_integer(const JsonValue& v) {
  return v.is_number() && std::floor(v.number) == v.number;
}

std::string format_number(double v) {
  std::ostringstream out;
  out << v;
  return out.str();
}

std::vector<Ignition> parse_ignitions(const JsonValue& points,
                                      const std::string& path) {
  if (!points.is_array()) {
    fail(path, points.line,
         std::string("\"ignition_points\" must be an array of [row, col] "
                     "pairs, got ") + json_type_name(points.type));
  }
  if (points.array.empty()) {
    fail(path, points.line, "\"ignition_points\" needs at least one [row, col]");
  }
  std::vector<Ignition> out;
  for (std::size_t i = 0; i < points.array.size(); ++i) {
    const JsonValue& p = points.array[i];
    const std::string which = "ignition_points[" + std::to_string(i) + "]";
    if (!p.is_array() || p.array.size() != 2) {
      fail(path, p.line, which + " must be a [row, col] pair");
    }
    for (const JsonValue& coord : p.array) {
      if (!is_integer(coord) || coord.number < 0.0 ||
          coord.number > std::numeric_limits<int>::max()) {
        fail(path, coord.line,
             which + " row and col must be non-negative integers");
      }
    }
    Ignition ig;
    ig.row = static_cast<int>(p.array[0].number);
    ig.col = static_cast<int>(p.array[1].number);
    out.push_back(ig);
  }
  return out;
}

}  // namespace

SimConfig load_settings(const std::string& path) {
  // Accept either the scenario folder or the settings.json inside it.
  fs::path dir = path;
  fs::path settings_path;
  if (fs::is_directory(dir)) {
    settings_path = dir / kSettingsFile;
  } else {
    settings_path = dir;
    dir = settings_path.parent_path();
  }
  if (dir.empty()) dir = ".";
  const std::string spath = settings_path.string();

  const JsonValue root = parse_json(read_file(spath), spath);
  if (!root.is_object()) {
    fail(spath, root.line, std::string("expected a JSON object, got ") +
                               json_type_name(root.type));
  }

  SimConfig cfg;
  cfg.scenario_dir = dir.string();
  cfg.scenario_name = fs::weakly_canonical(dir).filename().string();

  // Required.
  const JsonValue* points = root.find("ignition_points");
  if (!points) fail(spath, "missing required key \"ignition_points\"");
  cfg.ignitions = parse_ignitions(*points, spath);

  // Optional numbers.
  for (const NumberKey& k : kNumberKeys) {
    const JsonValue* v = root.find(k.name);
    if (!v) {
      cfg.notes.push_back(std::string("not set in settings.json, using default ") +
                          k.name + " = " + format_number(cfg.*(k.field)));
      continue;
    }
    if (!v->is_number()) {
      fail(spath, v->line, std::string("\"") + k.name + "\" must be a number, got " +
                               json_type_name(v->type));
    }
    cfg.*(k.field) = v->number;
  }

  if (const JsonValue* seed = root.find("seed")) {
    if (!is_integer(*seed) || seed->number < 0.0 ||
        seed->number > kMaxExactInteger) {
      fail(spath, seed->line, "\"seed\" must be a non-negative integer");
    }
    cfg.seed = static_cast<std::uint64_t>(seed->number);
  } else {
    cfg.notes.push_back("not set in settings.json, using default seed = " +
                        std::to_string(cfg.seed));
  }

  // Keys the solver does not use. Visualizer keys are expected; anything
  // else is probably a typo, so say so (but still run).
  for (const auto& member : root.object) {
    const std::string& key = member.first;
    bool known = key == "ignition_points" || key == "seed";
    for (const NumberKey& k : kNumberKeys) known = known || key == k.name;
    for (const char* k : kVisualizerKeys) known = known || key == k;
    if (!known) {
      cfg.notes.push_back("ignoring unknown key \"" + key + "\" in " + spath);
    }
  }

  // Range checks.
  if (!(cfg.cell_size_m > 0.0)) fail(spath, "cell_size_m must be > 0");
  if (!(cfg.timestep_s > 0.0)) fail(spath, "timestep_s must be > 0");
  if (cfg.max_duration_s < 0.0) fail(spath, "max_duration_s must be >= 0");

  // Per-cell layer files next to settings.json.
  const fs::path terrain = dir / kTerrainFile;
  if (!fs::exists(terrain)) {
    throw std::runtime_error("missing required file: " + terrain.string());
  }
  cfg.terrain_file = terrain.string();

  struct OptionalLayer {
    const char* file;
    std::string SimConfig::*field;
    const char* fallback;
  };
  const OptionalLayer layers[] = {
    {kElevationFile, &SimConfig::elevation_file, "flat terrain (0 m)"},
    {kMoistureFile, &SimConfig::moisture_file, "dry fuel (moisture 0)"},
    {kWindSpeedFile, &SimConfig::wind_speed_file, "no wind (0 km/h)"},
    {kWindFromFile, &SimConfig::wind_from_file, "wind from the north (0 deg)"},
  };
  for (const OptionalLayer& layer : layers) {
    const fs::path file = dir / layer.file;
    if (fs::exists(file)) {
      cfg.*(layer.field) = file.string();
    } else {
      cfg.notes.push_back(std::string(layer.file) + " not found, using " +
                          layer.fallback);
    }
  }
  return cfg;
}

}  // namespace wildfire
