# Wildfire Solver (serial draft)

This is a C++17 serial implementation of a simplified PROPAGATOR model (Trucchia et al. 2020). It reads a terrain grid and settings, simulates fire spread, and writes the ignition grid described in [`docs/INPUTS_OUTPUTS.md`](../docs/INPUTS_OUTPUTS.md).

## Build and test

```sh
cd solver
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Any CMake generator works. `-G Ninja` is just what was used during development.

## Run

The solver takes two command-line arguments: the config file and an output folder. The config file names the terrain file and any other input files.

```sh
solver/build/wildfire_solver inputs_test/scenarios/demo/config.txt outputs/demo
```

The solver writes four files to the output folder:

- `ignition_time.csv`: seconds since the start for each cell, or `-1` if the cell never ignited.
- `burn_duration.csv`: the fixed burn time for ignited cells, or `-1`.
- `stats.csv`: one row per timestep.
- `metadata.json`: grid layout, timing, input sources and summary counts.

## Config file

The config is plain text with one `key = value` per line. `#` starts a comment. File paths are relative to the config file.

| Key | Meaning | Default |
|---|---|---|
| `terrain_file` | CSV of terrain IDs | **required** |
| `ignition` | `row col [time_s]`; repeat the line for more ignitions | **required** (at least one) |
| `cell_size_m` | Cell width in meters | 20 |
| `elevation_m` *or* `elevation_file` | Elevation (m): one constant, or a per-cell CSV | 0 |
| `moisture` *or* `moisture_file` | Fuel moisture as a fraction 0–1 | 0 |
| `wind_speed_m_s` *or* `wind_speed_file` | Wind speed in m/s | 0 |
| `wind_direction_deg` *or* `wind_direction_file` | Direction the wind blows **toward**, clockwise from north | 0 |
| `timestep_s` | Timestep | 1 |
| `seed` | Random seed | 67 |
| `max_duration_s` | Hard stop | 3600 |
| `burn_duration_s` | Fixed visual burn time | 60 |

**Per-cell files** (`*_file`) use the same layout as `terrain.csv`: one number per cell, with the same rows and columns.

**When a key is missing:**

- If it's optional, the default is used, and the solver prints a `note:` line for each default it applied.
- If it's required, the run stops with an error.

**Errors.** The run stops with a message naming the file and line, and exit code 1, before any simulation starts, when:

- a key is unknown (for example, a typo) or appears twice;
- a value is empty or not a number;
- a layer is given both as a constant and as a file;
- a layer file has the wrong size or out-of-range values;
- the terrain file contains an ID that isn't 0–6;
- an ignition is outside the grid or on non-burnable terrain.

**Terrain IDs:**

| ID | Terrain |
|---:|---|
| 0 | Broadleaves |
| 1 | Shrubs |
| 2 | Grassland |
| 3 | Fire-prone conifers |
| 4 | Agro-forestry |
| 5 | Not fire-prone forest |
| 6 | Non-vegetated (never burns) |

In every grid file, row 0 is the north edge.

## Code layout

Each function in the headers has a short In / Out / Why comment.

| File | Contents |
|---|---|
| `terrain.*` | Terrain enum, PPR Table 1 (`p_n`, `v_n`) |
| `probability.*` | PPR Eq. 1 and the spread decision (`r < p`) |
| `rng.hpp` | Deterministic draw keyed by (seed, source cell, target cell). It does not depend on processing order or MPI rank. |
| `travel_time.*` | PPR Eq. 3–6 |
| `grid.hpp` | Per-cell layers: terrain, elevation, moisture, wind speed and direction |
| `simulation.*` | Fixed-timestep loop: activate, spread once, burn, extinguish |
| `config.*`, `io.*` | Input parsing/validation and output writing |
| `main.cpp` | Command-line entry point |

## Draft limitations

- **Spread-probability modifiers are neutral.** Wind, slope and moisture currently leave the spread probability unchanged (`alpha_w = alpha_h = e_m = 1`). The paper gives these only as figures.
- **Wind and slope do change travel time.** They act through Eq. 5–6, using the burning (source) cell's wind.
- **Table 1 values to check:**
  - Grassland → conifers probability.
  - Broadleaves speed, and the units of the nominal speeds.

  Both are marked `TODO(verify)` in `src/terrain.cpp`.
- **Arrival times are rounded** to the nearest timestep, with a minimum of 1 step.
