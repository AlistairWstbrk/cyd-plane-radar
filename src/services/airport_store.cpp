#include "services/airport_store.h"

#include <Arduino.h>
#include <LittleFS.h>

#include <cctype>
#include <cmath>
#include <cstring>

namespace services::airports {
namespace {

constexpr char kPath[] = "/airports.json";
Airport s_list[kMaxCustomAirports];
size_t s_count = 0;
uint32_t s_rev = 1;
bool s_fs_ok = false;

bool validCoord(float lat, float lon) {
  return std::isfinite(lat) && std::isfinite(lon) && fabsf(lat) <= 90.0f &&
         fabsf(lon) <= 180.0f;
}

bool save() {
  if (!s_fs_ok) return false;
  JsonDocument doc;
  JsonArray arr = doc["airports"].to<JsonArray>();
  for (size_t i = 0; i < s_count; ++i) {
    toJson(s_list[i], arr.add<JsonObject>());
  }
  File f = LittleFS.open(kPath, "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}

void load() {
  s_count = 0;
  if (!s_fs_ok || !LittleFS.exists(kPath)) return;
  File f = LittleFS.open(kPath, "r");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f)) {
    f.close();
    return;
  }
  f.close();
  for (JsonObjectConst o : doc["airports"].as<JsonArrayConst>()) {
    if (s_count >= kMaxCustomAirports) break;
    const char* err = nullptr;
    if (fromJson(o, &s_list[s_count], &err)) ++s_count;
  }
}

}  // namespace

void init() {
  s_fs_ok = LittleFS.begin(true);
  if (!s_fs_ok) Serial.println("LittleFS mount failed - custom airports disabled");
  load();
  Serial.printf("Custom airports: %u\n", static_cast<unsigned>(s_count));
}

size_t count() { return s_count; }
const Airport& at(size_t i) { return s_list[i]; }
uint32_t revision() { return s_rev; }

int find(const char* ident) {
  for (size_t i = 0; i < s_count; ++i) {
    if (strcasecmp(s_list[i].ident, ident) == 0) return static_cast<int>(i);
  }
  return -1;
}

bool upsert(const Airport& ap) {
  int idx = find(ap.ident);
  if (idx < 0) {
    if (s_count >= kMaxCustomAirports) return false;
    idx = static_cast<int>(s_count++);
  }
  s_list[idx] = ap;
  ++s_rev;
  return save();
}

bool remove(const char* ident) {
  const int idx = find(ident);
  if (idx < 0) return false;
  for (size_t i = idx; i + 1 < s_count; ++i) s_list[i] = s_list[i + 1];
  --s_count;
  ++s_rev;
  return save();
}

bool normaliseIdent(const char* in, char* out, size_t out_len) {
  size_t n = 0;
  if (in == nullptr || out_len == 0) return false;
  for (const char* p = in; *p && n + 1 < out_len && n < 7; ++p) {
    const char c = static_cast<char>(toupper(static_cast<unsigned char>(*p)));
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') out[n++] = c;
  }
  out[n] = '\0';
  return n > 0;
}

void toJson(const Airport& ap, JsonObject o) {
  o["ident"] = ap.ident;
  o["name"] = ap.name;
  o["lat"] = serialized(String(ap.lat, 6));
  o["lon"] = serialized(String(ap.lon, 6));
  o["label"] = ap.show_label;
  JsonArray rw = o["runways"].to<JsonArray>();
  for (uint8_t i = 0; i < ap.runway_count; ++i) {
    const Runway& r = ap.runways[i];
    JsonObject j = rw.add<JsonObject>();
    j["name"] = r.name;
    j["le_lat"] = serialized(String(r.le_lat, 6));
    j["le_lon"] = serialized(String(r.le_lon, 6));
    j["he_lat"] = serialized(String(r.he_lat, 6));
    j["he_lon"] = serialized(String(r.he_lon, 6));
  }
}

bool fromJson(JsonObjectConst o, Airport* ap, const char** err) {
  memset(ap, 0, sizeof(*ap));
  if (!normaliseIdent(o["ident"] | "", ap->ident, sizeof(ap->ident))) {
    *err = "ident required (letters/digits)";
    return false;
  }
  strncpy(ap->name, o["name"] | "", sizeof(ap->name) - 1);
  ap->show_label = o["label"] | true;
  JsonArrayConst rw = o["runways"].as<JsonArrayConst>();
  float sum_lat = 0, sum_lon = 0;
  int pts = 0;
  for (JsonObjectConst r : rw) {
    if (ap->runway_count >= kMaxRunways) break;
    Runway& out = ap->runways[ap->runway_count];
    out.le_lat = r["le_lat"] | NAN;
    out.le_lon = r["le_lon"] | NAN;
    out.he_lat = r["he_lat"] | NAN;
    out.he_lon = r["he_lon"] | NAN;
    if (!validCoord(out.le_lat, out.le_lon) || !validCoord(out.he_lat, out.he_lon)) {
      *err = "runway coordinates invalid";
      return false;
    }
    strncpy(out.name, r["name"] | "", sizeof(out.name) - 1);
    sum_lat += out.le_lat + out.he_lat;
    sum_lon += out.le_lon + out.he_lon;
    pts += 2;
    ++ap->runway_count;
  }
  ap->lat = o["lat"] | NAN;
  ap->lon = o["lon"] | NAN;
  if (!validCoord(ap->lat, ap->lon)) {
    if (pts == 0) {
      *err = "airport position or at least one runway required";
      return false;
    }
    ap->lat = sum_lat / pts;
    ap->lon = sum_lon / pts;
  }
  return true;
}

}  // namespace services::airports
