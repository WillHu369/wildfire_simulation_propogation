import argparse
import json
import math

import numpy as np
from flask import Flask, abort, jsonify

from . import common
from .generator import REPO_ROOT

OUTPUT_DIRS = ("outputs", "mock_outputs")


def find_runs(root):
    runs = []
    for terrain in sorted((root / "inputs").rglob("terrain_type.csv")):
        name = terrain.parent.relative_to(root / "inputs").as_posix()
        for output_dir in OUTPUT_DIRS:
            if (root / output_dir / name / "ignition_time_s.csv").exists():
                runs.append(f"{output_dir}/{name}")
    return runs


def to_list(arr):
    values = np.asarray(arr, dtype=float).ravel().round(3).tolist()
    return [None if math.isnan(v) else v for v in values]


def load_for_browser(root, run_id):
    output_dir, name = run_id.split("/", 1)
    terrain, settings = common.load_scenario(root / "inputs" / name)
    ignition = common.load_run(root / output_dir / name)
    metadata_path = root / output_dir / name / "metadata.json"
    metadata = json.loads(metadata_path.read_text()) if metadata_path.exists() else {}

    rows, cols = terrain["terrain_type"].shape
    arrays = {k: v for k, v in terrain.items() if k != "cell_size_m"} | ignition
    return {
        "run": run_id,
        "model": metadata.get("model", "unknown"),
        "rows": rows,
        "cols": cols,
        "cell_size_m": terrain["cell_size_m"],
        "ignition_points": settings["ignition_points"],
        "never_ignited": common.NEVER_IGNITED,
        "terrain_types": common.load_terrain_table(),
        "arrays": {k: to_list(v) for k, v in arrays.items()},
    }


def create_app(root=REPO_ROOT):
    app = Flask(__name__)

    @app.get("/")
    def index():
        return app.send_static_file("index.html")

    @app.get("/api/runs")
    def runs():
        return jsonify(find_runs(root))

    @app.get("/api/runs/<path:run_id>")
    def run(run_id):
        if run_id not in find_runs(root):
            abort(404)
        return jsonify(load_for_browser(root, run_id))

    return app


def main():
    parser = argparse.ArgumentParser(description="Serve the wildfire visualizer website.")
    parser.add_argument("--port", type=int, default=5000)
    args = parser.parse_args()
    create_app().run(host="127.0.0.1", port=args.port)


if __name__ == "__main__":
    main()
