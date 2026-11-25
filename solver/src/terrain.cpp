#include "terrain.hpp"

namespace wildfire {
namespace {

// PROPAGATOR Table 1 (Trucchia et al. 2020), nominal spread probability p_n.
//
// Laid out EXACTLY as printed in the paper so it can be checked by eye:
//   rows    = NEIGHBOR (target) cell
//   columns = BURNING (source) cell
// so the lookup is kSpreadProbability[target][source]. The table is not
// symmetric: e.g. Grassland -> Shrubs is 0.35 but Shrubs -> Grassland is 0.475.
//
// Verified against the paper. The CIMA reference code (propagator_sim) uses
// 0.1 for Grassland -> Fire-prone conifers; the paper says 0.25.
constexpr double kSpreadProbability[kNumBurnableTerrains][kNumBurnableTerrains] = {
    // burning (source):  Broadl  Shrubs  Grass   Conif   Agro    NotFP
    /* Broadleaves   */ {0.300, 0.375, 0.250, 0.275, 0.250, 0.250},
    /* Shrubs        */ {0.375, 0.375, 0.350, 0.400, 0.300, 0.375},
    /* Grassland     */ {0.450, 0.475, 0.475, 0.475, 0.375, 0.475},
    /* Conifers      */ {0.225, 0.325, 0.250, 0.350, 0.200, 0.350},
    /* Agro-forestry */ {0.250, 0.250, 0.300, 0.475, 0.350, 0.250},
    /* Not fire-prone*/ {0.075, 0.100, 0.075, 0.275, 0.075, 0.075},
};

// PROPAGATOR Table 1 nominal spread velocity v_n of the BURNING cell, in m/min
// (the paper's units). Verified against the paper; the CIMA reference code
// uses 140 for Broadleaves and treats these as m/h, which disagrees with it.
constexpr double kNominalSpeed[kNumBurnableTerrains] = {
    100.0, 140.0, 120.0, 200.0, 120.0, 60.0,
};

constexpr const char *kNames[kNumTerrains] = {
    "Broadleaves",         "Shrubs",        "Grassland",
    "Fire-prone conifers", "Agro-forestry", "Not fire-prone forest",
    "Non-vegetated",
};

int to_index(Terrain t) { return static_cast<int>(t); }

} // namespace

bool is_burnable(Terrain t) {
  return to_index(t) >= 0 && to_index(t) < kNumBurnableTerrains;
}

std::optional<Terrain> terrain_from_id(int id) {
  if (id < 0 || id >= kNumTerrains)
    return std::nullopt;
  return static_cast<Terrain>(id);
}

const char *terrain_name(Terrain t) {
  const int i = to_index(t);
  return (i >= 0 && i < kNumTerrains) ? kNames[i] : "Unknown";
}

double nominal_probability(Terrain source, Terrain target) {
  if (!is_burnable(source) || !is_burnable(target))
    return 0.0;
  return kSpreadProbability[to_index(target)][to_index(source)];
}

double nominal_speed_m_per_min(Terrain source) {
  if (!is_burnable(source))
    return 0.0;
  return kNominalSpeed[to_index(source)];
}

} // namespace wildfire
