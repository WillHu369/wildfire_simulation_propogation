// Unit tests for the serial solver. No external framework: run the executable
// (or `ctest`) and it exits non-zero if any check fails.

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "config.hpp"
#include "io.hpp"
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

// Writes text to a file in a scratch folder and returns its path.
std::string write_temp(const std::string& name, const std::string& text) {
  const auto dir = std::filesystem::temp_directory_path() / "wildfire_tests";
  std::filesystem::create_directories(dir);
  const auto path = dir / name;
  std::ofstream(path) << text;
  return path.string();
}

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
  cfg.burn_duration_s = 60.0;
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
  CHECK(res.stats.back().burning == 0);
  CHECK(res.stats.back().pending == 0);

  int ignited = 0;
  bool durations_ok = true;
  bool times_ok = true;
  for (int i = 0; i < grid.size(); ++i) {
    const double t = res.ignition_time_s[i];
    if (t == wildfire::kNeverIgnited) {
      durations_ok = durations_ok && res.burn_duration_s[i] == wildfire::kNeverIgnited;
      continue;
    }
    ++ignited;
    durations_ok = durations_ok && res.burn_duration_s[i] == cfg.burn_duration_s;
    // Grassland needs at least 10 s per cardinal cell, so no cell other than
    // the ignition can ignite before t = 10.
    if (i != grid.index(10, 10)) times_ok = times_ok && t >= 10.0;
  }
  CHECK(ignited == res.cells_ignited);
  CHECK(durations_ok);
  CHECK(times_ok);
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

// --- Config parsing and input validation ------------------------------------

void test_config_defaults_and_errors() {
  using std::runtime_error;
  write_temp("t.csv", "2,2,2\n2,6,2\n2,2,2\n");

  const auto ok = wildfire::load_config(
      write_temp("ok.txt", "terrain_file = t.csv\nignition = 0 0\n"));
  CHECK(ok.seed == 67);
  CHECK(ok.burn_duration_s == 60.0);
  CHECK(ok.ignitions.size() == 1 && ok.ignitions[0].time_s == 0.0);
  bool seed_noted = false;
  for (const auto& d : ok.defaults_used) seed_noted = seed_noted || d == "seed = 67";
  CHECK(seed_noted);

  CHECK(throws<runtime_error>([] {
    wildfire::load_config(write_temp("c1.txt", "ignition = 0 0\n"));  // no terrain
  }));
  CHECK(throws<runtime_error>([] {
    wildfire::load_config(write_temp("c2.txt", "terrain_file = t.csv\n"));  // no ignition
  }));
  CHECK(throws<runtime_error>([] {
    wildfire::load_config(write_temp(
        "c3.txt", "terrain_file = t.csv\nignition = 0 0\nsed = 4\n"));  // typo
  }));
  CHECK(throws<runtime_error>([] {
    wildfire::load_config(write_temp(
        "c4.txt", "terrain_file = t.csv\nignition = 0 0\nseed = 1\nseed = 2\n"));
  }));
  CHECK(throws<runtime_error>([] {
    wildfire::load_config(write_temp(
        "c5.txt", "terrain_file = t.csv\nignition = 0 0\ntimestep_s = fast\n"));
  }));
  CHECK(throws<runtime_error>([] {
    wildfire::load_config(write_temp(
        "c6.txt", "terrain_file = t.csv\nignition = 0 0\nseed =\n"));
  }));
  CHECK(throws<runtime_error>([] {
    wildfire::load_config(write_temp(
        "c7.txt", "terrain_file = t.csv\nignition = 0 0\n"
                  "wind_speed_m_s = 3\nwind_speed_file = w.csv\n"));
  }));
  CHECK(throws<runtime_error>([] {
    wildfire::load_config(write_temp(
        "c8.txt", "terrain_file = t.csv\nignition = 0 0 5 9\n"));  // extra value
  }));

  // Ignition on the non-vegetated center cell is rejected before simulating.
  const auto bad_ig = wildfire::load_config(
      write_temp("c9.txt", "terrain_file = t.csv\nignition = 1 1\n"));
  const auto grid = wildfire::load_grid(bad_ig);
  CHECK(throws_invalid([&] { wildfire::run_simulation(grid, bad_ig); }));
}

void test_layer_files() {
  using std::runtime_error;
  write_temp("t.csv", "2,2,2\n2,6,2\n2,2,2\n");
  write_temp("ws.csv", "1,2,3\n4,5,6\n7,8,9\n");
  write_temp("wd.csv", "90,90,90\n90,90,90\n90,90,90\n");
  const auto cfg = wildfire::load_config(write_temp(
      "l1.txt", "terrain_file = t.csv\nignition = 0 0\n"
                "wind_speed_file = ws.csv\nwind_direction_file = wd.csv\n"));
  const auto grid = wildfire::load_grid(cfg);
  CHECK(grid.rows == 3 && grid.cols == 3);
  CHECK(grid.wind_speed_m_s[grid.index(1, 2)] == 6.0);
  CHECK(grid.wind_direction_deg[grid.index(2, 0)] == 90.0);
  CHECK(grid.moisture.size() == 9 && grid.moisture[4] == 0.0);  // default

  write_temp("ws_small.csv", "1,2\n3,4\n");
  write_temp("ws_neg.csv", "1,2,3\n4,-5,6\n7,8,9\n");
  write_temp("t_bad.csv", "2,2,2\n2,9,2\n2,2,2\n");
  CHECK(throws<runtime_error>([] {
    wildfire::load_grid(wildfire::load_config(write_temp(
        "l2.txt", "terrain_file = t.csv\nignition = 0 0\nwind_speed_file = ws_small.csv\n")));
  }));
  CHECK(throws<runtime_error>([] {
    wildfire::load_grid(wildfire::load_config(write_temp(
        "l3.txt", "terrain_file = t.csv\nignition = 0 0\nwind_speed_file = ws_neg.csv\n")));
  }));
  CHECK(throws<runtime_error>([] {
    wildfire::load_grid(wildfire::load_config(write_temp(
        "l4.txt", "terrain_file = t_bad.csv\nignition = 0 0\n")));
  }));
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
  test_max_duration();
  test_invalid_ignition();
  test_per_cell_wind();
  test_config_defaults_and_errors();
  test_layer_files();

  std::cout << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
  return g_failures == 0 ? 0 : 1;
}
