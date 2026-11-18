// Serial wildfire solver (simplified PROPAGATOR, Trucchia et al. 2020).
//
// Usage: wildfire_solver <config_file> <output_dir>

#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>

#include "config.hpp"
#include "io.hpp"
#include "simulation.hpp"

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: " << argv[0] << " <config_file> <output_dir>\n";
    return 2;
  }
  const std::string config_path = argv[1];
  const std::filesystem::path out_dir = argv[2];

  try {
    const wildfire::SimConfig cfg = wildfire::load_config(config_path);
    for (const std::string& d : cfg.defaults_used) {
      std::cout << "note: not set in config, using default " << d << "\n";
    }
    const wildfire::Grid grid = wildfire::load_grid(cfg);

    const auto start = std::chrono::steady_clock::now();
    const wildfire::SimResult result = wildfire::run_simulation(grid, cfg);
    const auto stop = std::chrono::steady_clock::now();
    const double wall_s = std::chrono::duration<double>(stop - start).count();

    std::filesystem::create_directories(out_dir);
    wildfire::write_grid_csv((out_dir / "ignition_time.csv").string(),
                             grid.rows, grid.cols, result.ignition_time_s);
    wildfire::write_grid_csv((out_dir / "burn_duration.csv").string(),
                             grid.rows, grid.cols, result.burn_duration_s);
    wildfire::write_stats_csv((out_dir / "stats.csv").string(), result.stats);
    wildfire::write_metadata_json((out_dir / "metadata.json").string(), grid,
                                  cfg, result, config_path, wall_s);

    std::cout << "grid " << grid.rows << " x " << grid.cols << ", "
              << result.cells_ignited << " cells ignited, simulated "
              << result.end_time_s << " s in " << result.steps << " steps"
              << (result.hit_max_duration ? " (hit max duration)" : "")
              << ", compute " << wall_s << " s\n"
              << "outputs written to " << out_dir.string() << "\n";
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
