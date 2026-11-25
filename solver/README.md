# Wildfire Solver (serial draft)

This is a C++17 serial implementation of a simplified PROPAGATOR model (Trucchia et al. 2020). It reads a scenario folder (a `settings.json` plus per-cell CSV layers), simulates fire spread, and writes the ignition grid described in [`docs/INPUTS_OUTPUTS.md`](../docs/INPUTS_OUTPUTS.md).

## Build and test

```sh
cd solver
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Any CMake generator works. `-G Ninja` is just what was used during development. On Windows with Strawberry Perl's MinGW, put `C:\Strawberry\c\bin` on `PATH` before running the executables, or they exit with code 127 (missing runtime DLLs).

## Run

The solver takes two arguments: a scenario folder (or the `settings.json` inside it) and an output folder. Outputs mirror the input layout, so `inputs/examples/<name>` writes to `outputs/examples/<name>`:

```sh
solver/build/wildfire_solver inputs/examples/size_032 outputs/examples/size_032
```

The solver writes two files to the output folder:

- `ignition_time_s.csv`: seconds since the start for each cell, or `-1` if the cell never ignited. Three decimals, same layout as the inputs.
- `metadata.json`: model name, grid size, end time, compute wall time, summary counts, and the settings used.

How long a cell is *shown* burning is up to the visualizer; the solver does not output it.

## Scenario folder

| File | Contents | Units | Required |
|---|---|---|---|
| `settings.json` | Run settings (below) | | **yes** |
| `terrain_type.csv` | Terrain ID per cell (0–6, see below) | | **yes** |
| `elevation_m.csv` | Elevation per cell | m | no (flat) |
| `fuel_moisture.csv` | Fuel moisture per cell | fraction 0–1 | no (0, dry) |
| `wind_speed_kmh.csv` | Wind speed per cell | km/h | no (0) |
| `wind_from_deg.csv` | Direction the wind blows **from**, clockwise from north | degrees | no (0) |
| `water_mask.csv` | Visualizer only; ignored by the solver (water is terrain 6) | | no |

All CSVs have the same rows and columns as `terrain_type.csv`. Row 0 is the north edge and column 0 is the west edge. Internally the solver converts wind to m/s and to the direction it blows *toward*.

### settings.json

```json
{
  "ignition_points": [[118, 32]],
  "timestep_s": 60.0,
  "max_duration_s": 21600.0,
  "seed": 0,
  "burn_duration_s": 600.0,
  "cell_size_m": 30.0
}
```

| Key | Meaning | Default |
|---|---|---|
| `ignition_points` | List of `[row, col]` cells that ignite at `t = 0` | **required** (at least one) |
| `timestep_s` | Timestep, s | 1 |
| `max_duration_s` | Hard stop, s | 3600 |
| `seed` | Random seed (non-negative integer) | 67 |
| `cell_size_m` | Cell width, m | 20 |
| `burn_duration_s` | Visualizer only; ignored by the solver | |

**When something is missing:** an optional setting or layer file falls back to its default, and the solver prints a `note:` line for each one. An unknown key (for example, a typo) also gets a `note:` but doesn't stop the run.

**Errors.** The run stops with exit code 1 and a message naming the file (and line, where there is one) before any simulation starts, when:

- `settings.json` is missing, isn't valid JSON, or isn't a JSON object;
- `ignition_points` is missing, empty, or not a list of `[row, col]` non-negative integer pairs;
- a setting has the wrong type or is out of range (`timestep_s` and `cell_size_m` must be > 0, `max_duration_s` ≥ 0);
- `terrain_type.csv` is missing or contains an ID that isn't 0–6;
- a layer file has the wrong size, ragged rows, non-numbers, or out-of-range values (moisture outside 0–1, negative wind speed);
- an ignition point is outside the grid or on non-burnable terrain.

**Terrain IDs:**

| ID | Terrain |
|---:|---|
| 0 | Broadleaves |
| 1 | Shrubs |
| 2 | Grassland |
| 3 | Fire-prone conifers |
| 4 | Agro-forestry |
| 5 | Not fire-prone forest |
| 6 | Non-vegetated: roads, water, bare ground (never burns) |

`inputs_test/` holds the old `config.txt` starter scenario. The solver no longer reads that format.

## Code layout

Each function in the headers has a short In / Out / Why comment.

| File | Contents |
|---|---|
| `terrain.*` | Terrain enum, PPR Table 1 (`p_n`, `v_n`), stored as printed in the paper |
| `probability.*` | PPR Eq. 1 and the spread decision (`r < p`) |
| `rng.hpp` | Deterministic draw keyed by (seed, source cell, target cell). It does not depend on processing order or MPI rank. |
| `travel_time.*` | PPR Eq. 3–6 |
| `grid.hpp` | Per-cell layers: terrain, elevation, moisture, wind speed and direction |
| `simulation.*` | Fixed-timestep loop: activate, spread once, stop when nothing is pending |
| `json.*` | Small dependency-free JSON parser for `settings.json` |
| `config.*` | Reads `settings.json` and locates the layer files |
| `io.*` | Reads the CSV layers (with unit conversion), writes the outputs |
| `main.cpp` | Command-line entry point |

## Draft limitations

- **Spread-probability modifiers are neutral.** Wind, slope and moisture currently leave the spread probability unchanged (`alpha_w = alpha_h = e_m = 1`).
- **Wind and slope do change travel time.** They act through Eq. 5–6, using the burning (source) cell's wind.
- **Arrival times are rounded** to the nearest timestep, with a minimum of 1 step.
