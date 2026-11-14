import json
from pathlib import Path

import numpy as np

BROADLEAVES = 0
SHRUBS = 1
GRASSLAND = 2
FIRE_PRONE_CONIFERS = 3
AGRO_FORESTRY = 4
NOT_FIRE_PRONE_FOREST = 5
NON_VEGETATED = 6  # roads, water, bare ground; cannot burn

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


def is_burnable(terrain_type):
    return terrain_type != NON_VEGETATED


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


def _cast(arrays, dtypes):
    return {name: np.asarray(arrays[name], dtype=dtypes[name]) for name in dtypes if name in arrays}


def save_scenario(folder, terrain, settings):
    check_ignition_points(terrain["terrain_type"], settings["ignition_points"])
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        folder / "terrain.npz",
        cell_size_m=float(terrain["cell_size_m"]),
        **_cast(terrain, TERRAIN_DTYPES),
    )
    (folder / "settings.json").write_text(json.dumps(settings, indent=2) + "\n")


def load_scenario(folder):
    folder = Path(folder)
    with np.load(folder / "terrain.npz") as npz:
        terrain = dict(npz)
    terrain["cell_size_m"] = float(terrain["cell_size_m"])
    settings = json.loads((folder / "settings.json").read_text())
    return terrain, settings


def save_run(folder, ignition):
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(folder / "ignition.npz", **_cast(ignition, IGNITION_DTYPES))


def load_run(folder):
    with np.load(Path(folder) / "ignition.npz") as npz:
        return dict(npz)
