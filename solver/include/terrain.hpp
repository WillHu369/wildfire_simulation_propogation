#pragma once

#include <cstdint>
#include <optional>

namespace wildfire {

// Terrain IDs used in input files [OURS]. IDs 0-5 follow the column order of
// PPR Table 1 (Trucchia et al. 2020), so they index the probability table
// directly. NonVegetated (roads, water, bare ground) never burns.
enum class Terrain : std::uint8_t {
  Broadleaves = 0,
  Shrubs = 1,
  Grassland = 2,
  FireProneConifers = 3,
  AgroForestry = 4,
  NotFireProneForest = 5,
  NonVegetated = 6,
};

constexpr int kNumBurnableTerrains = 6;
constexpr int kNumTerrains = 7;

// In:  t (Terrain)
// Out: bool, true for IDs 0-5
// Why: skip barrier cells during spread; reject ignitions on barriers.
bool is_burnable(Terrain t);

// In:  id (int) read from terrain.csv
// Out: optional<Terrain>, empty if id is not 0-6
// Why: validate input files before simulating.
std::optional<Terrain> terrain_from_id(int id);

// In:  t (Terrain)
// Out: const char*, e.g. "Grassland"
// Why: readable error messages.
const char* terrain_name(Terrain t);

// In:  source, target (Terrain): burning cell and its neighbor
// Out: double in [0, 1], p_n from PPR Table 1 (0 if either can't burn)
// Why: base chance of spread before wind/slope/moisture (Eq. 1).
double nominal_probability(Terrain source, Terrain target);

// In:  source (Terrain): the burning cell
// Out: double, nominal spread speed v_n in m/min (0 if it can't burn)
// Why: base speed for the travel-time calculation (Eq. 3).
double nominal_speed_m_per_min(Terrain source);

}  // namespace wildfire
