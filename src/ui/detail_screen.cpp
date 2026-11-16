// Aircraft detail: everything known about the selected aircraft plus a
// closest-approach prediction (where and when it passes nearest the radar
// centre if it holds its track and speed).
#include <Arduino.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "services/settings.h"
#include "ui/app.h"
#include "ui/theme.h"
#include "ui/traffic.h"

namespace ui {
namespace {

constexpr int kDialX = 262, kDialY = 112, kDialR = 50;

const char* categoryName(uint8_t cat) {
  switch (cat) {
    case 0x11: return "Light";
    case 0x12: return "Small";
    case 0x13: return "Large";
    case 0x14: return "Large (B757)";
    case 0x15: return "Heavy";
    case 0x16: return "High perf.";
    case 0x17: return "Helicopter";
    case 0x21: return "Glider";
    case 0x22: return "Balloon";
    case 0x23: return "Parachutist";
    case 0x24: return "Ultralight";
    case 0x26: return "Drone";
    case 0x27: return "Space";
    case 0x31: return "Emergency veh.";
    case 0x32: return "Service veh.";
    default: return "";
  }
}

const char* compass8(float deg) {
  static const char* kDir[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  int i = static_cast<int>(lroundf(fmodf(deg + 360.0f, 360.0f) / 45.0f)) % 8;
  return kDir[i];
}

void cell(Canvas& c, int x, int y, const char* label, const char* value, uint16_t col) {
  c.text(label, x, y, pal.text_dim, Font::XS);
  c.text(value, x, y + 12, col, Font::S);
}

void drawDial(Canvas& c, const traffic::Target& t, const traffic::Cpa& cpa) {
  if (!c.rows(kDialY - kDialR - 2, kDialY + kDialR + 12)) return;
  c.circle(kDialX, kDialY, kDialR, pal.grid);
  c.circle(kDialX, kDialY, kDialR / 2, pal.grid);
  c.text("N", kDialX, kDialY - kDialR - 1, pal.label, Font::XS, textdatum_t::bottom_center);
  // Scale: the aircraft's current distance sits on the outer ring.
  const float scale = kDialR / fmaxf(t.dist_km, 0.5f);
  auto pt = [&](float brg, float km, int* x, int* y) {
    const float a = brg * 0.01745329f;
    *x = kDialX + static_cast<int>(lroundf(sinf(a) * km * scale));
    *y = kDialY - static_cast<int>(lroundf(cosf(a) * km * scale));
  };
  int ax, ay;
  pt(t.brg_deg, t.dist_km, &ax, &ay);
  if (cpa.valid) {
    int qx, qy;
    pt(cpa.brg_deg, cpa.dist_km, &qx, &qy);
    // dashed predicted path
    const int n = 10;
    for (int i = 0; i < n; i += 2) {
      c.line(ax + (qx - ax) * i / n, ay + (qy - ay) * i / n, ax + (qx - ax) * (i + 1) / n,
             ay + (qy - ay) * (i + 1) / n, pal.select);
    }
    c.circle(qx, qy, 3, pal.select);
  } else {
    const float tr = t.ac.track_deg * 0.01745329f;
    c.line(ax, ay, ax + static_cast<int>(sinf(tr) * 14), ay - static_cast<int>(cosf(tr) * 14), pal.select);
  }
  c.smoothCircle(kDialX, kDialY, 3, pal.text);
  c.smoothCircle(ax, ay, 4, pal.aircraft);
  char d[16];
  settings::formatDistance(d, sizeof(d), t.dist_km);
  c.text(d, kDialX, kDialY + kDialR + 3, pal.text_dim, Font::XS, textdatum_t::top_center);
}

}  // namespace

void detailOpen() {
  if (traffic::selectedIndex() < 0 && traffic::count() > 0) traffic::select(traffic::at(0).ac.hex);
  if (traffic::selectedIndex() >= 0) navigate(ScreenId::Detail);
}

void detailDraw(Canvas& c, void*) {
  c.fill(pal.bg);
  const int idx = traffic::selectedIndex();
  if (idx < 0) {
    drawHeader(c, "Aircraft", true);
    c.text("Aircraft no longer in range", 160, 120, pal.text_dim, Font::S, textdatum_t::middle_center);
    return;
  }
  const auto& t = traffic::at(idx);
  const auto& ac = t.ac;
  const bool emerg = !strcmp(ac.squawk, "7700") || !strcmp(ac.squawk, "7600") || !strcmp(ac.squawk, "7500");
  char hex[16];
  snprintf(hex, sizeof(hex), "%s%s", ac.military ? "MIL  " : "", ac.hex);
  drawHeader(c, "Aircraft", true, hex);

  c.text(ac.callsign[0] ? ac.callsign : ac.hex, 8, 34, emerg ? pal.alert : pal.text, Font::XL);
  char line[64];
  const char* cat = categoryName(ac.category);
  snprintf(line, sizeof(line), "%s%s%s%s%s", ac.type, ac.reg[0] ? "  " : "", ac.reg, *cat ? "  " : "", cat);
  c.text(line, 8, 72, pal.tag_type, Font::XS);

  char v[24];
  settings::formatAltitude(v, sizeof(v), ac.alt_ft, ac.on_ground, ac.alt_valid);
  cell(c, 8, 90, "ALTITUDE", v, pal.tag_alt);
  if (ac.vrate_fpm > 200) snprintf(v, sizeof(v), "+%d fpm", ac.vrate_fpm);
  else if (ac.vrate_fpm < -200) snprintf(v, sizeof(v), "%d fpm", ac.vrate_fpm);
  else snprintf(v, sizeof(v), "level");
  cell(c, 112, 90, "VERT SPEED", v, pal.text);
  settings::formatSpeed(v, sizeof(v), ac.gs_knots);
  cell(c, 8, 118, "GROUND SPEED", v, pal.text);
  snprintf(v, sizeof(v), "%03d %s", static_cast<int>(lroundf(ac.track_deg)) % 360, compass8(ac.track_deg));
  cell(c, 112, 118, "TRACK", v, pal.text);
  settings::formatDistance(v, sizeof(v), t.dist_km);
  cell(c, 8, 146, "DISTANCE", v, pal.text);
  snprintf(v, sizeof(v), "%03d %s", static_cast<int>(lroundf(t.brg_deg)) % 360, compass8(t.brg_deg));
  cell(c, 112, 146, "BEARING", v, pal.text);
  cell(c, 8, 174, "SQUAWK", ac.squawk[0] ? ac.squawk : "-", emerg ? pal.alert : pal.text);
  snprintf(v, sizeof(v), "%lus ago", static_cast<unsigned long>(ac.pos_age_ms / 1000));
  cell(c, 112, 174, "POSITION", v, pal.text);

  const traffic::Cpa cpa = traffic::closestApproach(t);
  drawDial(c, t, cpa);

  // Closest approach strip
  if (c.rows(202, 240)) {
    c.fillRect(0, 203, 320, 37, pal.panel_bg);
    c.hline(0, 203, 320, pal.panel_line);
    c.text("CLOSEST APPROACH", 8, 207, pal.text_dim, Font::XS);
    if (ac.on_ground) {
      snprintf(line, sizeof(line), "On the ground");
    } else if (!cpa.valid) {
      snprintf(line, sizeof(line), ac.gs_knots < 5 ? "Holding position" : "Now - moving away");
    } else {
      char d[16], a[16];
      settings::formatDistance(d, sizeof(d), cpa.dist_km);
      const int32_t alt = (cpa.alt_ft + 50) / 100 * 100;
      settings::formatAltitude(a, sizeof(a), alt, false, ac.alt_valid);
      char where[24], when[16];
      if (cpa.dist_km < 0.3f) snprintf(where, sizeof(where), "Overhead");
      else snprintf(where, sizeof(where), "%s %s", d, compass8(cpa.brg_deg));
      const int s = static_cast<int>(cpa.t_s);
      if (s >= 3600) snprintf(when, sizeof(when), "%dh%02d", s / 3600, (s / 60) % 60);
      else snprintf(when, sizeof(when), "%d:%02d", s / 60, s % 60);
      snprintf(line, sizeof(line), "%s in %s at %s", where, when, a);
    }
    c.text(line, 8, 220, pal.select, Font::S);
  }
}

void detailTouch(const touch::Event& e) {
  if (e.type != touch::Type::Tap) return;
  if (kBackBtn.hit(e.x, e.y) || e.y > 30) navigate(ScreenId::Radar);
}

}  // namespace ui
