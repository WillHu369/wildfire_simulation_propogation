// Serial wildfire solver (simplified PROPAGATOR, Trucchia et al. 2020).
//
// Usage: wildfire_solver <scenario_dir> <output_dir>
//   scenario_dir: folder with settings.json and the per-cell CSV layers
//                 (e.g. inputs/examples/size_032), or that settings.json
//   output_dir:   where ignition_time_s.csv and metadata.json are written
//                 (e.g. outputs/examples/size_032)

#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>

#include "config.hpp"
#include "io.hpp"
#include "simulation.hpp"

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: " << argv[0] << " <scenario_dir> <output_dir>\n"
              << "  e.g. " << argv[0]
              << " inputs/examples/size_032 outputs/examples/size_032\n";
    return 2;
  }
  const std::filesystem::path out_dir = argv[2];

  try {
    const wildfire::SimConfig cfg = wildfire::load_settings(argv[1]);
    for (const std::string& note : cfg.notes) {
      std::cout << "note: " << note << "\n";
    }
    const wildfire::Grid grid = wildfire::load_grid(cfg);

    const auto start = std::chrono::steady_clock::now();
    const wildfire::SimResult result = wildfire::run_simulation(grid, cfg);
    const auto stop = std::chrono::steady_clock::now();
    const double wall_s = std::chrono::duration<double>(stop - start).count();

    if (wildfire::timestep_too_coarse(cfg.timestep_s, result)) {
      std::cerr << "warning: timestep_s = " << cfg.timestep_s
                << " s is too coarse for this run. The fastest cell crossing "
                   "took "
                << result.min_travel_time_s
                << " s, and arrival times are rounded to whole steps, so the "
                   "fire will spread too slowly. Use timestep_s <= "
                << 0.5 * result.min_travel_time_s << ".\n";
    }

    std::filesystem::create_directories(out_dir);
    wildfire::write_grid_csv((out_dir / "ignition_time_s.csv").string(),
                             grid.rows, grid.cols, result.ignition_time_s);
    wildfire::write_metadata_json((out_dir / "metadata.json").string(), grid,
                                  cfg, result, wall_s);

    std::cout << cfg.scenario_name << ": grid " << grid.rows << " x "
              << grid.cols << ", " << result.cells_ignited
              << " cells ignited, simulated " << result.end_time_s << " s in "
              << result.steps << " steps"
              << (result.hit_max_duration ? " (hit max duration)" : "")
              << ", compute " << wall_s << " s\n"
              << "outputs written to " << out_dir.string() << "\n";
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
