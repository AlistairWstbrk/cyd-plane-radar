#pragma once

#include <cstddef>
#include <cstdint>

#include "data/airport_index.h"

/**
 * Offline airport search over the built-in index (data/airport_index):
 * name, city, region, ICAO/GPS ident or IATA code. Several words narrow
 * the result ("st pete", "paris orly"). An empty query lists the airports
 * nearest to the given position.
 */
namespace services::search {

using Entry = data::airport_index::Entry;

struct Hit {
  uint16_t idx;
  int16_t score;
  float dist_km;
};

constexpr int kMaxHits = 40;

/** Run a search. Returns hits written (<= max); `total` gets every match. */
int run(const char* query, float lat, float lon, Hit* out, int max, int* total);

const Entry& entry(uint16_t idx);
size_t count();
inline const char* name(const Entry& e) { return e.text; }
const char* city(const Entry& e);
const char* region(const Entry& e);
float lat(const Entry& e);
float lon(const Entry& e);
/** Airport nearest to a position (the radar's "home" airport). */
bool nearest(float lat, float lon, uint16_t* idx, float* km);
/** Exact ident lookup (binary search). */
bool findIdent(const char* ident, uint16_t* idx);
/** Approximate distance in km (equirectangular; fine for sorting and display). */
float distanceKm(float lat0, float lon0, float lat1, float lon1);

}  // namespace services::search
