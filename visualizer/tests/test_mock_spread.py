import json

import numpy as np
import pytest

from visualizer import common
from visualizer.mock_spread import run, run_mock_spread

NEVER = common.NEVER_IGNITED


def flat_grass(rows=21, cols=21, wind_kmh=0.0, wind_from=270.0):
    shape = (rows, cols)
    return {
        "terrain_type": np.full(shape, common.GRASSLAND, dtype=np.int8),
        "elevation_m": np.zeros(shape),
        "fuel_moisture": np.full(shape, 0.08),
        "wind_speed_kmh": np.full(shape, wind_kmh),
        "wind_from_deg": np.full(shape, wind_from),
        "cell_size_m": 30.0,
    }


def settings(points, max_duration_s=24 * 3600):
    return {
        "ignition_points": points,
        "timestep_s": 60.0,
        "max_duration_s": max_duration_s,
        "seed": 0,
        "burn_duration_s": 600.0,
    }


def test_ignition_points_start_at_zero():
    out = run_mock_spread(flat_grass(), settings([[5, 5], [15, 15]]))
    assert out["ignition_time_s"][5, 5] == 0
    assert out["ignition_time_s"][15, 15] == 0
    assert np.all(out["ignition_time_s"] >= 0)


def test_spread_is_symmetric_without_wind_or_slope():
    t = run_mock_spread(flat_grass(), settings([[10, 10]]))["ignition_time_s"]
    assert t[10, 15] == pytest.approx(t[10, 5])
    assert t[5, 10] == pytest.approx(t[15, 10])


def test_wind_from_west_pushes_fire_east():
    t = run_mock_spread(flat_grass(wind_kmh=20), settings([[10, 10]]))["ignition_time_s"]
    assert t[10, 15] < t[10, 5]


def test_fire_runs_faster_uphill():
    terrain = flat_grass()
    terrain["elevation_m"] = np.tile(np.arange(21) * 10.0, (21, 1))
    t = run_mock_spread(terrain, settings([[10, 10]]))["ignition_time_s"]
    assert t[10, 15] < t[10, 5]


def test_non_burnable_barrier_stops_fire():
    terrain = flat_grass()
    terrain["terrain_type"][:, 12] = common.NON_VEGETATED
    out = run_mock_spread(terrain, settings([[10, 5]]))
    assert np.all(out["ignition_time_s"][:, 12:] == NEVER)
    assert np.all(out["ignition_time_s"][:, :12] >= 0)


def test_max_duration_limits_spread():
    out = run_mock_spread(flat_grass(), settings([[10, 10]], max_duration_s=120))
    times = out["ignition_time_s"]
    assert times[0, 0] == NEVER
    assert times[times != NEVER].max() <= 120


def test_outputs_follow_terrain_table():
    terrain = flat_grass()
    terrain["terrain_type"][:, 15:] = common.FIRE_PRONE_CONIFERS
    out = run_mock_spread(terrain, settings([[10, 10]], max_duration_s=600))
    table = common.load_terrain_table()

    assert out["burn_duration_s"][0, 0] == table[common.GRASSLAND]["burn_duration_s"]
    assert out["burn_duration_s"][0, 20] == table[common.FIRE_PRONE_CONIFERS]["burn_duration_s"]
    never = out["ignition_time_s"] == NEVER
    assert never.any()
    assert np.all(np.isnan(out["spread_rate_m_min"][never]))
    assert np.all(np.isnan(out["intensity_kw_m"][never]))
    assert np.all(out["intensity_kw_m"][~never] > 0)


def test_run_writes_ignition_and_mock_metadata(tmp_path):
    terrain = flat_grass()
    common.save_scenario(tmp_path / "scenario", terrain, settings([[10, 10]]))
    run(tmp_path / "scenario", tmp_path / "run")

    loaded = common.load_run(tmp_path / "run")
    assert set(loaded) == set(common.IGNITION_DTYPES)
    metadata = json.loads((tmp_path / "run" / "metadata.json").read_text())
    assert "NOT PROPAGATOR" in metadata["model"]
