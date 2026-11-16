#pragma once

#include <cstddef>
#include <cstdint>

#include "services/adsb_client.h"

namespace ui::traffic {

// Radar geometry (left 240x240 of the 320x240 screen).
constexpr int kCx = 120;
constexpr int kCy = 120;
constexpr int kOuterR = 106;        // outermost grid ring
constexpr int kRimR = 115;          // beyond-ring bearing dots
constexpr int kInsideInsetPx = 6;   // symbol must stay this far inside the ring

struct Target {
  services::adsb::Aircraft ac;
  float lat, lon;         // dead-reckoned position
  float dist_km;
  float brg_deg;          // bearing from radar centre
  int16_t sx, sy;         // screen position (valid when inside)
  bool inside;            // within the outer ring
  uint8_t trail_idx;      // 255 = none
};

/** Refresh from the feed (snapshot + dead-reckoning + filters). */
void update();

size_t count();
/** Targets sorted by distance (nearest first). */
const Target& at(size_t i);
/** Feed timestamp of the current data. */
unsigned long feedMs();

// Selection (by hex; survives refreshes)
void select(const char* hex);
void clearSelection();
/** Index of the selected target, or -1. */
int selectedIndex();
const char* selectedHex();
/** Nearest target to a screen point within `radius_px`, or -1. */
int hitTest(int x, int y, int radius_px);

/** Count of targets within the proximity alert radius (0 if disabled). */
size_t alertCount();
/** True when a target newly entered the alert radius since the last call. */
bool consumeNewAlert();

// Projection helpers (use current settings centre and range)
float pxPerKm();
void offsetKm(float lat, float lon, float* dx, float* dy);
void toScreen(float lat, float lon, int* x, int* y);

/** Closest point of approach to the radar centre on the current track/speed. */
struct Cpa {
  bool valid;      // still approaching (closest point ahead)
  float dist_km;
  float brg_deg;   // bearing from the centre to the closest point
  float t_s;       // seconds until then
  int32_t alt_ft;  // extrapolated altitude at that time
};
Cpa closestApproach(const Target& t);

// Trails
constexpr size_t kTrailLen = 12;
struct Trail {
  char hex[7];
  uint8_t n;
  uint8_t head;
  unsigned long last_seen;
  float lat[kTrailLen];
  float lon[kTrailLen];
};
const Trail* trail(uint8_t idx);

// Runway overlay cache (rebuilt when range / centre / airports change)
struct Segment {
  int16_t x0, y0, x1, y1;
  uint8_t kind;  // 0 built-in, 1 custom
};
struct Label {
  char text[8];
  int16_t x, y;
  uint8_t kind;
};
void rebuildOverlay();
size_t segmentCount();
const Segment& segment(size_t i);
size_t labelCount();
const Label& label(size_t i);

}  // namespace ui::traffic
