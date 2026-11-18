#pragma once

#include <cstdint>

namespace wildfire {

// In:  x (uint64)
// Out: uint64, a random-looking scramble of x
// Why: building block for spread_draw.
// This is the standard SplitMix64 mixer (Steele, Lea & Flood 2014). Treat it
// as a black box: inputs that differ by 1 give unrelated outputs. The
// constants are part of the published algorithm; do not edit them.
inline std::uint64_t splitmix64(std::uint64_t x) {
  x += 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  return x ^ (x >> 31);
}

// In:  seed (uint64): run seed from the config
//      source_id, target_id (uint64): global flat cell indices
// Out: double, uniform in [0, 1)
// Why: random number for one spread attempt [OURS]. It depends only on its
//      inputs, not on processing order or MPI rank, so serial and parallel
//      runs make identical decisions. Each source -> target edge is tried at
//      most once per run, so no attempt counter is needed.
inline double spread_draw(std::uint64_t seed, std::uint64_t source_id,
                          std::uint64_t target_id) {
  std::uint64_t h = splitmix64(seed);
  h = splitmix64(h ^ source_id);
  h = splitmix64(h ^ target_id);
  return static_cast<double>(h >> 11) * 0x1.0p-53;  // top 53 bits
}

}  // namespace wildfire
