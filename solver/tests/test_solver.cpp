// Unit tests for the serial solver. No external framework: run the executable
// (or `ctest`) and it exits non-zero if any check fails.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "io.hpp"
#include "json.hpp"
#include "probability.hpp"
#include "rng.hpp"
#include "simulation.hpp"
#include "terrain.hpp"
#include "travel_time.hpp"

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* expr, const char* file, int line) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::cerr << file << ":" << line << ": CHECK failed: " << expr << "\n";
  }
}

#define CHECK(cond) check((cond), #cond, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, tol) \
  check(std::fabs((a) - (b)) <= (tol), #a " ~= " #b, __FILE__, __LINE__)

template <typename Fn>
bool throws_invalid(Fn fn) {
  try {
    fn();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

using wildfire::Terrain;

wildfire::Grid uniform_grid(int rows, int cols, Terrain t) {
  wildfire::Grid g;
  g.rows = rows;
  g.cols = cols;
  g.cell_size_m = 20.0;
  g.terrain.assign(rows * cols, t);
  g.elevation_m.assign(rows * cols, 0.0);
  g.moisture.assign(rows * cols, 0.0);
  g.wind_speed_m_s.assign(rows * cols, 0.0);
  g.wind_direction_deg.assign(rows * cols, 0.0);
  return g;
}

namespace fs = std::filesystem;
using Files = std::vector<std::pair<std::string, std::string>>;

// Creates a fresh scenario folder in the temp directory holding the given
// (file name, contents) pairs, and returns the folder's path.
std::string make_scenario(const std::string& name, const Files& files) {
  const auto dir = fs::temp_directory_path() / "wildfire_tests" / name;
  fs::remove_all(dir);
  fs::create_directories(dir);
  for (const auto& [file, text] : files) std::ofstream(dir / file) << text;
  return dir.string();
}

std::string read_text(const std::string& path) {
  std::ifstream in(path);
  std::ostringstream s;
  s << in.rdbuf();
  return s.str();
}

bool any_note_contains(const wildfire::SimConfig& cfg, const std::string& text) {
  for (const auto& note : cfg.notes) {
    if (note.find(text) != std::string::npos) return true;
  }
  return false;
}

// 3 x 3 grassland with a non-burnable center.
const char* kTerrain3x3 = "2,2,2\n2,6,2\n2,2,2\n";
const char* kSettingsOk = R"({"ignition_points": [[0, 0]]})";

template <typename E, typename Fn>
bool throws(Fn fn) {
  try {
    fn();
  } catch (const E&) {
    return true;
  }
  return false;
}

// Mean ignition time of ignited cells west vs east of a column.
std::pair<double, double> mean_time_west_east(const wildfire::Grid& g,
                                              const wildfire::SimResult& res,
                                              int center_col) {
  double west = 0.0, east = 0.0;
  int nw = 0, ne = 0;
  for (int r = 0; r < g.rows; ++r) {
    for (int c = 0; c < g.cols; ++c) {
      const double t = res.ignition_time_s[g.index(r, c)];
      if (t < 0.0) continue;
      if (c < center_col) { west += t; ++nw; }
      if (c > center_col) { east += t; ++ne; }
    }
  }
  return {nw ? west / nw : 0.0, ne ? east / ne : 0.0};
}

wildfire::SimConfig base_config(int row, int col) {
  wildfire::SimConfig cfg;
  cfg.ignitions.push_back({row, col, 0.0});
  cfg.seed = 42;
  cfg.timestep_s = 1.0;
  cfg.max_duration_s = 100000.0;
  return cfg;
}

// --- Table 1 ---------------------------------------------------------------

void test_table_direction() {
  // Source -> target; the table is not symmetric.
  CHECK(wildfire::nominal_probability(Terrain::Grassland, Terrain::Shrubs) == 0.350);
  CHECK(wildfire::nominal_probability(Terrain::Shrubs, Terrain::Grassland) == 0.475);
  CHECK(wildfire::nominal_probability(Terrain::Grassland, Terrain::Grassland) == 0.475);
  CHECK(wildfire::nominal_probability(Terrain::FireProneConifers,
                                      Terrain::AgroForestry) == 0.475);
  CHECK(wildfire::nominal_probability(Terrain::AgroForestry,
                                      Terrain::FireProneConifers) == 0.200);
  CHECK(wildfire::nominal_speed_m_per_min(Terrain::Grassland) == 120.0);
  CHECK(wildfire::nominal_speed_m_per_min(Terrain::FireProneConifers) == 200.0);
}

void test_non_vegetated() {
  CHECK(!wildfire::is_burnable(Terrain::NonVegetated));
  CHECK(wildfire::nominal_probability(Terrain::Grassland, Terrain::NonVegetated) == 0.0);
  CHECK(wildfire::nominal_probability(Terrain::NonVegetated, Terrain::Grassland) == 0.0);
  CHECK(wildfire::spread_probability(Terrain::Grassland, Terrain::NonVegetated) == 0.0);
  CHECK(wildfire::nominal_speed_m_per_min(Terrain::NonVegetated) == 0.0);
  CHECK(!wildfire::terrain_from_id(7).has_value());
  CHECK(!wildfire::terrain_from_id(-1).has_value());
  CHECK(wildfire::terrain_from_id(6) == Terrain::NonVegetated);
}

// --- Eq. 1 and the spread decision ------------------------------------------

void test_equation_1() {
  CHECK_NEAR(wildfire::spread_probability(0.35), 0.35, 1e-12);  // neutral
  wildfire::SpreadModifiers wind;
  wind.alpha_w = 2.0;
  CHECK_NEAR(wildfire::spread_probability(0.35, wind), 0.5775, 1e-12);
  wind.e_m = 0.8;
  CHECK_NEAR(wildfire::spread_probability(0.35, wind), 0.462, 1e-12);
  wildfire::SpreadModifiers zero_exp;
  zero_exp.alpha_h = 0.0;
  CHECK_NEAR(wildfire::spread_probability(0.35, zero_exp), 0.0, 1e-12);
}

void test_spread_decision() {
  CHECK(wildfire::spread_succeeds(0.35, 0.20));
  CHECK(!wildfire::spread_succeeds(0.35, 0.50));
  CHECK(!wildfire::spread_succeeds(0.35, 0.35));  // strict <
  CHECK(!wildfire::spread_succeeds(0.0, 0.0));
}

void test_invalid_inputs() {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  wildfire::SpreadModifiers bad;
  CHECK(throws_invalid([] { wildfire::spread_probability(1.5); }));
  CHECK(throws_invalid([] { wildfire::spread_probability(-0.1); }));
  CHECK(throws_invalid([&] { wildfire::spread_probability(nan); }));
  bad.alpha_w = -1.0;
  CHECK(throws_invalid([&] { wildfire::spread_probability(0.3, bad); }));
  bad = {};
  bad.e_m = 1.2;
  CHECK(throws_invalid([&] { wildfire::spread_probability(0.3, bad); }));
  bad = {};
  bad.alpha_h = nan;
  CHECK(throws_invalid([&] { wildfire::spread_probability(0.3, bad); }));
}

// --- Travel time (Eq. 3-6) --------------------------------------------------

void test_travel_time() {
  CHECK(wildfire::wind_speed_factor(0.0) == 1.0);
  CHECK(wildfire::slope_speed_factor(0.0) == 1.0);
  CHECK(wildfire::moisture_speed_factor(0.0) == 1.0);
  // Downhill is the reciprocal of the same uphill slope.
  CHECK_NEAR(wildfire::slope_speed_factor(-0.3) * wildfire::slope_speed_factor(0.3),
             1.0, 1e-12);
  CHECK(wildfire::slope_speed_factor(0.3) > 1.0);
  // Grassland, 20 m cardinal step, neutral: 20 m / 120 m/min = 10 s.
  CHECK_NEAR(wildfire::travel_time_seconds(20.0, 120.0, 1, 1, 1), 10.0, 1e-12);
  CHECK_NEAR(wildfire::travel_time_seconds(20.0 * std::sqrt(2.0), 120.0, 1, 1, 1),
             10.0 * std::sqrt(2.0), 1e-12);
  CHECK(throws_invalid([] { wildfire::travel_time_seconds(20.0, 0.0, 1, 1, 1); }));
  // Wind convention: blowing toward east (90) along an eastward bearing.
  CHECK_NEAR(wildfire::wind_component_along(5.0, 90.0, 90.0), 5.0, 1e-12);
  CHECK_NEAR(wildfire::wind_component_along(5.0, 90.0, 270.0), -5.0, 1e-12);
  CHECK_NEAR(wildfire::wind_component_along(5.0, 0.0, 90.0), 0.0, 1e-12);
}

// --- Random draws -----------------------------------------------------------

void test_rng() {
  CHECK(wildfire::spread_draw(1, 10, 11) == wildfire::spread_draw(1, 10, 11));
  CHECK(wildfire::spread_draw(1, 10, 11) != wildfire::spread_draw(2, 10, 11));
  CHECK(wildfire::spread_draw(1, 10, 11) != wildfire::spread_draw(1, 11, 10));
  double sum = 0.0;
  bool in_range = true;
  const int n = 200000;
  for (int i = 0; i < n; ++i) {
    const double r = wildfire::spread_draw(7, static_cast<std::uint64_t>(i),
                                           static_cast<std::uint64_t>(i + 1));
    in_range = in_range && r >= 0.0 && r < 1.0;
    sum += r;
  }
  CHECK(in_range);
  CHECK_NEAR(sum / n, 0.5, 0.01);
}

// --- Whole simulation -------------------------------------------------------

void test_simulation_basics() {
  const auto grid = uniform_grid(21, 21, Terrain::Grassland);
  const auto cfg = base_config(10, 10);
  const auto res = wildfire::run_simulation(grid, cfg);

  CHECK(res.ignition_time_s.size() == static_cast<std::size_t>(grid.size()));
  CHECK(res.ignition_time_s[grid.index(10, 10)] == 0.0);
  CHECK(res.cells_ignited > 1);
  CHECK(!res.hit_max_duration);

  int ignited = 0;
  double last_ignition = 0.0;
  bool times_ok = true;
  for (int i = 0; i < grid.size(); ++i) {
    const double t = res.ignition_time_s[i];
    if (t == wildfire::kNeverIgnited) continue;
    ++ignited;
    last_ignition = std::max(last_ignition, t);
    // Grassland needs at least 10 s per cardinal cell, so no cell other than
    // the ignition can ignite before t = 10.
    if (i != grid.index(10, 10)) times_ok = times_ok && t >= 10.0;
  }
  CHECK(ignited == res.cells_ignited);
  CHECK(times_ok);
  // The run stops as soon as nothing is pending: at the last ignition.
  CHECK(res.end_time_s == last_ignition);
}

void test_reproducible() {
  const auto grid = uniform_grid(30, 30, Terrain::Shrubs);
  auto cfg = base_config(15, 15);
  const auto a = wildfire::run_simulation(grid, cfg);
  const auto b = wildfire::run_simulation(grid, cfg);
  CHECK(a.ignition_time_s == b.ignition_time_s);
  cfg.seed = 43;
  const auto c = wildfire::run_simulation(grid, cfg);
  CHECK(a.ignition_time_s != c.ignition_time_s);
}

void test_firebreak() {
  // A full column of non-vegetated cells must stop the fire.
  auto grid = uniform_grid(15, 15, Terrain::Grassland);
  for (int r = 0; r < grid.rows; ++r) {
    grid.terrain[grid.index(r, 7)] = Terrain::NonVegetated;
  }
  const auto res = wildfire::run_simulation(grid, base_config(7, 2));
  bool crossed = false;
  for (int r = 0; r < grid.rows; ++r) {
    for (int c = 7; c < grid.cols; ++c) {
      crossed = crossed || res.ignition_time_s[grid.index(r, c)] != wildfire::kNeverIgnited;
    }
  }
  CHECK(!crossed);
}

void test_timestep_check() {
  // Grassland, 20 m cells, no wind/slope: fastest crossing is the cardinal
  // step, 20 m / 120 m/min = 10 s. Timesteps above 5 s are too coarse.
  const auto grid = uniform_grid(21, 21, Terrain::Grassland);
  auto cfg = base_config(10, 10);
  const auto fine = wildfire::run_simulation(grid, cfg);
  CHECK_NEAR(fine.min_travel_time_s, 10.0, 1e-9);
  CHECK(!wildfire::timestep_too_coarse(1.0, fine));
  CHECK(!wildfire::timestep_too_coarse(5.0, fine));
  CHECK(wildfire::timestep_too_coarse(5.1, fine));

  // The problem the check guards against: with 60 s steps every crossing is
  // rounded up to one step, so the same fire finishes much later.
  cfg.timestep_s = 60.0;
  const auto coarse = wildfire::run_simulation(grid, cfg);
  CHECK(wildfire::timestep_too_coarse(cfg.timestep_s, coarse));
  CHECK(coarse.end_time_s > 3.0 * fine.end_time_s);

  // No spread at all (a lone burnable cell): nothing to warn about.
  auto lone = uniform_grid(3, 3, Terrain::NonVegetated);
  lone.terrain[lone.index(1, 1)] = Terrain::Grassland;
  const auto none = wildfire::run_simulation(lone, base_config(1, 1));
  CHECK(none.min_travel_time_s == -1.0);
  CHECK(!wildfire::timestep_too_coarse(60.0, none));
}

void test_max_duration() {
  const auto grid = uniform_grid(21, 21, Terrain::Grassland);
  auto cfg = base_config(10, 10);
  cfg.max_duration_s = 0.0;
  const auto res = wildfire::run_simulation(grid, cfg);
  CHECK(res.cells_ignited == 1);
  CHECK(res.hit_max_duration);
  CHECK(res.steps == 1);
}

void test_invalid_ignition() {
  auto grid = uniform_grid(5, 5, Terrain::Grassland);
  grid.terrain[grid.index(2, 2)] = Terrain::NonVegetated;
  CHECK(throws_invalid([&] { wildfire::run_simulation(grid, base_config(2, 2)); }));
  CHECK(throws_invalid([&] { wildfire::run_simulation(grid, base_config(5, 0)); }));
  CHECK(throws_invalid([&] { wildfire::run_simulation(grid, base_config(-1, 0)); }));
}

// --- Per-cell wind ----------------------------------------------------------

void test_per_cell_wind() {
  // Wind blowing toward the east everywhere: the east side burns sooner.
  auto grid = uniform_grid(41, 41, Terrain::Grassland);
  grid.wind_speed_m_s.assign(grid.size(), 8.0);
  grid.wind_direction_deg.assign(grid.size(), 90.0);
  const auto east_wind = wildfire::run_simulation(grid, base_config(20, 20));
  const auto [w1, e1] = mean_time_west_east(grid, east_wind, 20);
  CHECK(e1 < w1);

  // Wind only in the WEST half, blowing west: now the west side is faster.
  // This proves the solver reads wind per cell, not one global value.
  auto local = uniform_grid(41, 41, Terrain::Grassland);
  for (int r = 0; r < local.rows; ++r) {
    for (int c = 0; c < 20; ++c) {
      local.wind_speed_m_s[local.index(r, c)] = 8.0;
      local.wind_direction_deg[local.index(r, c)] = 270.0;
    }
  }
  const auto west_wind = wildfire::run_simulation(local, base_config(20, 20));
  const auto [w2, e2] = mean_time_west_east(local, west_wind, 20);
  CHECK(w2 < e2);
}

// --- JSON parser ------------------------------------------------------------

void test_json() {
  using wildfire::JsonValue;
  const JsonValue v = wildfire::parse_json(
      R"({"a": [1, -2.5e1, true, null], "b": {"c": "x\né\"/"}, "d": 0})",
      "t.json");
  CHECK(v.is_object() && v.object.size() == 3);
  const JsonValue* a = v.find("a");
  CHECK(a && a->is_array() && a->array.size() == 4);
  CHECK(a && a->array[0].number == 1.0 && a->array[1].number == -25.0);
  CHECK(a && a->array[2].type == JsonValue::Type::Bool && a->array[2].boolean);
  CHECK(a && a->array[3].type == JsonValue::Type::Null);
  const JsonValue* c = v.find("b") ? v.find("b")->find("c") : nullptr;
  CHECK(c && c->string == "x\n\xC3\xA9\"/");  // é -> UTF-8 "é"
  CHECK(v.find("missing") == nullptr);
  CHECK(wildfire::parse_json("  [ ]  ", "t").array.empty());

  using std::runtime_error;
  for (const char* bad : {
           R"({"a": 1,})",          // trailing comma
           R"({"a": 1, "a": 2})",   // duplicate key
           R"({"a": 01})",          // leading zero
           R"({"a": tru})",         // bad literal
           R"({"a": 1} x)",         // trailing content
           R"({"a": "abc)",         // unterminated string
           R"({"a": 1.})",          // digit required after '.'
           R"({a: 1})",             // unquoted key
           "",                      // empty document
       }) {
    CHECK(throws<runtime_error>([&] { wildfire::parse_json(bad, "t.json"); }));
  }
  // Error messages point at the line.
  try {
    wildfire::parse_json("{\n  \"a\": 1,\n  \"b\": }", "t.json");
    CHECK(false);
  } catch (const runtime_error& e) {
    CHECK(std::string(e.what()).find("t.json:3:") == 0);
  }
}

// --- settings.json ----------------------------------------------------------

void test_settings_full() {
  const std::string dir = make_scenario("full", {
      {"settings.json", R"({
        "ignition_points": [[0, 0], [2, 1]],
        "timestep_s": 60.0,
        "max_duration_s": 21600.0,
        "seed": 3,
        "burn_duration_s": 600.0,
        "cell_size_m": 30.0
      })"},
      {"terrain_type.csv", kTerrain3x3},
      {"elevation_m.csv", "0,1,2\n3,4,5\n6,7,8\n"},
      {"fuel_moisture.csv", "0.08,0.08,0.08\n0.08,0.08,0.08\n0.08,0.08,0.08\n"},
      {"wind_speed_kmh.csv", "36,36,36\n36,36,36\n36,36,72\n"},
      {"wind_from_deg.csv", "270,270,270\n270,270,270\n270,270,0\n"},
      {"water_mask.csv", "0,0,0\n0,1,0\n0,0,0\n"},  // visualizer only: ignored
  });
  const auto cfg = wildfire::load_settings(dir);
  CHECK(cfg.ignitions.size() == 2);
  CHECK(cfg.ignitions[1].row == 2 && cfg.ignitions[1].col == 1);
  CHECK(cfg.ignitions[1].time_s == 0.0);
  CHECK(cfg.timestep_s == 60.0 && cfg.max_duration_s == 21600.0);
  CHECK(cfg.seed == 3 && cfg.cell_size_m == 30.0);
  CHECK(cfg.scenario_name == "full");
  CHECK(cfg.notes.empty());  // burn_duration_s is a known visualizer key

  // The settings.json path itself works the same as the folder.
  const auto same = wildfire::load_settings((fs::path(dir) / "settings.json").string());
  CHECK(same.seed == 3 && same.scenario_name == "full" &&
        same.terrain_file == cfg.terrain_file);

  // Layers, with wind converted to m/s and "blowing toward".
  const auto grid = wildfire::load_grid(cfg);
  CHECK(grid.rows == 3 && grid.cols == 3 && grid.cell_size_m == 30.0);
  CHECK(grid.terrain[grid.index(1, 1)] == Terrain::NonVegetated);
  CHECK(grid.elevation_m[grid.index(1, 2)] == 5.0);
  CHECK_NEAR(grid.moisture[0], 0.08, 1e-12);
  CHECK_NEAR(grid.wind_speed_m_s[0], 10.0, 1e-12);                  // 36 km/h
  CHECK_NEAR(grid.wind_speed_m_s[grid.index(2, 2)], 20.0, 1e-12);   // 72 km/h
  CHECK_NEAR(grid.wind_direction_deg[0], 90.0, 1e-12);   // from W -> toward E
  CHECK_NEAR(grid.wind_direction_deg[grid.index(2, 2)], 180.0, 1e-12);  // from N

  // An ignition on the non-burnable center is rejected before simulating.
  auto bad_ig = cfg;
  bad_ig.ignitions = {{1, 1, 0.0}};
  CHECK(throws_invalid([&] { wildfire::run_simulation(grid, bad_ig); }));
  bad_ig.ignitions = {{3, 0, 0.0}};  // outside the 3 x 3 grid
  CHECK(throws_invalid([&] { wildfire::run_simulation(grid, bad_ig); }));
}

void test_settings_defaults() {
  // Only the required pieces: ignition_points and terrain_type.csv.
  const std::string dir = make_scenario("minimal", {
      {"settings.json", kSettingsOk},
      {"terrain_type.csv", kTerrain3x3},
  });
  const auto cfg = wildfire::load_settings(dir);
  CHECK(cfg.seed == 67 && cfg.timestep_s == 1.0);
  CHECK(cfg.cell_size_m == 20.0 && cfg.max_duration_s == 3600.0);
  CHECK(any_note_contains(cfg, "seed = 67"));
  CHECK(any_note_contains(cfg, "timestep_s = 1"));
  CHECK(any_note_contains(cfg, "elevation_m.csv not found"));
  CHECK(any_note_contains(cfg, "wind_speed_kmh.csv not found"));
  CHECK(cfg.notes.size() == 8);  // 4 settings + 4 layer files

  const auto grid = wildfire::load_grid(cfg);
  bool neutral = true;
  for (int i = 0; i < grid.size(); ++i) {
    neutral = neutral && grid.elevation_m[i] == 0.0 && grid.moisture[i] == 0.0 &&
              grid.wind_speed_m_s[i] == 0.0;
  }
  CHECK(neutral);

  // Unknown keys (likely typos) are reported but do not stop the run.
  const auto typo = wildfire::load_settings(make_scenario("typo", {
      {"settings.json", R"({"ignition_points": [[0, 0]], "sed": 4})"},
      {"terrain_type.csv", kTerrain3x3},
  }));
  CHECK(any_note_contains(typo, "\"sed\""));
  CHECK(typo.seed == 67);
}

void test_settings_errors() {
  using std::runtime_error;
  // Each of these settings.json files must be rejected.
  const char* bad_settings[] = {
      R"({"seed": 1})",                                     // no ignition_points
      R"({"ignition_points": []})",                         // empty
      R"({"ignition_points": [0, 0]})",                     // not a list of pairs
      R"({"ignition_points": [[1]]})",                      // too few coords
      R"({"ignition_points": [[1, 2, 3]]})",                // too many coords
      R"({"ignition_points": [[1.5, 2]]})",                 // not an integer
      R"({"ignition_points": [[-1, 0]]})",                  // negative
      R"({"ignition_points": [["1", "2"]]})",               // strings
      R"({"ignition_points": [[0, 0]], "timestep_s": "fast"})",
      R"({"ignition_points": [[0, 0]], "timestep_s": 0})",
      R"({"ignition_points": [[0, 0]], "cell_size_m": -30})",
      R"({"ignition_points": [[0, 0]], "max_duration_s": -1})",
      R"({"ignition_points": [[0, 0]], "seed": -1})",
      R"({"ignition_points": [[0, 0]], "seed": 1.5})",
      R"([{"ignition_points": [[0, 0]]}])",                 // root not an object
      R"({"ignition_points": [[0, 0]],})",                  // malformed JSON
  };
  int i = 0;
  for (const char* text : bad_settings) {
    const std::string dir = make_scenario("bad" + std::to_string(i++), {
        {"settings.json", text},
        {"terrain_type.csv", kTerrain3x3},
    });
    CHECK(throws<runtime_error>([&] { wildfire::load_settings(dir); }));
  }

  // Missing settings.json, missing terrain_type.csv.
  const std::string empty = make_scenario("empty", {});
  CHECK(throws<runtime_error>([&] { wildfire::load_settings(empty); }));
  const std::string no_terrain = make_scenario("no_terrain", {
      {"settings.json", kSettingsOk},
  });
  CHECK(throws<runtime_error>([&] { wildfire::load_settings(no_terrain); }));
}

void test_layer_errors() {
  using std::runtime_error;
  // Each layer file must be rejected by load_grid.
  const Files bad_layers[] = {
      {{"wind_speed_kmh.csv", "1,2\n3,4\n"}},                 // wrong size
      {{"wind_speed_kmh.csv", "1,2,3\n4,-5,6\n7,8,9\n"}},     // negative
      {{"fuel_moisture.csv", "0,0,0\n0,1.5,0\n0,0,0\n"}},     // not a fraction
      {{"elevation_m.csv", "0,0,0\n0,0\n0,0,0\n"}},           // ragged row
      {{"elevation_m.csv", "0,0,0\n0,x,0\n0,0,0\n"}},         // not a number
      {{"terrain_type.csv", "2,2,2\n2,9,2\n2,2,2\n"}},        // unknown terrain id
  };
  int i = 0;
  for (const Files& layer : bad_layers) {
    Files files = {{"settings.json", kSettingsOk}};
    if (layer[0].first != "terrain_type.csv") files.push_back({"terrain_type.csv", kTerrain3x3});
    files.push_back(layer[0]);
    const std::string dir = make_scenario("bad_layer" + std::to_string(i++), files);
    CHECK(throws<runtime_error>([&] {
      wildfire::load_grid(wildfire::load_settings(dir));
    }));
  }
}

void test_wind_conversion() {
  CHECK_NEAR(wildfire::kmh_to_m_s(36.0), 10.0, 1e-12);
  CHECK_NEAR(wildfire::wind_from_to_toward_deg(0.0), 180.0, 1e-12);
  CHECK_NEAR(wildfire::wind_from_to_toward_deg(270.0), 90.0, 1e-12);
  CHECK_NEAR(wildfire::wind_from_to_toward_deg(350.0), 170.0, 1e-12);
  CHECK_NEAR(wildfire::wind_from_to_toward_deg(-90.0), 90.0, 1e-12);
  CHECK_NEAR(wildfire::wind_from_to_toward_deg(540.0), 0.0, 1e-12);

  // End to end: a westerly wind (FROM 270) pushes the fire EAST.
  // 41 x 41 grassland, 30 km/h wind from the west everywhere.
  std::string terrain, speed, from;
  for (int r = 0; r < 41; ++r) {
    for (int c = 0; c < 41; ++c) {
      const char* sep = c ? "," : "";
      terrain += std::string(sep) + "2";
      speed += std::string(sep) + "30";
      from += std::string(sep) + "270";
    }
    terrain += "\n";
    speed += "\n";
    from += "\n";
  }
  const std::string dir = make_scenario("westerly", {
      {"settings.json", R"({"ignition_points": [[20, 20]], "seed": 42,
                            "timestep_s": 1, "max_duration_s": 100000})"},
      {"terrain_type.csv", terrain},
      {"wind_speed_kmh.csv", speed},
      {"wind_from_deg.csv", from},
  });
  const auto cfg = wildfire::load_settings(dir);
  const auto grid = wildfire::load_grid(cfg);
  const auto res = wildfire::run_simulation(grid, cfg);
  const auto [west, east] = mean_time_west_east(grid, res, 20);
  CHECK(east < west);
}

// --- Outputs ----------------------------------------------------------------

void test_outputs() {
  const std::string dir = make_scenario("outputs", {});
  const std::string csv = (fs::path(dir) / "ignition_time_s.csv").string();
  wildfire::write_grid_csv(csv, 2, 2, {-1.0, 0.0, 60.0, 1234567.5});
  // Fixed 3 decimals, like the visualizer's files; never scientific notation.
  CHECK(read_text(csv) == "-1.000,0.000\n60.000,1234567.500\n");

  // metadata.json is valid JSON with the fields the visualizer reads.
  const auto grid = uniform_grid(5, 5, Terrain::Grassland);
  auto cfg = base_config(2, 2);
  cfg.scenario_name = "my \"quoted\" run";
  const auto res = wildfire::run_simulation(grid, cfg);
  const std::string meta = (fs::path(dir) / "metadata.json").string();
  wildfire::write_metadata_json(meta, grid, cfg, res, 0.25);
  const auto json = wildfire::parse_json(read_text(meta), meta);
  CHECK(json.find("model") && json.find("model")->string.find("PROPAGATOR") == 0);
  CHECK(json.find("scenario") && json.find("scenario")->string == cfg.scenario_name);
  CHECK(json.find("never_ignited") && json.find("never_ignited")->number == -1.0);
  CHECK(json.find("end_time_s") && json.find("end_time_s")->number == res.end_time_s);
  const auto* settings = json.find("settings");
  const auto* points = settings ? settings->find("ignition_points") : nullptr;
  CHECK(points && points->array.size() == 1 && points->array[0].array[0].number == 2.0);
  const auto* run = json.find("run");
  CHECK(run && run->find("wall_time_s") && run->find("wall_time_s")->number == 0.25);
}

}  // namespace

int main() {
  test_table_direction();
  test_non_vegetated();
  test_equation_1();
  test_spread_decision();
  test_invalid_inputs();
  test_travel_time();
  test_rng();
  test_simulation_basics();
  test_reproducible();
  test_firebreak();
  test_timestep_check();
  test_max_duration();
  test_invalid_ignition();
  test_per_cell_wind();
  test_json();
  test_settings_full();
  test_settings_defaults();
  test_settings_errors();
  test_layer_errors();
  test_wind_conversion();
  test_outputs();

  std::cout << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
  return g_failures == 0 ? 0 : 1;
}
