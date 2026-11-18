#include "io.hpp"

#include <cmath>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>

namespace wildfire {
namespace {

std::ofstream open_out(const std::string& path) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot write file: " + path);
  return out;
}

std::string json_escape(const std::string& s) {
  std::string out;
  for (const char ch : s) {
    switch (ch) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      default: out += ch;
    }
  }
  return out;
}

[[noreturn]] void fail_at(const std::string& path, int line_no,
                          const std::string& msg) {
  throw std::runtime_error(path + ":" + std::to_string(line_no) + ": " + msg);
}

// Reads a rectangular CSV of numbers. Calls on_value(value, line_no) for each
// field in row-major order. Returns {rows, cols}.
std::pair<int, int> read_csv(
    const std::string& path,
    const std::function<void(double, int)>& on_value) {
  std::ifstream file(path);
  if (!file) throw std::runtime_error("cannot open file: " + path);

  int rows = 0;
  int cols = 0;
  std::string line;
  int line_no = 0;
  while (std::getline(file, line)) {
    ++line_no;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.find_first_not_of(" \t") == std::string::npos) continue;

    std::istringstream row_in(line);
    std::string field;
    int cols_in_row = 0;
    while (std::getline(row_in, field, ',')) {
      std::istringstream field_in(field);
      double v = 0.0;
      std::string extra;
      if (!(field_in >> v) || (field_in >> extra) || !std::isfinite(v)) {
        fail_at(path, line_no, "bad value '" + field + "'");
      }
      on_value(v, line_no);
      ++cols_in_row;
    }
    if (rows == 0) {
      cols = cols_in_row;
    } else if (cols_in_row != cols) {
      fail_at(path, line_no, "expected " + std::to_string(cols) +
                                 " columns, got " + std::to_string(cols_in_row));
    }
    ++rows;
  }
  if (rows == 0 || cols == 0) throw std::runtime_error("file is empty: " + path);
  return {rows, cols};
}

// Fills one per-cell layer from a constant or a CSV file of the grid's shape.
// check(value) returns an error message, or "" if the value is valid.
std::vector<double> load_layer(
    const std::string& name, double constant, const std::string& file,
    const Grid& grid, const std::function<std::string(double)>& check) {
  if (file.empty()) return std::vector<double>(grid.size(), constant);

  std::vector<double> values;
  values.reserve(grid.size());
  const auto [rows, cols] = read_csv(file, [&](double v, int line_no) {
    const std::string err = check(v);
    if (!err.empty()) fail_at(file, line_no, name + " " + err);
    values.push_back(v);
  });
  if (rows != grid.rows || cols != grid.cols) {
    throw std::runtime_error(
        file + ": " + name + " grid is " + std::to_string(rows) + " x " +
        std::to_string(cols) + " but terrain is " + std::to_string(grid.rows) +
        " x " + std::to_string(grid.cols));
  }
  return values;
}

void write_layer_source(std::ostream& out, const char* key, double constant,
                        const std::string& file, bool last) {
  out << "    \"" << key << "\": ";
  if (file.empty()) {
    out << "{ \"constant\": " << constant << " }";
  } else {
    out << "{ \"file\": \"" << json_escape(file) << "\" }";
  }
  out << (last ? "\n" : ",\n");
}

}  // namespace

Grid load_grid(const SimConfig& cfg) {
  Grid grid;
  grid.cell_size_m = cfg.cell_size_m;

  const auto [rows, cols] =
      read_csv(cfg.terrain_file, [&](double v, int line_no) {
        const int id = static_cast<int>(v);
        const auto terrain = terrain_from_id(id);
        if (static_cast<double>(id) != v || !terrain) {
          std::ostringstream shown;
          shown << v;
          fail_at(cfg.terrain_file, line_no,
                  "unknown terrain id " + shown.str() +
                      " (expected an integer 0-6)");
        }
        grid.terrain.push_back(*terrain);
      });
  grid.rows = rows;
  grid.cols = cols;

  auto any_value = [](double) { return std::string(); };
  grid.elevation_m = load_layer("elevation", cfg.elevation_m,
                                cfg.elevation_file, grid, any_value);
  grid.moisture = load_layer(
      "moisture", cfg.moisture, cfg.moisture_file, grid, [](double v) {
        return (v < 0.0 || v > 1.0) ? std::string("must be in [0, 1]")
                                    : std::string();
      });
  grid.wind_speed_m_s = load_layer(
      "wind speed", cfg.wind_speed_m_s, cfg.wind_speed_file, grid,
      [](double v) {
        return v < 0.0 ? std::string("must be >= 0") : std::string();
      });
  grid.wind_direction_deg =
      load_layer("wind direction", cfg.wind_direction_deg,
                 cfg.wind_direction_file, grid, any_value);
  return grid;
}

void write_grid_csv(const std::string& path, int rows, int cols,
                    const std::vector<double>& values) {
  auto out = open_out(path);
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      if (c > 0) out << ',';
      out << values[static_cast<std::size_t>(r) * cols + c];
    }
    out << '\n';
  }
}

void write_stats_csv(const std::string& path,
                     const std::vector<StepStats>& stats) {
  auto out = open_out(path);
  out << "time_s,burning,burned_out,new_ignitions,pending\n";
  for (const StepStats& s : stats) {
    out << s.time_s << ',' << s.burning << ',' << s.burned_out << ','
        << s.new_ignitions << ',' << s.pending << '\n';
  }
}

void write_metadata_json(const std::string& path, const Grid& grid,
                         const SimConfig& cfg, const SimResult& result,
                         const std::string& config_path,
                         double wall_time_s) {
  auto out = open_out(path);
  out << "{\n";
  out << "  \"grid\": {\n";
  out << "    \"rows\": " << grid.rows << ",\n";
  out << "    \"cols\": " << grid.cols << ",\n";
  out << "    \"cell_size_m\": " << grid.cell_size_m << ",\n";
  out << "    \"order\": \"row-major\",\n";
  out << "    \"row0\": \"north\",\n";
  out << "    \"col0\": \"west\"\n";
  out << "  },\n";
  out << "  \"files\": {\n";
  out << "    \"ignition_time\": { \"path\": \"ignition_time.csv\", "
         "\"units\": \"s\", \"never_ignited\": -1 },\n";
  out << "    \"burn_duration\": { \"path\": \"burn_duration.csv\", "
         "\"units\": \"s\", \"never_ignited\": -1 },\n";
  out << "    \"stats\": { \"path\": \"stats.csv\" }\n";
  out << "  },\n";
  out << "  \"timing\": {\n";
  out << "    \"timestep_s\": " << cfg.timestep_s << ",\n";
  out << "    \"max_duration_s\": " << cfg.max_duration_s << ",\n";
  out << "    \"end_time_s\": " << result.end_time_s << ",\n";
  out << "    \"steps\": " << result.steps << ",\n";
  out << "    \"hit_max_duration\": "
      << (result.hit_max_duration ? "true" : "false") << ",\n";
  out << "    \"wall_time_s\": " << wall_time_s << "\n";
  out << "  },\n";
  out << "  \"layers\": {\n";
  out << "    \"terrain\": { \"file\": \"" << json_escape(cfg.terrain_file)
      << "\" },\n";
  write_layer_source(out, "elevation_m", cfg.elevation_m, cfg.elevation_file, false);
  write_layer_source(out, "moisture", cfg.moisture, cfg.moisture_file, false);
  write_layer_source(out, "wind_speed_m_s", cfg.wind_speed_m_s,
                     cfg.wind_speed_file, false);
  write_layer_source(out, "wind_direction_deg", cfg.wind_direction_deg,
                     cfg.wind_direction_file, true);
  out << "  },\n";
  out << "  \"settings\": {\n";
  out << "    \"config_file\": \"" << json_escape(config_path) << "\",\n";
  out << "    \"seed\": " << cfg.seed << ",\n";
  out << "    \"burn_duration_s\": " << cfg.burn_duration_s << ",\n";
  out << "    \"wind_direction_convention\": "
         "\"blowing toward, degrees clockwise from north\",\n";
  out << "    \"ignitions\": [";
  for (std::size_t i = 0; i < cfg.ignitions.size(); ++i) {
    const Ignition& ig = cfg.ignitions[i];
    out << (i ? ", " : "") << "{ \"row\": " << ig.row << ", \"col\": "
        << ig.col << ", \"time_s\": " << ig.time_s << " }";
  }
  out << "]\n";
  out << "  },\n";
  out << "  \"summary\": {\n";
  out << "    \"cells_ignited\": " << result.cells_ignited << ",\n";
  out << "    \"spread_attempts\": " << result.spread_attempts << ",\n";
  out << "    \"successful_spreads\": " << result.successful_spreads << "\n";
  out << "  }\n";
  out << "}\n";
}

}  // namespace wildfire
