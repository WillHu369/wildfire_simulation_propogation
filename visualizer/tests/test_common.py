import numpy as np
import pytest

from visualizer import common
from visualizer.common import NEVER_IGNITED


@pytest.fixture
def terrain():
    terrain_type = np.full((4, 5), common.GRASSLAND, dtype=np.int8)
    terrain_type[:, 4] = common.NON_VEGETATED
    return {
        "terrain_type": terrain_type,
        "elevation_m": np.linspace(0, 100, 20).reshape(4, 5),
        "fuel_moisture": np.full((4, 5), 0.1),
        "wind_speed_kmh": np.full((4, 5), 15.0),
        "wind_from_deg": np.full((4, 5), 270.0),
        "water_mask": terrain_type == common.NON_VEGETATED,
        "cell_size_m": 30.0,
    }


@pytest.fixture
def settings():
    return {
        "ignition_points": [[0, 0], [3, 3]],
        "timestep_s": 60.0,
        "max_duration_s": 3600.0,
        "seed": 42,
        "burn_duration_s": 600.0,
    }


def test_cell_states_follow_docs_table():
    times = np.array([100.0, NEVER_IGNITED])
    duration = np.array([50.0, 50.0])
    U, B, X = common.UNBURNED, common.BURNING, common.BURNED_OUT
    assert common.cell_states(times, duration, 99).tolist() == [U, U]
    assert common.cell_states(times, duration, 100).tolist() == [B, U]
    assert common.cell_states(times, duration, 149).tolist() == [B, U]
    assert common.cell_states(times, duration, 150).tolist() == [X, U]


def test_scenario_roundtrip(tmp_path, terrain, settings):
    common.save_scenario(tmp_path, terrain, settings)
    loaded, loaded_settings = common.load_scenario(tmp_path)
    assert loaded_settings == settings
    assert loaded["cell_size_m"] == 30.0
    assert loaded["terrain_type"].dtype == np.int8
    assert loaded["elevation_m"].dtype == np.float32
    np.testing.assert_array_equal(loaded["water_mask"], terrain["water_mask"])


def test_run_roundtrip(tmp_path):
    times = np.array([[0.0, 60.0], [NEVER_IGNITED, 120.0]])
    common.save_run(tmp_path, {"ignition_time_s": times, "burn_duration_s": np.ones((2, 2))})
    loaded = common.load_run(tmp_path)
    np.testing.assert_array_equal(loaded["ignition_time_s"], times)
    assert loaded["ignition_time_s"].dtype == np.float32
    assert "intensity_kw_m" not in loaded


@pytest.mark.parametrize(
    "point, message", [([4, 0], "outside the 4x5 grid"), ([0, 4], "cannot burn")]
)
def test_bad_ignition_points_are_rejected(terrain, point, message):
    with pytest.raises(ValueError, match=message):
        common.check_ignition_points(terrain["terrain_type"], [point])
