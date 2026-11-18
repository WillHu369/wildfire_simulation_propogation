import numpy as np
import pytest

from visualizer import common
from visualizer.generator import make_scenario
from visualizer.mock_spread import run
from visualizer.web import create_app


@pytest.fixture
def client(tmp_path):
    terrain, settings = make_scenario(16, 20, seed=1)
    scenario = tmp_path / "inputs" / "examples" / "small"
    common.save_scenario(scenario, terrain, settings)
    run(scenario, tmp_path / "mock_outputs" / "examples" / "small")
    run(scenario, tmp_path / "outputs" / "examples" / "small")
    common.save_scenario(tmp_path / "inputs" / "no_run", terrain, settings)
    return create_app(tmp_path).test_client()


def test_lists_runs_from_both_output_folders(client):
    assert client.get("/api/runs").get_json() == [
        "outputs/examples/small",
        "mock_outputs/examples/small",
    ]


def test_returns_run_data(client):
    data = client.get("/api/runs/mock_outputs/examples/small").get_json()
    assert (data["rows"], data["cols"]) == (16, 20)
    assert "NOT PROPAGATOR" in data["model"]
    for name in list(common.TERRAIN_DTYPES) + list(common.IGNITION_DTYPES):
        assert len(data["arrays"][name]) == 16 * 20
    times = np.array(data["arrays"]["ignition_time_s"])
    rates = data["arrays"]["spread_rate_m_min"]
    assert all(rates[i] is None for i in np.flatnonzero(times == common.NEVER_IGNITED))


@pytest.mark.parametrize(
    "run_id", ["mock_outputs/no_run", "mock_outputs/missing", "inputs/examples/small"]
)
def test_unknown_runs_are_404(client, run_id):
    assert client.get(f"/api/runs/{run_id}").status_code == 404


def test_serves_viewer_page_and_terrain_colors(client):
    assert b"app.js" in client.get("/").data
    assert client.get("/static/vendor/three.module.min.js").status_code == 200
    types = client.get("/api/runs/mock_outputs/examples/small").get_json()["terrain_types"]
    assert all(t["color"].startswith("#") for t in types.values())
