# Solver Inputs and Outputs (Draft)

The ideal inputs and outputs for the wildfire solver. This is a draft; file formats and names may change.

## Inputs

1. **Terrain grid** (`rows × cols`). Each cell holds:
   - **Terrain type** (integer ID, see below). Required.
   - **Elevation** (meters). Constant placeholder for now.
   - **Fuel moisture** (fraction 0–1). Constant placeholder for now.
   - **Wind Speed** (m/s).
   - **Direction of wind** (degrees clockwise from north, the direction the wind blows *toward*).

   Terrain type comes from `terrain.csv`. Each other layer is either one constant for every cell (the optional default in the settings below) or its own CSV with the same shape as `terrain.csv`: `elevation.csv`, `moisture.csv`, `wind_speed.csv`, `wind_direction.csv`. Per-cell wind allows local wind effects on large real-world maps later, without changing the input format.

2. **Ignition point:** the `(row, col)` cell where the fire starts at `t = 0`. It must be inside the grid and burnable.
3. **Simulation settings:**
   - Optional default wind speed and direction.
   - Timestep (seconds).
   - Random seed.
   - Maximum duration (seconds).
   - Burn duration (seconds): how long each ignited cell is shown as burning. Display only; it does not affect spread.
4. **Model parameters:** published values from the PROPAGATOR paper (Trucchia et al. 2020).
   - Table 1 spread probabilities for each pair of terrain types.
   - Nominal spread speeds.
   - The constants used in the spread equations.

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

**Grid orientation:** in every grid file, row 0 is the north edge, column 0 is the west edge, and storage is row-major.

## Outputs

1. **Ignition grid** (required): same size and orientation as the terrain grid. Each cell holds:
   - **Ignition time:** seconds since the start, or `-1` if the cell never ignited.
   - **Burn duration:** seconds the cell is shown as burning. For now this is the same fixed value for every cell, taken from the settings.
2. **`metadata.json`** (optional):
   - Grid dimensions and cell size.
   - Timing: timestep, maximum duration and actual end time.
   - Data types, units and sentinel values.
   - Grid layout.
   - The seed and settings used.
3. **`stats.csv`** (optional): summary statistics over time, such as the number of burning cells, the number of burned cells and the burned area.

## How the visualizer uses the outputs

The visualizer draws the terrain from the terrain grid. For each frame time `t`, it reads each cell's state from the ignition grid:

| Condition | State |
|---|---|
| ignition time is `-1`, or `t <` ignition time | unburned |
| ignition time `≤ t <` ignition time + burn duration | burning |
| `t ≥` ignition time + burn duration | burned out |
