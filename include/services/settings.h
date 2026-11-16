#pragma once

#include <cstddef>
#include <cstdint>

namespace settings {

enum DistUnits : uint8_t { kKm = 0, kMi = 1, kNm = 2 };
enum AltUnits : uint8_t { kFt = 0, kM = 1 };
enum SpeedUnits : uint8_t { kKt = 0, kKmh = 1, kMph = 2 };
enum LabelMode : uint8_t { kLabelsOff = 0, kLabelsCallsign = 1, kLabelsFull = 2 };
enum Theme : uint8_t { kThemeClassic = 0, kThemeAmber = 1, kThemeIce = 2, kThemeNight = 3, kThemeCount = 4 };

/** All user settings. Persisted as one NVS blob. */
struct Settings {
  uint16_t version;
  double lat;
  double lon;
  uint8_t range_idx;
  uint8_t dist_units;
  uint8_t alt_units;
  uint8_t speed_units;
  uint8_t label_mode;
  uint8_t theme;
  uint8_t brightness;      // 5..100 %
  uint8_t alert_km;        // 0 = off
  uint8_t fetch_s;         // ADS-B poll interval (s)
  uint16_t max_alt_kft;    // hide aircraft above (thousand ft), 0 = off
  bool show_runways;
  bool show_trails;
  bool show_ground;
  bool alt_colors;
  bool sweep;
  bool reserved_a;         // unused, keeps the stored layout
  bool alert_led;
  bool reserved_b;         // unused, keeps the stored layout
  bool status_led;
  bool clock_24h;
  bool flip;
  bool invert;
  char tz[48];
  // v4 (appended; older blobs are migrated field-for-field up to `tz`)
  bool night_mode;         // dim + night theme between night_start and night_end
  uint8_t night_start_h;   // local hour 0..23
  uint8_t night_end_h;
  uint8_t night_brightness;  // 5..100 %
  bool night_theme;        // switch to the red Night theme
};

/** Night presets cycled from the touch menu (start, end). */
constexpr uint8_t kNightPresets[][2] = {{21, 7}, {22, 7}, {23, 6}, {22, 6}, {0, 6}};
constexpr size_t kNightPresetCount = sizeof(kNightPresets) / sizeof(kNightPresets[0]);

struct RangePreset {
  float ring3_km;  // distance at ring 3 (3/4 of outer radius)
  float outer_km;
};

constexpr float kRing3ToOuter = 4.0f / 3.0f;
constexpr RangePreset kRangePresets[] = {
    {2.0f, 2.0f * kRing3ToOuter},   {5.0f, 5.0f * kRing3ToOuter},
    {10.0f, 10.0f * kRing3ToOuter}, {15.0f, 15.0f * kRing3ToOuter},
    {25.0f, 25.0f * kRing3ToOuter}, {40.0f, 40.0f * kRing3ToOuter},
    {60.0f, 60.0f * kRing3ToOuter},
};
constexpr size_t kRangeCount = sizeof(kRangePresets) / sizeof(kRangePresets[0]);
constexpr uint8_t kDefaultRangeIdx = 2;  // 10 km, as the original

void init();
Settings& get();
/** Persist the current settings and flag consumers (renderer) to refresh. */
void save();
/** Restore defaults (keeps touch calibration). */
void resetToDefaults();
/** True once after any change; used by the UI to rebuild caches. */
bool consumeChanged();
void markChanged();

const RangePreset& range();
void rangeNext();
void rangePrev();
/** Fetch radius so beyond-ring rim dots have data. */
float fetchRadiusKm();

bool setLocation(double lat, double lon);
bool setLocationFromStrings(const char* lat, const char* lon);

// Touch calibration (separate key, survives resets)
bool loadTouchCal(uint16_t out[8]);
void saveTouchCal(const uint16_t cal[8]);
void clearTouchCal();

// Formatting helpers
void formatDistance(char* buf, size_t len, float km, bool short_form = false);
void formatRangeLabel(char* buf, size_t len, float ring3_km);
void formatAltitude(char* buf, size_t len, int32_t alt_ft, bool on_ground, bool valid);
void formatSpeed(char* buf, size_t len, float knots);
const char* distUnitName();
const char* themeName(uint8_t theme);

}  // namespace settings
