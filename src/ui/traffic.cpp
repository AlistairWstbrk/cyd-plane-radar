#include "ui/traffic.h"

#include <Arduino.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "data/large_airports.h"
#include "services/airport_store.h"
#include "services/settings.h"

namespace ui::traffic {
namespace {

constexpr float kKmPerDeg = 111.0f;
constexpr float kDegToRad = 0.01745329252f;
constexpr size_t kMaxTrails = services::adsb::kMaxAircraft + 8;
constexpr unsigned long kTrailExpireMs = 90000;
constexpr size_t kMaxSegments = 160;
constexpr size_t kMaxLabels = 32;

services::adsb::Aircraft s_snap[services::adsb::kMaxAircraft];
Target s_targets[services::adsb::kMaxAircraft];
size_t s_count = 0;
unsigned long s_feed_ms = 0;
unsigned long s_trail_feed_ms = 0;
char s_sel_hex[7] = "";
int s_sel_idx = -1;

Trail s_trails[kMaxTrails];

Segment s_segments[kMaxSegments];
size_t s_seg_count = 0;
Label s_labels[kMaxLabels];
size_t s_label_count = 0;

char s_alert_hex[16][7];
size_t s_alert_n = 0;
size_t s_alert_count = 0;
bool s_new_alert = false;

float cosCenter() {
  return cosf(static_cast<float>(settings::get().lat) * kDegToRad);
}

void extrapolate(const services::adsb::Aircraft& p, unsigned long base_ms, float* lat,
                 float* lon) {
  *lat = p.lat;
  *lon = p.lon;
  if (base_ms == 0 || p.gs_knots <= 0.0f || p.on_ground) return;
  unsigned long elapsed = (millis() - base_ms) + p.pos_age_ms;
  if (elapsed > 60000) elapsed = 60000;  // don't fling stale targets
  const float dist_km = p.gs_knots * 1.852f * (elapsed / 3600000.0f);
  const float rad = p.track_deg * kDegToRad;
  *lat = p.lat + dist_km * cosf(rad) / kKmPerDeg;
  *lon = p.lon + dist_km * sinf(rad) / (kKmPerDeg * cosf(p.lat * kDegToRad));
}

Trail* findTrail(const char* hex, bool create, unsigned long now) {
  Trail* free_slot = nullptr;
  Trail* oldest = &s_trails[0];
  for (auto& t : s_trails) {
    if (t.hex[0] && strcmp(t.hex, hex) == 0) return &t;
    if (!t.hex[0] && free_slot == nullptr) free_slot = &t;
    if (t.last_seen < oldest->last_seen) oldest = &t;
  }
  if (!create) return nullptr;
  Trail* t = free_slot ? free_slot : oldest;
  memset(t, 0, sizeof(*t));
  strncpy(t->hex, hex, sizeof(t->hex) - 1);
  t->last_seen = now;
  return t;
}

void updateTrails(unsigned long now) {
  for (size_t i = 0; i < s_count; ++i) {
    const auto& ac = s_targets[i].ac;
    if (!ac.hex[0]) continue;
    Trail* t = findTrail(ac.hex, true, now);
    t->last_seen = now;
    const uint8_t last = (t->head + kTrailLen - 1) % kTrailLen;
    if (t->n > 0 && fabsf(t->lat[last] - ac.lat) < 1e-5f && fabsf(t->lon[last] - ac.lon) < 1e-5f)
      continue;
    t->lat[t->head] = ac.lat;
    t->lon[t->head] = ac.lon;
    t->head = (t->head + 1) % kTrailLen;
    if (t->n < kTrailLen) ++t->n;
  }
  for (auto& t : s_trails) {
    if (t.hex[0] && now - t.last_seen > kTrailExpireMs) t.hex[0] = '\0';
  }
}

void updateAlerts() {
  const auto& cfg = settings::get();
  s_alert_count = 0;
  if (cfg.alert_km == 0) {
    s_alert_n = 0;
    return;
  }
  char now_hex[16][7];
  size_t now_n = 0;
  for (size_t i = 0; i < s_count; ++i) {
    const Target& t = s_targets[i];
    if (t.dist_km > cfg.alert_km || t.ac.on_ground) continue;
    ++s_alert_count;
    if (now_n < 16) {
      strncpy(now_hex[now_n], t.ac.hex, 7);
      bool seen = false;
      for (size_t k = 0; k < s_alert_n; ++k) {
        if (strcmp(s_alert_hex[k], t.ac.hex) == 0) seen = true;
      }
      if (!seen) s_new_alert = true;
      ++now_n;
    }
  }
  memcpy(s_alert_hex, now_hex, sizeof(now_hex));
  s_alert_n = now_n;
}

bool clipToCircle(float& x0, float& y0, float& x1, float& y1, float r) {
  const float cx = kCx, cy = kCy;
  const float dx = x1 - x0, dy = y1 - y0;
  const float fx = x0 - cx, fy = y0 - cy;
  const float a = dx * dx + dy * dy;
  const float c = fx * fx + fy * fy - r * r;
  if (a < 1e-6f) return c <= 0;
  const float b = 2 * (fx * dx + fy * dy);
  const float disc = b * b - 4 * a * c;
  if (disc < 0) return false;
  const float sq = sqrtf(disc);
  float t0 = (-b - sq) / (2 * a);
  float t1 = (-b + sq) / (2 * a);
  if (t1 < 0 || t0 > 1) return false;
  if (t0 < 0) t0 = 0;
  if (t1 > 1) t1 = 1;
  const float nx0 = x0 + dx * t0, ny0 = y0 + dy * t0;
  const float nx1 = x0 + dx * t1, ny1 = y0 + dy * t1;
  x0 = nx0; y0 = ny0; x1 = nx1; y1 = ny1;
  return true;
}

void toScreenF(float lat, float lon, float* x, float* y) {
  float dx, dy;
  offsetKm(lat, lon, &dx, &dy);
  const float k = pxPerKm();
  *x = kCx + dx * k;
  *y = kCy - dy * k;
}

bool addSegment(float la0, float lo0, float la1, float lo1, uint8_t kind) {
  if (s_seg_count >= kMaxSegments) return false;
  float x0, y0, x1, y1;
  toScreenF(la0, lo0, &x0, &y0);
  toScreenF(la1, lo1, &x1, &y1);
  if (!clipToCircle(x0, y0, x1, y1, kOuterR)) return false;
  Segment& s = s_segments[s_seg_count++];
  s.x0 = static_cast<int16_t>(lroundf(x0));
  s.y0 = static_cast<int16_t>(lroundf(y0));
  s.x1 = static_cast<int16_t>(lroundf(x1));
  s.y1 = static_cast<int16_t>(lroundf(y1));
  // Very short segments (zoomed far out) still show as a 2 px mark.
  if (s.x0 == s.x1 && s.y0 == s.y1) s.x1 += 1;
  s.kind = kind;
  return true;
}

void addLabel(const char* text, float lat, float lon, uint8_t kind) {
  if (s_label_count >= kMaxLabels) return;
  for (size_t i = 0; i < s_label_count; ++i) {
    if (strcmp(s_labels[i].text, text) == 0) return;
  }
  float x, y;
  toScreenF(lat, lon, &x, &y);
  float dx = x - kCx, dy = y - kCy;
  float d = sqrtf(dx * dx + dy * dy);
  if (d > kOuterR - 4) {  // keep the label inside the ring
    x = kCx + dx / d * (kOuterR - 4);
    y = kCy + dy / d * (kOuterR - 4);
    dx = x - kCx; dy = y - kCy; d = kOuterR - 4;
  }
  // Nudge outward from the centre so it doesn't sit on the centre dot.
  if (d > 1) {
    x += dx / d * 3;
    y += dy / d * 3;
  } else {
    y -= 3;
  }
  Label& l = s_labels[s_label_count++];
  strncpy(l.text, text, sizeof(l.text) - 1);
  l.text[sizeof(l.text) - 1] = '\0';
  l.x = static_cast<int16_t>(x);
  l.y = static_cast<int16_t>(y);
  l.kind = kind;
}

}  // namespace

float pxPerKm() { return static_cast<float>(kOuterR) / settings::range().outer_km; }

void offsetKm(float lat, float lon, float* dx, float* dy) {
  const auto& cfg = settings::get();
  *dx = static_cast<float>(lon - cfg.lon) * kKmPerDeg * cosCenter();
  *dy = static_cast<float>(lat - cfg.lat) * kKmPerDeg;
}

Cpa closestApproach(const Target& t) {
  Cpa r{};
  float px, py;
  offsetKm(t.lat, t.lon, &px, &py);
  const float v = t.ac.gs_knots * 1.852f / 3600.0f;  // km/s
  if (v < 0.01f || t.ac.on_ground) return r;
  const float tr = t.ac.track_deg * 0.01745329f;
  const float vx = v * sinf(tr), vy = v * cosf(tr);
  const float ts = -(px * vx + py * vy) / (v * v);
  if (ts <= 0) return r;
  const float cx = px + vx * ts, cy = py + vy * ts;
  r.valid = true;
  r.t_s = ts;
  r.dist_km = sqrtf(cx * cx + cy * cy);
  r.brg_deg = fmodf(atan2f(cx, cy) * 57.2957795f + 360.0f, 360.0f);
  r.alt_ft = t.ac.alt_ft + static_cast<int32_t>(t.ac.vrate_fpm * ts / 60.0f);
  if (r.alt_ft < 0) r.alt_ft = 0;
  return r;
}

void toScreen(float lat, float lon, int* x, int* y) {
  float fx, fy;
  toScreenF(lat, lon, &fx, &fy);
  *x = static_cast<int>(lroundf(fx));
  *y = static_cast<int>(lroundf(fy));
}

void update() {
  unsigned long feed_ms = 0;
  const size_t n = services::adsb::snapshotAircraft(s_snap, services::adsb::kMaxAircraft, &feed_ms);
  const auto& cfg = settings::get();
  const float inner_km = settings::range().outer_km * (kOuterR - kInsideInsetPx) / kOuterR;
  const float k = pxPerKm();

  s_count = 0;
  for (size_t i = 0; i < n; ++i) {
    const auto& ac = s_snap[i];
    if (ac.on_ground && !cfg.show_ground) continue;
    if (cfg.max_alt_kft > 0 && ac.alt_valid && ac.alt_ft > cfg.max_alt_kft * 1000L) continue;
    Target& t = s_targets[s_count++];
    t.ac = ac;
    extrapolate(ac, feed_ms, &t.lat, &t.lon);
    float dx, dy;
    offsetKm(t.lat, t.lon, &dx, &dy);
    t.dist_km = sqrtf(dx * dx + dy * dy);
    float b = atan2f(dx, dy) / kDegToRad;
    if (b < 0) b += 360.0f;
    t.brg_deg = b;
    t.inside = t.dist_km <= inner_km;
    t.sx = static_cast<int16_t>(lroundf(kCx + dx * k));
    t.sy = static_cast<int16_t>(lroundf(kCy - dy * k));
    t.trail_idx = 255;
  }
  std::sort(s_targets, s_targets + s_count,
            [](const Target& a, const Target& b) { return a.dist_km < b.dist_km; });

  const unsigned long now = millis();
  if (feed_ms != s_trail_feed_ms) {
    s_trail_feed_ms = feed_ms;
    updateTrails(now);
    updateAlerts();
  }
  s_feed_ms = feed_ms;
  s_sel_idx = -1;
  for (size_t i = 0; i < s_count; ++i) {
    Target& t = s_targets[i];
    if (s_sel_hex[0] && strcmp(t.ac.hex, s_sel_hex) == 0) s_sel_idx = static_cast<int>(i);
    if (cfg.show_trails) {
      for (size_t j = 0; j < kMaxTrails; ++j) {
        if (s_trails[j].hex[0] && strcmp(s_trails[j].hex, t.ac.hex) == 0) {
          t.trail_idx = static_cast<uint8_t>(j);
          break;
        }
      }
    }
  }
}

size_t count() { return s_count; }
const Target& at(size_t i) { return s_targets[i]; }
unsigned long feedMs() { return s_feed_ms; }

void select(const char* hex) {
  strncpy(s_sel_hex, hex, sizeof(s_sel_hex) - 1);
  s_sel_hex[sizeof(s_sel_hex) - 1] = '\0';
  s_sel_idx = -1;
  for (size_t i = 0; i < s_count; ++i) {
    if (strcmp(s_targets[i].ac.hex, s_sel_hex) == 0) s_sel_idx = static_cast<int>(i);
  }
}

void clearSelection() {
  s_sel_hex[0] = '\0';
  s_sel_idx = -1;
}

int selectedIndex() { return s_sel_idx; }
const char* selectedHex() { return s_sel_hex; }

int hitTest(int x, int y, int radius_px) {
  int best = -1;
  int best_d = radius_px * radius_px + 1;
  for (size_t i = 0; i < s_count; ++i) {
    const Target& t = s_targets[i];
    if (!t.inside) continue;
    const int dx = t.sx - x, dy = t.sy - y;
    const int d = dx * dx + dy * dy;
    if (d < best_d) {
      best_d = d;
      best = static_cast<int>(i);
    }
  }
  return best;
}

size_t alertCount() { return s_alert_count; }

bool consumeNewAlert() {
  const bool v = s_new_alert;
  s_new_alert = false;
  return v;
}

const Trail* trail(uint8_t idx) { return idx < kMaxTrails ? &s_trails[idx] : nullptr; }

void rebuildOverlay() {
  s_seg_count = 0;
  s_label_count = 0;
  const auto& cfg = settings::get();
  if (!cfg.show_runways) return;
  const float radius_km = settings::range().outer_km * 1.4f;
  namespace la = data::large_airports;

  // Custom airports first so they win the label budget.
  for (size_t i = 0; i < services::airports::count(); ++i) {
    const auto& ap = services::airports::at(i);
    float dx, dy;
    offsetKm(ap.lat, ap.lon, &dx, &dy);
    if (sqrtf(dx * dx + dy * dy) > radius_km + 5.0f) continue;
    bool any = false;
    for (uint8_t r = 0; r < ap.runway_count; ++r) {
      const auto& rw = ap.runways[r];
      any |= addSegment(rw.le_lat, rw.le_lon, rw.he_lat, rw.he_lon, 1);
    }
    if (ap.runway_count == 0) {
      float x, y;
      toScreenF(ap.lat, ap.lon, &x, &y);
      const float ddx = x - kCx, ddy = y - kCy;
      any = ddx * ddx + ddy * ddy <= kOuterR * kOuterR;
    }
    if (any && ap.show_label) addLabel(ap.ident, ap.lat, ap.lon, 1);
  }

  static uint8_t in_range[(la::kAirportCount + 7) / 8];
  static uint8_t checked[(la::kAirportCount + 7) / 8];
  memset(in_range, 0, sizeof(in_range));
  memset(checked, 0, sizeof(checked));
  for (size_t i = 0; i < la::kRunwayCount; ++i) {
    const auto& rw = la::kRunways[i];
    const uint16_t a = rw.airport_idx;
    const auto& ap = la::kAirports[a];
    if (services::airports::find(ap.ident) >= 0) continue;  // custom overrides built-in
    if (!(checked[a >> 3] & (1 << (a & 7)))) {
      checked[a >> 3] |= 1 << (a & 7);
      float dx, dy;
      offsetKm(ap.lat_e7 * 1e-7f, ap.lon_e7 * 1e-7f, &dx, &dy);
      if (sqrtf(dx * dx + dy * dy) <= radius_km) in_range[a >> 3] |= 1 << (a & 7);
    }
    if (!(in_range[a >> 3] & (1 << (a & 7)))) continue;
    if (addSegment(rw.le_lat_e7 * 1e-7f, rw.le_lon_e7 * 1e-7f, rw.he_lat_e7 * 1e-7f,
                   rw.he_lon_e7 * 1e-7f, 0)) {
      addLabel(ap.ident, ap.lat_e7 * 1e-7f, ap.lon_e7 * 1e-7f, 0);
    }
  }
}

size_t segmentCount() { return s_seg_count; }
const Segment& segment(size_t i) { return s_segments[i]; }
size_t labelCount() { return s_label_count; }
const Label& label(size_t i) { return s_labels[i]; }

}  // namespace ui::traffic
