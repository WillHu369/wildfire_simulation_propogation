#include "terrain.hpp"

namespace wildfire {
namespace {

// PPR Table 1 (Trucchia et al. 2020), stored as kSpreadProbability[source][target].
//
// The paper prints the BURNING SOURCE as columns and the TARGET as rows, so this
// array is the TRANSPOSE of the printed layout. Values from the project handoff.
//
// TODO(verify): Grassland -> Fire-prone conifers is 0.250 here but 0.1 in the
// CIMA reference code (propagator_sim). Check against the paper PDF.
constexpr double kSpreadProbability[kNumBurnableTerrains][kNumBurnableTerrains] = {
  // target:            Broadl  Shrubs  Grass   Conif   Agro    NotFP
  /* Broadleaves   */ { 0.300,  0.375,  0.450,  0.225,  0.250,  0.075 },
  /* Shrubs        */ { 0.375,  0.375,  0.475,  0.325,  0.250,  0.100 },
  /* Grassland     */ { 0.250,  0.350,  0.475,  0.250,  0.300,  0.075 },
  /* Conifers      */ { 0.275,  0.400,  0.475,  0.350,  0.475,  0.275 },
  /* Agro-forestry */ { 0.250,  0.300,  0.375,  0.200,  0.350,  0.075 },
  /* Not fire-prone*/ { 0.250,  0.375,  0.475,  0.350,  0.250,  0.075 },
};

// PPR Table 1 nominal source speeds v_n, meters per minute.
// TODO(verify): Broadleaves is 100 here but 140 in the CIMA reference code,
// which also treats these values as m/h. Check value and units in the paper.
constexpr double kNominalSpeed[kNumBurnableTerrains] = {
  100.0, 140.0, 120.0, 200.0, 120.0, 60.0,
};

constexpr const char* kNames[kNumTerrains] = {
  "Broadleaves",
  "Shrubs",
  "Grassland",
  "Fire-prone conifers",
  "Agro-forestry",
  "Not fire-prone forest",
  "Non-vegetated",
};

int to_index(Terrain t) { return static_cast<int>(t); }

}  // namespace

bool is_burnable(Terrain t) {
  return to_index(t) >= 0 && to_index(t) < kNumBurnableTerrains;
}

std::optional<Terrain> terrain_from_id(int id) {
  if (id < 0 || id >= kNumTerrains) return std::nullopt;
  return static_cast<Terrain>(id);
}

const char* terrain_name(Terrain t) {
  const int i = to_index(t);
  return (i >= 0 && i < kNumTerrains) ? kNames[i] : "Unknown";
}

double nominal_probability(Terrain source, Terrain target) {
  if (!is_burnable(source) || !is_burnable(target)) return 0.0;
  return kSpreadProbability[to_index(source)][to_index(target)];
}

double nominal_speed_m_per_min(Terrain source) {
  if (!is_burnable(source)) return 0.0;
  return kNominalSpeed[to_index(source)];
}

}  // namespace wildfire
