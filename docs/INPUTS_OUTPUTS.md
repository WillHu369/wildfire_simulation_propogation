# Solver Inputs and Outputs

The agreed file formats between the generator/visualizer and the solver.

## Folder layout

Each scenario is a folder under `inputs/examples/<name>/`. The solver writes its results to the matching folder under `outputs/examples/<name>/` (for example, `inputs/examples/size_032` → `outputs/examples/size_032`). How serial and parallel outputs are kept apart is still to be decided.

## Inputs

All grid files are CSVs with the same rows and columns as `terrain_type.csv`. Row 0 is the north edge, column 0 is the west edge, and storage is row-major.

| File | Contents | Units | Used by |
|---|---|---|---|
| `settings.json` | Run settings (below) | | solver, visualizer |
| `terrain_type.csv` | Terrain ID per cell (see below) | integer 0–6 | solver, visualizer |
| `elevation_m.csv` | Elevation | m | solver, visualizer |
| `fuel_moisture.csv` | Fuel moisture | fraction 0–1 | solver, visualizer |
| `wind_speed_kmh.csv` | Wind speed | km/h | solver, visualizer |
| `wind_from_deg.csv` | Direction the wind blows **from**, clockwise from north (meteorological convention) | degrees | solver, visualizer |
| `water_mask.csv` | 1 where the cell is water | 0/1 | visualizer only |

Water cells have terrain ID 6 (non-vegetated), so to the solver they behave like roads or bare ground. The mask only tells the visualizer to draw them as water.

### settings.json

```json
{
  "ignition_points": [[46, 21], [48, 84]],
  "timestep_s": 1.0,
  "max_duration_s": 21600.0,
  "seed": 3,
  "burn_duration_s": 600.0,
  "cell_size_m": 30.0
}
```

| Key | Meaning | Used by |
|---|---|---|
| `ignition_points` | One or more `[row, col]` cells that ignite at `t = 0`; each must be inside the grid and burnable | solver, visualizer |
| `timestep_s` | Simulation timestep, s. Keep it at most half the fastest cell crossing (the solver warns otherwise); the examples use 1 s. The visualizer can show frames at any interval regardless. | solver |
| `max_duration_s` | Hard stop, s | solver |
| `seed` | Random seed (non-negative integer); same seed + same inputs = same fire | solver |
| `cell_size_m` | Cell width, m | solver, visualizer |
| `burn_duration_s` | How long each ignited cell is shown burning, s | visualizer only |

### Terrain IDs

| ID | Terrain | Burnable |
|---:|---|:---:|
| 0 | Broadleaves | yes |
| 1 | Shrubs | yes |
| 2 | Grassland | yes |
| 3 | Fire-prone conifers | yes |
| 4 | Agro-forestry | yes |
| 5 | Not fire-prone forest | yes |
| 6 | Non-vegetated (roads, water, bare ground) | no |

The model parameters (PROPAGATOR Table 1 spread probabilities, nominal spread speeds in m/min, and equation constants) are built into the solver.

## Outputs

| File | Contents |
|---|---|
| `ignition_time_s.csv` | Seconds since the start at which each cell ignited, or `-1` if it never did. Same shape and orientation as the inputs, three decimals. |
| `metadata.json` | `model`, `solver`, `scenario`, `grid` (rows, cols), `cell_size_m`, `end_time_s`, `never_ignited` (-1), `run` (wall time, steps, fastest cell crossing `min_travel_time_s`, counts) and the `settings` used. |

## How the visualizer uses the outputs

The visualizer draws the terrain from the input grids. For each frame time `t`, it works out each cell's state from `ignition_time_s.csv` and the `burn_duration_s` setting:

| Condition | State |
|---|---|
| ignition time is `-1`, or `t <` ignition time | unburned |
| ignition time `≤ t <` ignition time + burn duration | burning |
| `t ≥` ignition time + burn duration | burned out |
