import numpy as np

from . import common, mock_spread
from .generator import REPO_ROOT, make_scenario, make_settings

SIZES = [32, 64, 128, 256, 512]
CASE_SIZE = 128


def flat_grass(n=CASE_SIZE):
    shape = (n, n)
    return {
        "terrain_type": np.full(shape, common.GRASSLAND, dtype=np.int8),
        "elevation_m": np.zeros(shape),
        "fuel_moisture": np.full(shape, 0.08),
        "wind_speed_kmh": np.zeros(shape),
        "wind_from_deg": np.zeros(shape),
        "water_mask": np.zeros(shape, dtype=bool),
        "cell_size_m": 30.0,
    }


def vegetation_patches(n=CASE_SIZE):
    terrain = flat_grass(n)
    burnable = list(range(common.NON_VEGETATED))
    block = n // 4
    for i in range(4):
        for j in range(4):
            kind = burnable[(i * 4 + j) % len(burnable)]
            terrain["terrain_type"][i * block : (i + 1) * block, j * block : (j + 1) * block] = kind
    return terrain, make_settings([[n // 2, n // 2]])


def steep_terrain(n=CASE_SIZE):
    terrain = flat_grass(n)
    r, c = np.mgrid[0:n, 0:n]
    terrain["elevation_m"] = 800 * np.exp(-(((r - n / 2) ** 2 + (c - n / 2) ** 2) / (n / 4) ** 2))
    terrain["terrain_type"][:] = common.SHRUBS
    return terrain, make_settings([[n - 5, n // 2]])


def barriers(n=CASE_SIZE):
    terrain = flat_grass(n)
    river = slice(n // 2 - 1, n // 2 + 2)
    terrain["water_mask"][:, river] = True
    road_row, gap = n // 3, slice(n // 4 - 3, n // 4 + 3)
    road = np.zeros((n, n), dtype=bool)
    road[road_row, : n // 2] = True
    road[road_row, gap] = False
    terrain["terrain_type"][terrain["water_mask"] | road] = common.NON_VEGETATED
    terrain["wind_speed_kmh"][:] = 10
    terrain["wind_from_deg"][:] = 180
    return terrain, make_settings([[n - 10, n // 4]])


def variable_wind(n=CASE_SIZE):
    terrain = flat_grass(n)
    r = np.mgrid[0:n, 0:n][0]
    terrain["wind_speed_kmh"][:] = 25
    terrain["wind_from_deg"] = 270 - 90 * r / (n - 1)
    return terrain, make_settings([[n // 2, n // 2]])


def multiple_ignitions(n=CASE_SIZE):
    return make_scenario(n, n, seed=3, ignitions=4)


CASES = [vegetation_patches, steep_terrain, barriers, variable_wind, multiple_ignitions]


def build_all():
    examples = {f"size_{n:03d}": make_scenario(n, n, seed=0) for n in SIZES}
    examples.update({case.__name__: case() for case in CASES})
    return examples


def main():
    for name, (terrain, settings) in build_all().items():
        scenario = REPO_ROOT / "inputs" / "examples" / name
        run = REPO_ROOT / "mock_outputs" / "examples" / name
        common.save_scenario(scenario, terrain, settings)
        mock_spread.run(scenario, run)
        print(f"wrote {name}")


if __name__ == "__main__":
    main()
