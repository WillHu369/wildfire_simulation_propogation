import numpy as np
import pytest

from visualizer import common
from visualizer.generator import main, make_scenario, make_terrain


@pytest.mark.parametrize("shape", [(200, 200), (120, 300), (40, 40)])
def test_terrain_has_every_feature(shape):
    terrain = make_terrain(*shape, seed=1)
    kind = terrain["terrain_type"]
    water = terrain["water_mask"]

    assert kind.shape == water.shape == terrain["elevation_m"].shape == shape
    assert water.any()
    assert np.all(kind[water] == common.NON_VEGETATED)
    assert np.any((kind == common.NON_VEGETATED) & ~water)
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


def test_scenario_is_complete_and_saveable(tmp_path):
    terrain, settings = make_scenario(60, 80, seed=2, ignitions=3)
    for name in common.TERRAIN_DTYPES:
        assert terrain[name].shape == (60, 80)
    assert len(settings["ignition_points"]) == 3
    assert np.all((terrain["wind_from_deg"] >= 0) & (terrain["wind_from_deg"] < 360))
    assert np.all(terrain["wind_speed_kmh"] > 0)
    common.save_scenario(tmp_path, terrain, settings)


def test_same_seed_gives_same_scenario():
    (a, sa), (b, sb) = make_scenario(seed=4), make_scenario(seed=4)
    assert sa == sb
    np.testing.assert_array_equal(a["wind_from_deg"], b["wind_from_deg"])


def test_wind_follows_requested_direction():
    terrain, _ = make_scenario(seed=1, wind_from_deg=90)
    assert abs(np.median(terrain["wind_from_deg"]) - 90) < 15


def test_cli_writes_scenario_and_mock_run(tmp_path):
    main(["--name", "demo", "--rows", "50", "--cols", "50", "--root", str(tmp_path)])
    terrain, settings = common.load_scenario(tmp_path / "inputs" / "demo")
    ignition = common.load_run(tmp_path / "mock_outputs" / "demo")
    assert ignition["ignition_time_s"].shape == terrain["terrain_type"].shape
    r, c = settings["ignition_points"][0]
    assert ignition["ignition_time_s"][r, c] == 0
