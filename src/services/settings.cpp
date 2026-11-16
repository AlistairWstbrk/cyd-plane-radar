#include "services/settings.h"

#include <cstddef>

#include <Arduino.h>
#include <Preferences.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config.h"

namespace settings {
namespace {

constexpr char kNs[] = "cydradar";
constexpr char kKeyBlob[] = "cfg";
constexpr char kKeyTouch[] = "tcal";
constexpr uint16_t kVersion = 4;
// Bytes of the v3 layout that carry data (everything up to and including tz).
constexpr size_t kV3DataEnd = offsetof(Settings, tz) + sizeof(Settings::tz);
constexpr float kKmPerMile = 1.609344f;
constexpr float kKmPerNm = 1.852f;

Settings s_cfg;
volatile bool s_changed = true;

void defaults(Settings& c) {
  memset(&c, 0, sizeof(c));
  c.version = kVersion;
  c.lat = config::kDefaultRadarLat;
  c.lon = config::kDefaultRadarLon;
  c.range_idx = kDefaultRangeIdx;
  c.dist_units = kKm;
  c.alt_units = kFt;
  c.speed_units = kKt;
  c.label_mode = kLabelsFull;
  c.theme = kThemeClassic;
  c.brightness = 80;
  c.alert_km = 0;
  c.fetch_s = 5;
  c.max_alt_kft = 0;
  c.show_runways = true;
  c.show_trails = true;
  c.show_ground = false;
  c.alt_colors = false;
  c.sweep = false;
  c.alert_led = true;
  c.status_led = true;
  c.clock_24h = true;
  c.flip = false;
  c.invert = false;
  strncpy(c.tz, config::kDefaultTz, sizeof(c.tz) - 1);
  c.night_mode = false;
  c.night_start_h = 22;
  c.night_end_h = 7;
  c.night_brightness = 10;
  c.night_theme = true;
}

bool validLatLon(double lat, double lon) {
  return std::isfinite(lat) && std::isfinite(lon) && lat >= -90.0 &&
         lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}

void sanitize(Settings& c) {
  if (c.range_idx >= kRangeCount) c.range_idx = kDefaultRangeIdx;
  if (c.dist_units > kNm) c.dist_units = kKm;
  if (c.alt_units > kM) c.alt_units = kFt;
  if (c.speed_units > kMph) c.speed_units = kKt;
  if (c.label_mode > kLabelsFull) c.label_mode = kLabelsFull;
  if (c.theme >= kThemeCount) c.theme = kThemeClassic;
  if (c.brightness < 5 || c.brightness > 100) c.brightness = 80;
  if (c.fetch_s < 3 || c.fetch_s > 60) c.fetch_s = 5;
  if (c.alert_km > 50) c.alert_km = 0;
  if (!validLatLon(c.lat, c.lon)) {
    c.lat = config::kDefaultRadarLat;
    c.lon = config::kDefaultRadarLon;
  }
  c.tz[sizeof(c.tz) - 1] = '\0';
  if (c.tz[0] == '\0') strncpy(c.tz, config::kDefaultTz, sizeof(c.tz) - 1);
  if (c.night_start_h > 23) c.night_start_h = 22;
  if (c.night_end_h > 23) c.night_end_h = 7;
  if (c.night_brightness < 5 || c.night_brightness > 100) c.night_brightness = 10;
}

}  // namespace

void init() {
  defaults(s_cfg);
  Preferences p;
  if (p.begin(kNs, true)) {
    const size_t len = p.getBytesLength(kKeyBlob);
    if (len == sizeof(Settings)) {
      Settings tmp;
      p.getBytes(kKeyBlob, &tmp, sizeof(tmp));
      if (tmp.version == kVersion) s_cfg = tmp;
    } else if (len >= kV3DataEnd && len < sizeof(Settings)) {
      // Older firmware: keep every existing field, new ones get defaults.
      uint8_t buf[sizeof(Settings)];
      p.getBytes(kKeyBlob, buf, len);
      uint16_t ver;
      memcpy(&ver, buf, sizeof(ver));
      if (ver == 3) {
        memcpy(&s_cfg, buf, kV3DataEnd);
        s_cfg.version = kVersion;
        Serial.println("settings: migrated v3 -> v4");
      }
    }
    p.end();
  }
  sanitize(s_cfg);
  s_changed = true;
}

Settings& get() { return s_cfg; }

void save() {
  sanitize(s_cfg);
  Preferences p;
  if (p.begin(kNs, false)) {
    p.putBytes(kKeyBlob, &s_cfg, sizeof(s_cfg));
    p.end();
  }
  s_changed = true;
}

void resetToDefaults() {
  defaults(s_cfg);
  Preferences p;
  if (p.begin(kNs, false)) {
    p.remove(kKeyBlob);
    p.end();
  }
  s_changed = true;
}

bool consumeChanged() {
  if (!s_changed) return false;
  s_changed = false;
  return true;
}

void markChanged() { s_changed = true; }

const RangePreset& range() { return kRangePresets[s_cfg.range_idx]; }

void rangeNext() {
  s_cfg.range_idx = static_cast<uint8_t>((s_cfg.range_idx + 1) % kRangeCount);
  save();
}

void rangePrev() {
  s_cfg.range_idx = static_cast<uint8_t>((s_cfg.range_idx + kRangeCount - 1) % kRangeCount);
  save();
}

float fetchRadiusKm() {
  // Screen corner of the radar half (120 px from centre at 106 px outer ring),
  // with a small margin so rim dots appear before aircraft enter the ring.
  return range().outer_km * 1.25f;
}

bool setLocation(double lat, double lon) {
  if (!validLatLon(lat, lon)) return false;
  s_cfg.lat = lat;
  s_cfg.lon = lon;
  save();
  return true;
}

bool setLocationFromStrings(const char* lat_s, const char* lon_s) {
  if (lat_s == nullptr || lon_s == nullptr || !*lat_s || !*lon_s) return false;
  char* e1 = nullptr;
  char* e2 = nullptr;
  const double lat = strtod(lat_s, &e1);
  const double lon = strtod(lon_s, &e2);
  if (e1 == lat_s || e2 == lon_s) return false;
  while (*e1 == ' ') ++e1;
  while (*e2 == ' ') ++e2;
  if (*e1 != '\0' || *e2 != '\0') return false;
  return setLocation(lat, lon);
}

bool loadTouchCal(uint16_t out[8]) {
  Preferences p;
  if (!p.begin(kNs, true)) return false;
  bool ok = p.getBytesLength(kKeyTouch) == sizeof(uint16_t) * 8;
  if (ok) p.getBytes(kKeyTouch, out, sizeof(uint16_t) * 8);
  p.end();
  return ok;
}

void saveTouchCal(const uint16_t cal[8]) {
  Preferences p;
  if (!p.begin(kNs, false)) return;
  p.putBytes(kKeyTouch, cal, sizeof(uint16_t) * 8);
  p.end();
}

void clearTouchCal() {
  Preferences p;
  if (!p.begin(kNs, false)) return;
  p.remove(kKeyTouch);
  p.end();
}

const char* distUnitName() {
  switch (s_cfg.dist_units) {
    case kMi: return "mi";
    case kNm: return "nm";
    default: return "km";
  }
}

void formatDistance(char* buf, size_t len, float km, bool short_form) {
  float v = km;
  if (s_cfg.dist_units == kMi) v = km / kKmPerMile;
  if (s_cfg.dist_units == kNm) v = km / kKmPerNm;
  if (v < 10.0f && !short_form) {
    snprintf(buf, len, "%.1f%s", v, distUnitName());
  } else {
    snprintf(buf, len, "%d%s", static_cast<int>(lroundf(v)), distUnitName());
  }
}

void formatRangeLabel(char* buf, size_t len, float ring3_km) {
  float v = ring3_km;
  if (s_cfg.dist_units == kMi) v = ring3_km / kKmPerMile;
  if (s_cfg.dist_units == kNm) v = ring3_km / kKmPerNm;
  if (v < 3.0f) {
    snprintf(buf, len, "%.1f%s", v, distUnitName());
  } else {
    snprintf(buf, len, "%d%s", static_cast<int>(lroundf(v)), distUnitName());
  }
}

void formatAltitude(char* buf, size_t len, int32_t alt_ft, bool on_ground, bool valid) {
  if (on_ground) {
    snprintf(buf, len, "GND");
  } else if (!valid) {
    snprintf(buf, len, "---");
  } else if (s_cfg.alt_units == kM) {
    snprintf(buf, len, "%ldm", static_cast<long>(lroundf(alt_ft * 0.3048f)));
  } else if (alt_ft >= 18000) {
    snprintf(buf, len, "FL%03ld", static_cast<long>((alt_ft + 50) / 100));
  } else {
    snprintf(buf, len, "%ldft", static_cast<long>(alt_ft));
  }
}

void formatSpeed(char* buf, size_t len, float knots) {
  switch (s_cfg.speed_units) {
    case kKmh: snprintf(buf, len, "%dkm/h", static_cast<int>(lroundf(knots * 1.852f))); break;
    case kMph: snprintf(buf, len, "%dmph", static_cast<int>(lroundf(knots * 1.15078f))); break;
    default: snprintf(buf, len, "%dkt", static_cast<int>(lroundf(knots))); break;
  }
}

const char* themeName(uint8_t theme) {
  switch (theme) {
    case kThemeAmber: return "Amber";
    case kThemeIce: return "Ice";
    case kThemeNight: return "Night";
    default: return "Classic";
  }
}

}  // namespace settings
