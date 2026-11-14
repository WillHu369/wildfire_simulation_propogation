import numpy as np
import pytest

from visualizer import common
from visualizer.generator import make_terrain


@pytest.mark.parametrize("shape", [(200, 200), (120, 300), (40, 40)])
def test_terrain_has_every_feature(shape):
    terrain = make_terrain(*shape, seed=1)
    kind = terrain["terrain_type"]
    water = terrain["water_mask"]

    assert kind.shape == water.shape == terrain["elevation_m"].shape == shape
    assert water.any()
    assert np.all(kind[water] == common.NON_VEGETATED)
    assert np.any((kind == common.NON_VEGETATED) & ~water)  # roads / bare ground
    assert len(np.unique(kind[kind != common.NON_VEGETATED])) >= 4
    assert terrain["elevation_m"].min() >= 0
    assert terrain["elevation_m"].max() <= 800


def test_same_seed_gives_same_terrain():
    a, b = make_terrain(seed=7), make_terrain(seed=7)
    for name in ("terrain_type", "elevation_m", "water_mask"):
        np.testing.assert_array_equal(a[name], b[name])


def test_different_seeds_give_different_terrain():
    a, b = make_terrain(seed=7), make_terrain(seed=8)
    assert not np.array_equal(a["elevation_m"], b["elevation_m"])


def test_lake_surface_is_flat():
    terrain = make_terrain(seed=3)
    water_heights = terrain["elevation_m"][terrain["water_mask"]]
    assert np.sum(water_heights == water_heights.min()) > 100
