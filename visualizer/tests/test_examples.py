import numpy as np
import pytest

from visualizer import common
from visualizer.examples import CASES, REPO_ROOT, SIZES, multiple_ignitions
from visualizer.generator import make_scenario
from visualizer.mock_spread import run_mock_spread

NAMES = [f"size_{n:03d}" for n in SIZES] + [case.__name__ for case in CASES]


@pytest.mark.parametrize("case", CASES, ids=lambda case: case.__name__)
def test_case_is_valid_and_burns(case):
    terrain, settings = case(32)
    common.check_ignition_points(terrain["terrain_type"], settings["ignition_points"])
    times = run_mock_spread(terrain, settings)["ignition_time_s"]
    assert np.sum(times != common.NEVER_IGNITED) > len(settings["ignition_points"])


def test_multiple_ignitions_has_several_points():
    assert len(multiple_ignitions(32)[1]["ignition_points"]) > 1


@pytest.mark.parametrize("name", NAMES)
def test_committed_example_exists(name):
    assert (REPO_ROOT / "inputs" / "examples" / name / "terrain.npz").exists()
    assert (REPO_ROOT / "outputs" / "examples" / name / "ignition.npz").exists()


def test_committed_small_example_matches_generator():
    terrain, settings = make_scenario(32, 32, seed=0)
    saved, saved_settings = common.load_scenario(REPO_ROOT / "inputs" / "examples" / "size_032")
    assert saved_settings == settings
    np.testing.assert_array_equal(saved["terrain_type"], terrain["terrain_type"])
