"""Generate test landscapes in the format described in common.py."""

import numpy as np
from scipy.ndimage import gaussian_filter

from . import common

# Preferred elevation (fraction of the maximum) for each vegetation class.
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
    """Hills plus one ridge plus rolling noise, scaled to [0, max_elevation_m]."""
    r, c = np.mgrid[0:rows, 0:cols].astype(float)
    size = min(rows, cols)

    land = 0.3 * smooth_noise(rng, (rows, cols), size / 12)
    for _ in range(rng.integers(3, 6)):
        r0, c0 = rng.uniform(0, rows), rng.uniform(0, cols)
        radius = rng.uniform(0.08, 0.2) * size
        land += rng.uniform(0.5, 1.0) * gaussian_band(np.hypot(r - r0, c - c0), radius)

    # Ridge along a random line through the grid.
    angle = rng.uniform(0, np.pi)
    r0, c0 = rng.uniform(0.3, 0.7) * rows, rng.uniform(0.3, 0.7) * cols
    distance = np.abs((r - r0) * np.cos(angle) - (c - c0) * np.sin(angle))
    land += 0.8 * gaussian_band(distance, 0.05 * size)

    land = (land - land.min()) / (land.max() - land.min())
    return land * max_elevation_m


def carve_river_and_lake(rng, elevation, max_elevation_m):
    """Carve a valley from the north edge down to a lake. Returns (elevation, water)."""
    rows, cols = elevation.shape
    r, c = np.mgrid[0:rows, 0:cols].astype(float)
    size = min(rows, cols)

    lake_row = rng.uniform(0.65, 0.8) * rows
    phase, wiggle = rng.uniform(0, 2 * np.pi), rng.uniform(0.05, 0.1) * cols
    river_col = rng.uniform(0.35, 0.65) * cols + wiggle * np.sin(r / rows * 3 * np.pi + phase)
    lake_col = river_col[int(lake_row), 0]

    # Valley floor slopes down from the north edge to the lake.
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
    """Pick the class whose preferred elevation fits best, plus smooth noise for patches."""
    height = elevation / max_elevation_m
    sigma = min(elevation.shape) / 15
    classes = list(VEGETATION_ELEVATION)
    scores = [
        0.15 * smooth_noise(rng, elevation.shape, sigma) - abs(height - VEGETATION_ELEVATION[k])
        for k in classes
    ]
    return np.array(classes, dtype=np.int8)[np.argmax(scores, axis=0)]


def make_roads(rng, rows, cols):
    """One road west to east and one north to south, each through random waypoints."""
    roads = np.zeros((rows, cols), dtype=bool)
    for along, across, horizontal in ((cols, rows, True), (rows, cols, False)):
        stops = np.linspace(0, along - 1, 4)
        offsets = rng.uniform(0.2, 0.8, size=4) * (across - 1)
        t = np.linspace(0, along - 1, 4 * along)
        a = np.round(t).astype(int)
        b = np.round(np.interp(t, stops, offsets)).astype(int)
        rr, cc = (b, a) if horizontal else (a, b)
        roads[rr, cc] = True
        roads[rr[:-1], cc[1:]] = True  # fill diagonal steps so fire cannot slip through
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
