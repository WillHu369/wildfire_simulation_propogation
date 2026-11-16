"""Mock fire spread for testing the visualizer. Not PROPAGATOR."""

import argparse
import heapq
import json
from pathlib import Path

import numpy as np

from . import common

NEIGHBORS = [(-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1)]
WIND_EFFECT = 0.04
SLOPE_EFFECT = 0.03
HEAT_CONTENT_KJ_KG = 18000


def neighbor_rates(terrain, base_rate, dr, dc):
    target_rate = np.roll(base_rate, (-dr, -dc), axis=(0, 1))
    target_height = np.roll(terrain["elevation_m"], (-dr, -dc), axis=(0, 1))
    distance = terrain["cell_size_m"] * np.hypot(dr, dc)

    bearing = np.degrees(np.arctan2(dc, -dr))
    wind_to = terrain["wind_from_deg"] + 180
    tailwind = terrain["wind_speed_kmh"] * np.cos(np.radians(bearing - wind_to))
    slope_deg = np.degrees(np.arctan((target_height - terrain["elevation_m"]) / distance))
    dryness = np.clip(1 - 2.5 * terrain["fuel_moisture"], 0.1, 1)

    return target_rate * np.exp(WIND_EFFECT * tailwind) * np.exp(SLOPE_EFFECT * slope_deg) * dryness


def run_mock_spread(terrain, settings, table=None):
    table = table or common.load_terrain_table()
    terrain_type = terrain["terrain_type"]
    rows, cols = terrain_type.shape
    base_rate = common.per_terrain(terrain_type, table, "spread_rate_m_min")

    rates = [neighbor_rates(terrain, base_rate, dr, dc) for dr, dc in NEIGHBORS]
    with np.errstate(divide="ignore"):
        travel_s = [
            (terrain["cell_size_m"] * np.hypot(dr, dc) * 60 / rate).tolist()
            for (dr, dc), rate in zip(NEIGHBORS, rates, strict=True)
        ]
    rates = [rate.tolist() for rate in rates]

    time = np.full((rows, cols), np.inf)
    rate_in = np.full((rows, cols), np.nan)
    heap = []
    for r, c in settings["ignition_points"]:
        time[r, c] = 0.0
        rate_in[r, c] = base_rate[r, c]
        heap.append((0.0, r, c))

    max_time = settings["max_duration_s"]
    while heap:
        t, r, c = heapq.heappop(heap)
        if t > time[r, c]:
            continue
        for k, (dr, dc) in enumerate(NEIGHBORS):
            rr, cc = r + dr, c + dc
            if 0 <= rr < rows and 0 <= cc < cols:
                arrival = t + travel_s[k][r][c]
                if arrival <= max_time and arrival < time[rr, cc]:
                    time[rr, cc] = arrival
                    rate_in[rr, cc] = rates[k][r][c]
                    heapq.heappush(heap, (arrival, rr, cc))

    ignited = np.isfinite(time)
    fuel_load = common.per_terrain(terrain_type, table, "fuel_load_kg_m2")
    return {
        "ignition_time_s": np.where(ignited, time, common.NEVER_IGNITED),
        "burn_duration_s": common.per_terrain(terrain_type, table, "burn_duration_s"),
        "spread_rate_m_min": rate_in,
        "intensity_kw_m": HEAT_CONTENT_KJ_KG * fuel_load * rate_in / 60,
    }


def run(scenario_folder, run_folder):
    terrain, settings = common.load_scenario(scenario_folder)
    ignition = run_mock_spread(terrain, settings)
    common.save_run(run_folder, ignition)

    times = ignition["ignition_time_s"]
    metadata = {
        "model": "mock spread (demonstration only, NOT PROPAGATOR)",
        "scenario": Path(scenario_folder).name,
        "grid": {"rows": times.shape[0], "cols": times.shape[1]},
        "cell_size_m": terrain["cell_size_m"],
        "end_time_s": float(times.max()),
        "never_ignited": common.NEVER_IGNITED,
        "settings": settings,
    }
    (Path(run_folder) / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    return ignition


def main():
    parser = argparse.ArgumentParser(description="Run the MOCK fire spread (not PROPAGATOR).")
    parser.add_argument("scenario", help="scenario folder, e.g. inputs/demo")
    parser.add_argument("output", help="run folder to write, e.g. outputs/demo")
    args = parser.parse_args()
    ignition = run(args.scenario, args.output)
    burned = int(np.sum(ignition["ignition_time_s"] != common.NEVER_IGNITED))
    print(f"MOCK spread: {burned} cells ignited -> {args.output}")


if __name__ == "__main__":
    main()
