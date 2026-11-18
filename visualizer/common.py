import json
from pathlib import Path

import numpy as np

BROADLEAVES = 0
SHRUBS = 1
GRASSLAND = 2
FIRE_PRONE_CONIFERS = 3
AGRO_FORESTRY = 4
NOT_FIRE_PRONE_FOREST = 5
NON_VEGETATED = 6

NEVER_IGNITED = -1.0

UNBURNED, BURNING, BURNED_OUT = 0, 1, 2

TERRAIN_DTYPES = {
    "terrain_type": np.int8,
    "elevation_m": np.float32,
    "fuel_moisture": np.float32,
    "wind_speed_kmh": np.float32,
    "wind_from_deg": np.float32,
    "water_mask": np.bool_,
}
IGNITION_DTYPES = {
    "ignition_time_s": np.float32,
    "burn_duration_s": np.float32,
    "spread_rate_m_min": np.float32,
    "intensity_kw_m": np.float32,
}


TERRAIN_TABLE = Path(__file__).parent / "config" / "terrain_types.json"


def is_burnable(terrain_type):
    return terrain_type != NON_VEGETATED


def load_terrain_table(path=TERRAIN_TABLE):
    types = json.loads(Path(path).read_text())
    return {int(k): v for k, v in types.items()}


def per_terrain(terrain_type, table, key):
    lookup = np.array([table[i][key] for i in range(NON_VEGETATED + 1)], dtype=np.float32)
    return lookup[terrain_type]


def cell_states(ignition_time_s, burn_duration_s, t):
    ignited = (ignition_time_s != NEVER_IGNITED) & (ignition_time_s <= t)
    states = np.where(ignited, BURNING, UNBURNED).astype(np.int8)
    states[ignited & (t >= ignition_time_s + burn_duration_s)] = BURNED_OUT
    return states


def check_ignition_points(terrain_type, ignition_points):
    rows, cols = terrain_type.shape
    for r, c in ignition_points:
        if not (0 <= r < rows and 0 <= c < cols):
            raise ValueError(f"ignition point ({r}, {c}) is outside the {rows}x{cols} grid")
        if not is_burnable(terrain_type[r, c]):
            raise ValueError(f"ignition point ({r}, {c}) is on a cell that cannot burn")


def write_grids(folder, arrays, dtypes):
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    for name, dtype in dtypes.items():
        if name in arrays:
            values = np.asarray(arrays[name], dtype=dtype)
            fmt = "%.3f" if values.dtype.kind == "f" else "%d"
            np.savetxt(folder / f"{name}.csv", values, fmt=fmt, delimiter=",")


def read_grids(folder, dtypes):
    folder = Path(folder)
    return {
        name: np.loadtxt(folder / f"{name}.csv", delimiter=",", ndmin=2).astype(dtype)
        for name, dtype in dtypes.items()
        if (folder / f"{name}.csv").exists()
    }


def save_scenario(folder, terrain, settings):
    check_ignition_points(terrain["terrain_type"], settings["ignition_points"])
    write_grids(folder, terrain, TERRAIN_DTYPES)
    settings = {**settings, "cell_size_m": float(terrain["cell_size_m"])}
    (Path(folder) / "settings.json").write_text(json.dumps(settings, indent=2) + "\n")


def load_scenario(folder):
    terrain = read_grids(folder, TERRAIN_DTYPES)
    settings = json.loads((Path(folder) / "settings.json").read_text())
    terrain["cell_size_m"] = float(settings.pop("cell_size_m"))
    return terrain, settings


def save_run(folder, ignition):
    write_grids(folder, ignition, IGNITION_DTYPES)


def load_run(folder):
    return read_grids(folder, IGNITION_DTYPES)
