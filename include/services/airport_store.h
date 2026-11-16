#pragma once

#include <cstddef>
#include <cstdint>

#include <ArduinoJson.h>

namespace services::airports {

constexpr size_t kMaxCustomAirports = 24;
constexpr size_t kMaxRunways = 8;

struct Runway {
  float le_lat, le_lon, he_lat, he_lon;
  char name[8];  // e.g. "09L/27R"
};

struct Airport {
  char ident[8];
  char name[40];
  float lat, lon;
  uint8_t runway_count;
  bool show_label;
  Runway runways[kMaxRunways];
};

/** Mount LittleFS and load /airports.json. */
void init();
size_t count();
const Airport& at(size_t i);
int find(const char* ident);
/** Add or replace (matched by ident). Saves to flash. */
bool upsert(const Airport& ap);
bool remove(const char* ident);
/** Increments on every change (renderer uses it to rebuild caches). */
uint32_t revision();

/** Normalise user input to an ident (A-Z, 0-9, '-'; max 7). False if empty. */
bool normaliseIdent(const char* in, char* out, size_t out_len);

void toJson(const Airport& ap, JsonObject obj);
bool fromJson(JsonObjectConst obj, Airport* out, const char** err);

}  // namespace services::airports
