import argparse
from pathlib import Path

import numpy as np
from scipy.ndimage import gaussian_filter

from . import common, mock_spread

REPO_ROOT = Path(__file__).resolve().parents[1]

VEGETATION_ELEVATION = {
    common.GRASSLAND: 0.10,
    common.AGRO_FORESTRY: 0.20,
    common.SHRUBS: 0.45,
    common.BROADLEAVES: 0.50,
    common.NOT_FIRE_PRONE_FOREST: 0.60,
    common.FIRE_PRONE_CONIFERS: 0.85,
}


def smooth_noise(rng, shape, sigma):
    noise = gaussian_filter(rng.standard_normal(shape), sigma)
    return (noise - noise.mean()) / noise.std()


def gaussian_band(distance, width):
    return np.exp(-((distance / width) ** 2))


def make_elevation(rng, rows, cols, max_elevation_m):
    r, c = np.mgrid[0:rows, 0:cols].astype(float)
    size = min(rows, cols)

    land = 0.3 * smooth_noise(rng, (rows, cols), size / 12)
    for _ in range(rng.integers(3, 6)):
        r0, c0 = rng.uniform(0, rows), rng.uniform(0, cols)
        radius = rng.uniform(0.08, 0.2) * size
        land += rng.uniform(0.5, 1.0) * gaussian_band(np.hypot(r - r0, c - c0), radius)

    angle = rng.uniform(0, np.pi)
    r0, c0 = rng.uniform(0.3, 0.7) * rows, rng.uniform(0.3, 0.7) * cols
    distance = np.abs((r - r0) * np.cos(angle) - (c - c0) * np.sin(angle))
    land += 0.8 * gaussian_band(distance, 0.05 * size)

    land = (land - land.min()) / (land.max() - land.min())
    return land * max_elevation_m


def carve_river_and_lake(rng, elevation, max_elevation_m):
    rows, cols = elevation.shape
    r, c = np.mgrid[0:rows, 0:cols].astype(float)
    size = min(rows, cols)

    lake_row = rng.uniform(0.65, 0.8) * rows
    phase, wiggle = rng.uniform(0, 2 * np.pi), rng.uniform(0.05, 0.1) * cols
    river_col = rng.uniform(0.35, 0.65) * cols + wiggle * np.sin(r / rows * 3 * np.pi + phase)
    lake_col = river_col[int(lake_row), 0]

    floor = np.interp(r, [0, lake_row], [0.2, 0.04]) * max_elevation_m
    floor = np.minimum(floor, elevation)
    valley = gaussian_band(np.abs(c - river_col), 0.08 * size) * (r <= lake_row)
    elevation = elevation * (1 - valley) + floor * valley

    radius_r, radius_c = rng.uniform(0.07, 0.1) * size, rng.uniform(0.09, 0.14) * size
    lake_distance = np.hypot((r - lake_row) / radius_r, (c - lake_col) / radius_c)
    lake_distance += 0.15 * smooth_noise(rng, elevation.shape, size / 15)
    lake_level = 0.03 * max_elevation_m
    basin = gaussian_band(lake_distance, 1.5)
    elevation = elevation * (1 - basin) + lake_level * basin

    lake = lake_distance < 1
    river = (np.abs(c - river_col) < 1.5) & (r <= lake_row)
    elevation[lake] = lake_level
    return elevation, lake | river


def make_vegetation(rng, elevation, max_elevation_m):
    height = elevation / max_elevation_m
    sigma = min(elevation.shape) / 15
    classes = list(VEGETATION_ELEVATION)
    scores = [
        0.15 * smooth_noise(rng, elevation.shape, sigma) - abs(height - VEGETATION_ELEVATION[k])
        for k in classes
    ]
    return np.array(classes, dtype=np.int8)[np.argmax(scores, axis=0)]


def make_roads(rng, rows, cols):
    roads = np.zeros((rows, cols), dtype=bool)
    for along, across, horizontal in ((cols, rows, True), (rows, cols, False)):
        stops = np.linspace(0, along - 1, 4)
        offsets = rng.uniform(0.2, 0.8, size=4) * (across - 1)
        t = np.linspace(0, along - 1, 4 * along)
        a = np.round(t).astype(int)
        b = np.round(np.interp(t, stops, offsets)).astype(int)
        rr, cc = (b, a) if horizontal else (a, b)
        roads[rr, cc] = True
        roads[rr[:-1], cc[1:]] = True  # close diagonal gaps
    return roads


def make_terrain(rows=200, cols=200, seed=0, cell_size_m=30.0, max_elevation_m=800.0):
    rng = np.random.default_rng(seed)
    elevation = make_elevation(rng, rows, cols, max_elevation_m)
    elevation, water = carve_river_and_lake(rng, elevation, max_elevation_m)

    terrain_type = make_vegetation(rng, elevation, max_elevation_m)
    rocky_peaks = (elevation > 0.9 * max_elevation_m) & (smooth_noise(rng, elevation.shape, 2) > 0)
    terrain_type[rocky_peaks | water | make_roads(rng, rows, cols)] = common.NON_VEGETATED

    return {
        "terrain_type": terrain_type,
        "elevation_m": elevation,
        "water_mask": water,
        "cell_size_m": cell_size_m,
    }


def make_wind(rng, elevation, speed_kmh, from_deg):
    height = (elevation - elevation.mean()) / elevation.std()
    speed = speed_kmh * np.clip(1 + 0.25 * height, 0.3, None)
    direction = from_deg + 20 * smooth_noise(rng, elevation.shape, min(elevation.shape) / 8)
    return speed, direction % 360


def pick_ignitions(rng, terrain_type, count):
    rows, cols = np.nonzero(common.is_burnable(terrain_type))
    picks = rng.choice(len(rows), size=count, replace=False)
    return [[int(rows[i]), int(cols[i])] for i in picks]


def make_scenario(
    rows=200, cols=200, seed=0, ignitions=1, wind_kmh=20.0, wind_from_deg=None, moisture=0.08
):
    terrain = make_terrain(rows, cols, seed)
    rng = np.random.default_rng((seed, 1))
    if wind_from_deg is None:
        wind_from_deg = rng.uniform(0, 360)
    speed, direction = make_wind(rng, terrain["elevation_m"], wind_kmh, wind_from_deg)
    terrain["wind_speed_kmh"] = speed
    terrain["wind_from_deg"] = direction
    terrain["fuel_moisture"] = np.full((rows, cols), moisture)

    points = pick_ignitions(rng, terrain["terrain_type"], ignitions)
    return terrain, make_settings(points, seed)


def make_settings(ignition_points, seed=0):
    return {
        "ignition_points": ignition_points,
        "timestep_s": 60.0,
        "max_duration_s": 6 * 3600.0,
        "seed": seed,
        "burn_duration_s": 600.0,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Generate a test scenario and a MOCK fire spread (not PROPAGATOR)."
    )
    parser.add_argument("--name", required=True)
    parser.add_argument("--rows", type=int, default=200)
    parser.add_argument("--cols", type=int, default=200)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--ignitions", type=int, default=1)
    parser.add_argument("--wind-kmh", type=float, default=20.0)
    parser.add_argument("--wind-from", type=float, help="degrees; random if omitted")
    parser.add_argument("--root", type=Path, default=REPO_ROOT)
    args = parser.parse_args(argv)

    terrain, settings = make_scenario(
        args.rows, args.cols, args.seed, args.ignitions, args.wind_kmh, args.wind_from
    )
    scenario = args.root / "inputs" / args.name
    run = args.root / "outputs" / args.name
    common.save_scenario(scenario, terrain, settings)
    mock_spread.run(scenario, run)
    print(f"wrote {scenario} and {run} (mock spread)")


if __name__ == "__main__":
    main()
