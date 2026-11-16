// Main radar screen: sonar grid on the left (same look and rules as the
// original round-display Plane Radar), info panel and touch buttons on the right.
#include <Arduino.h>
#include <WiFi.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "config.h"
#include "services/adsb_client.h"
#include "services/settings.h"
#include "ui/app.h"
#include "ui/theme.h"
#include "ui/traffic.h"

namespace ui {
namespace {

using traffic::kCx;
using traffic::kCy;
using traffic::kOuterR;
using traffic::kRimR;

constexpr int kPanelX = 240;
constexpr int kPanelW = 80;
constexpr float kDegToRad = 0.01745329252f;

constexpr Button kZoomOut{243, 168, 36, 30};
constexpr Button kZoomIn{282, 168, 36, 30};
constexpr Button kListBtn{243, 204, 36, 32};
constexpr Button kMenuBtn{282, 204, 36, 32};

constexpr int kNoseLen = 9;
constexpr int kTailLen = 4;
constexpr int kTailHalf = 5;

bool isEmergency(const services::adsb::Aircraft& ac) {
  return strcmp(ac.squawk, "7500") == 0 || strcmp(ac.squawk, "7600") == 0 ||
         strcmp(ac.squawk, "7700") == 0;
}

uint16_t symbolColor(const traffic::Target& t) {
  if (settings::get().alt_colors) return altitudeColor(t.ac.alt_ft, t.ac.on_ground);
  return pal.aircraft;
}

int speedVectorPx(float gs) {
  if (gs <= 0) return 0;
  // Fixed screen scale (not tied to zoom), as the original: 60 s at gs,
  // referenced to the 13.3 km outer ring, x0.3.
  const float px = gs * 1.852f * 60.0f / 3600.0f * kOuterR / 13.3f * 0.3f;
  return px < 2 ? 2 : static_cast<int>(px + 0.5f);
}

void clipToRing(int x0, int y0, int* x1, int* y1) {
  const int r2 = kOuterR * kOuterR;
  auto d2 = [](int x, int y) { return (x - kCx) * (x - kCx) + (y - kCy) * (y - kCy); };
  if (d2(*x1, *y1) <= r2) return;
  const int dx = *x1 - x0, dy = *y1 - y0;
  for (float t = 0.95f; t > 0; t -= 0.05f) {
    const int px = x0 + static_cast<int>(dx * t), py = y0 + static_cast<int>(dy * t);
    if (d2(px, py) <= r2) {
      *x1 = px;
      *y1 = py;
      return;
    }
  }
  *x1 = x0;
  *y1 = y0;
}

void drawGrid(Canvas& c) {
  const auto& cfg = settings::get();
  for (int i = 1; i <= 4; ++i) {
    const int r = kOuterR * i / 4;
    c.ring(kCx, kCy, r - 1, r, pal.grid);
  }
  c.wideLine(kCx - kOuterR, kCy, kCx + kOuterR, kCy, 1.0f, pal.grid);
  c.wideLine(kCx, kCy - kOuterR, kCx, kCy + kOuterR, 1.0f, pal.grid);

  // Proximity alert zone (dashed ring).
  if (cfg.alert_km > 0) {
    const float r = cfg.alert_km * traffic::pxPerKm();
    if (r > 4 && r < kOuterR) {
      const uint16_t col = traffic::alertCount() ? pal.alert : blend(pal.bg, pal.alert, 130);
      for (int a = 0; a < 360; a += 15) c.arc(kCx, kCy, r - 1, r, a, a + 8, col);
    }
  }
}

void drawSweep(Canvas& c) {
  const float ang = (millis() % 6000) / 6000.0f * 360.0f;  // bearing
  const float a = ang - 90.0f;                              // LGFX arc angle
  constexpr int kSlices = 8;
  for (int i = kSlices; i >= 1; --i) {
    const uint16_t col = blend(pal.bg, pal.sweep, static_cast<uint8_t>(110 - i * 12));
    c.arc(kCx, kCy, 0, kOuterR - 1, a - i * 4.0f, a - (i - 1) * 4.0f, col);
  }
  const int ex = kCx + static_cast<int>(sinf(ang * kDegToRad) * kOuterR);
  const int ey = kCy - static_cast<int>(cosf(ang * kDegToRad) * kOuterR);
  c.wideLine(kCx, kCy, ex, ey, 0.8f, blend(pal.bg, pal.sweep, 220));
}

void drawOverlay(Canvas& c) {
  for (size_t i = 0; i < traffic::segmentCount(); ++i) {
    const auto& s = traffic::segment(i);
    c.wideLine(s.x0, s.y0, s.x1, s.y1, 1.0f, s.kind ? pal.custom_runway : pal.runway);
  }
  for (size_t i = 0; i < traffic::labelCount(); ++i) {
    const auto& l = traffic::label(i);
    const uint16_t col = l.kind ? pal.custom_runway : pal.runway_label;
    const int tw = c.textWidth(l.text, Font::XS);
    const int th = c.fontHeight(Font::XS);
    c.fillRect(l.x - tw / 2 - 2, l.y - th, tw + 4, th, pal.bg);
    c.text(l.text, l.x, l.y, col, Font::XS, textdatum_t::bottom_center);
  }
}

void drawLabels(Canvas& c) {
  c.text("N", kCx, 0, pal.label, Font::S, textdatum_t::top_center);
  c.text("S", kCx, 240, pal.label, Font::S, textdatum_t::bottom_center);
  c.text("W", 2, kCy, pal.label, Font::S, textdatum_t::middle_left);
  c.text("E", 237, kCy, pal.label, Font::S, textdatum_t::middle_right);
  char lbl[16];
  settings::formatRangeLabel(lbl, sizeof(lbl), settings::range().ring3_km);
  // Range label sits on the east spoke just inside the outer ring, like the original.
  c.text(lbl, kCx + kOuterR - 5, kCy - 3, pal.label, Font::XS, textdatum_t::bottom_right);
  c.smoothCircle(kCx, kCy, 2, pal.center);
}

void drawTrail(Canvas& c, const traffic::Target& t) {
  const traffic::Trail* tr = traffic::trail(t.trail_idx);
  if (tr == nullptr || tr->n == 0) return;
  int px = 0, py = 0;
  bool have = false;
  const uint8_t n = tr->n;
  for (uint8_t k = 0; k < n; ++k) {
    const uint8_t idx = (tr->head + traffic::kTrailLen - n + k) % traffic::kTrailLen;
    int x, y;
    traffic::toScreen(tr->lat[idx], tr->lon[idx], &x, &y);
    const uint16_t col = blend(pal.bg, pal.trail, static_cast<uint8_t>(80 + 175 * (k + 1) / n));
    const int dx = x - kCx, dy = y - kCy;
    const bool in = dx * dx + dy * dy <= kOuterR * kOuterR;
    if (have && in) c.line(px, py, x, y, col);
    if (in) c.fillRect(x - 1, y - 1, 2, 2, col);
    px = x;
    py = y;
    have = in;
  }
  if (have) c.line(px, py, t.sx, t.sy, pal.trail);
}

void drawSymbol(Canvas& c, const traffic::Target& t, bool selected) {
  const auto& ac = t.ac;
  const uint16_t col = symbolColor(t);
  const float hr = ac.nose_deg * kDegToRad;
  const float sh = sinf(hr), ch = cosf(hr);
  const int tipx = t.sx + static_cast<int>(lroundf(sh * kNoseLen));
  const int tipy = t.sy - static_cast<int>(lroundf(ch * kNoseLen));

  // Speed vector from the nose along the ground track, clipped at the ring.
  const int len = speedVectorPx(ac.gs_knots) * (selected ? 2 : 1);
  if (len > 0 && !ac.on_ground) {
    const float tr = ac.track_deg * kDegToRad;
    int ex = tipx + static_cast<int>(lroundf(sinf(tr) * len));
    int ey = tipy - static_cast<int>(lroundf(cosf(tr) * len));
    clipToRing(tipx, tipy, &ex, &ey);
    if (ex != tipx || ey != tipy) c.wideLine(tipx, tipy, ex, ey, 1.0f, pal.track);
  }

  const int bx = t.sx - static_cast<int>(lroundf(sh * kTailLen));
  const int by = t.sy + static_cast<int>(lroundf(ch * kTailLen));
  const int wx = static_cast<int>(lroundf(ch * kTailHalf));
  const int wy = static_cast<int>(lroundf(sh * kTailHalf));
  c.triangle(tipx, tipy, bx + wx, by + wy, bx - wx, by - wy, col);

  if (isEmergency(ac) && (millis() / 400) % 2 == 0) c.circle(t.sx, t.sy, 13, pal.alert);
  if (selected) {
    c.circle(t.sx, t.sy, 12, pal.select);
    c.circle(t.sx, t.sy, 13, pal.select);
  }
}

// ---- Tag layout (decluttering) -------------------------------------------
// Computed once per frame (before the first band) so every band draws the same
// result. Priority: selected, emergencies, then nearest first. Each tag tries
// the preferred side (toward the centre, as the original), then the other
// side, then shrinks to callsign only; if nothing fits it is hidden.

struct TagBox {
  int16_t x, y, w, h;
};

struct TagPlan {
  int16_t idx;     // target index
  int16_t x, y;    // text anchor
  uint8_t lines;   // 0 = hidden
  bool right;      // text to the right of the symbol
  bool selected;
};

TagPlan s_tags[services::adsb::kMaxAircraft];
size_t s_tag_count = 0;
TagBox s_boxes[services::adsb::kMaxAircraft + 4];
size_t s_box_count = 0;

bool overlaps(const TagBox& a) {
  for (size_t i = 0; i < s_box_count; ++i) {
    const TagBox& b = s_boxes[i];
    if (a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h) return true;
  }
  return false;
}

bool insideDisc(const TagBox& b) {
  // Keep tags within the radar half and roughly inside the screen circle.
  return b.x >= 0 && b.x + b.w <= kPanelX - 1 && b.y >= 0 && b.y + b.h <= 240;
}

void planTags(Canvas& c) {
  s_tag_count = 0;
  s_box_count = 0;
  const auto& cfg = settings::get();
  const int sel = traffic::selectedIndex();
  const int lh = c.fontHeight(Font::XS) - 2;
  const size_t n = traffic::count();

  // Reserve the range label and the centre dot so tags avoid them.
  char lbl[16];
  settings::formatRangeLabel(lbl, sizeof(lbl), settings::range().ring3_km);
  const int lw = c.textWidth(lbl, Font::XS);
  s_boxes[s_box_count++] = TagBox{static_cast<int16_t>(kCx + kOuterR - 5 - lw), static_cast<int16_t>(kCy - 3 - lh),
                                  static_cast<int16_t>(lw), static_cast<int16_t>(lh)};

  int order[services::adsb::kMaxAircraft];
  size_t m = 0;
  if (sel >= 0 && traffic::at(sel).inside) order[m++] = sel;
  for (size_t i = 0; i < n; ++i) {
    if (static_cast<int>(i) != sel && traffic::at(i).inside && isEmergency(traffic::at(i).ac)) order[m++] = static_cast<int>(i);
  }
  for (size_t i = 0; i < n; ++i) {
    if (static_cast<int>(i) != sel && traffic::at(i).inside && !isEmergency(traffic::at(i).ac)) order[m++] = static_cast<int>(i);
  }

  // Symbols are obstacles too (so tags don't cover other aircraft).
  static TagBox symbols[services::adsb::kMaxAircraft];
  for (size_t k = 0; k < m; ++k) {
    const auto& t = traffic::at(order[k]);
    symbols[k] = TagBox{static_cast<int16_t>(t.sx - 6), static_cast<int16_t>(t.sy - 6), 12, 12};
  }
  auto hitsSymbol = [&](const TagBox& b) {
    for (size_t k = 0; k < m; ++k) {
      const TagBox& s = symbols[k];
      if (b.x < s.x + s.w && s.x < b.x + b.w && b.y < s.y + s.h && s.y < b.y + b.h) return true;
    }
    return false;
  };

  for (size_t k = 0; k < m; ++k) {
    const int i = order[k];
    const auto& t = traffic::at(i);
    const bool selected = i == sel;
    const uint8_t mode = selected ? settings::kLabelsFull : cfg.label_mode;
    TagPlan plan{static_cast<int16_t>(i), 0, 0, 0, true, selected};
    if (mode != settings::kLabelsOff) {
      char alt[12];
      settings::formatAltitude(alt, sizeof(alt), t.ac.alt_ft, t.ac.on_ground, t.ac.alt_valid);
      int w_call = c.textWidth(t.ac.callsign, Font::XS);
      int w_full = w_call;
      const int wt = c.textWidth(t.ac.type, Font::XS), wa = c.textWidth(alt, Font::XS);
      if (wt > w_full) w_full = wt;
      if (wa > w_full) w_full = wa;
      const uint8_t full_lines = mode == settings::kLabelsFull ? (t.ac.type[0] ? 3 : 2) : 1;
      const bool pref_right = t.sx < kCx;
      bool placed = false;
      for (int attempt = 0; attempt < 4 && !placed; ++attempt) {
        const uint8_t lines = attempt < 2 ? full_lines : 1;
        if (attempt >= 2 && full_lines == 1) break;
        const bool right = (attempt % 2 == 0) ? pref_right : !pref_right;
        const int w = lines == 1 ? w_call : w_full;
        const int h = lines * lh;
        const int x = right ? t.sx + 9 : t.sx - 9;
        const int top = t.sy - h / 2;
        TagBox box{static_cast<int16_t>(right ? x - 1 : x - w - 1), static_cast<int16_t>(top - 1),
                   static_cast<int16_t>(w + 2), static_cast<int16_t>(h + 1)};
        if (selected || (insideDisc(box) && !overlaps(box) && !hitsSymbol(box))) {
          plan.x = static_cast<int16_t>(x);
          plan.y = static_cast<int16_t>(top);
          plan.lines = lines;
          plan.right = right;
          if (s_box_count < sizeof(s_boxes) / sizeof(s_boxes[0])) s_boxes[s_box_count++] = box;
          placed = true;
        }
      }
    }
    s_tags[s_tag_count++] = plan;
  }
}

void drawTagPlan(Canvas& c, const TagPlan& p) {
  if (p.lines == 0) return;
  const auto& ac = traffic::at(p.idx).ac;
  const int lh = c.fontHeight(Font::XS) - 2;
  if (!c.rows(p.y - 2, p.y + p.lines * lh + 2)) return;
  const textdatum_t d = p.right ? textdatum_t::top_left : textdatum_t::top_right;
  char alt[12];
  settings::formatAltitude(alt, sizeof(alt), ac.alt_ft, ac.on_ground, ac.alt_valid);
  if (p.selected) {
    int w = c.textWidth(ac.callsign, Font::XS);
    const int w2 = c.textWidth(alt, Font::XS);
    const int w3 = c.textWidth(ac.type, Font::XS);
    if (w2 > w) w = w2;
    if (w3 > w) w = w3;
    const int bx = p.right ? p.x - 2 : p.x - w - 2;
    c.fillRect(bx, p.y - 1, w + 4, p.lines * lh + 2, blend(pal.bg, pal.select, 40));
  }
  int y = p.y;
  c.text(ac.callsign, p.x, y, isEmergency(ac) ? pal.alert : pal.tag_call, Font::XS, d);
  if (p.lines == 1) return;
  y += lh;
  if (ac.type[0] && p.lines == 3) {
    c.text(ac.type, p.x, y, pal.tag_type, Font::XS, d);
    y += lh;
  }
  c.text(alt, p.x, y, pal.tag_alt, Font::XS, d);
}

void drawRimDot(Canvas& c, const traffic::Target& t) {
  const float a = t.brg_deg * kDegToRad;
  const int x = kCx + static_cast<int>(lroundf(sinf(a) * kRimR));
  const int y = kCy - static_cast<int>(lroundf(cosf(a) * kRimR));
  c.smoothCircle(x, y, 3, symbolColor(t));
}

void drawRadar(Canvas& c) {
  const auto& cfg = settings::get();
  c.fillRect(0, c.oy, kPanelX, c.h, pal.bg);
  if (cfg.sweep) drawSweep(c);
  drawGrid(c);
  drawOverlay(c);
  drawLabels(c);

  const int sel = traffic::selectedIndex();
  const size_t n = traffic::count();
  // Far to near so the nearest symbols end on top.
  if (cfg.show_trails) {
    for (size_t i = n; i-- > 0;) {
      const auto& t = traffic::at(i);
      if (t.inside) drawTrail(c, t);
    }
  }
  for (size_t i = n; i-- > 0;) {
    const auto& t = traffic::at(i);
    if (!t.inside) {
      drawRimDot(c, t);
    } else if (static_cast<int>(i) != sel) {
      drawSymbol(c, t, false);
    }
  }
  if (c.band == 0) planTags(c);
  if (sel >= 0 && traffic::at(sel).inside) drawSymbol(c, traffic::at(sel), true);
  // Lowest priority first so the important tags end on top.
  for (size_t k = s_tag_count; k-- > 0;) drawTagPlan(c, s_tags[k]);
}

void panelRow(Canvas& c, int y, const char* label, const char* value, uint16_t vcol) {
  c.text(label, kPanelX + 5, y, pal.text_dim, Font::XS);
  c.text(value, 316, y, vcol, Font::XS, textdatum_t::top_right);
}

void drawPanel(Canvas& c, bool online) {
  const auto& cfg = settings::get();
  c.fillRect(kPanelX, c.oy, kPanelW, c.h, pal.panel_bg);
  c.vline(kPanelX, c.oy, c.h, pal.panel_line);

  // Clock
  char buf[24];
  struct tm tmv;
  const bool synced = getLocalTime(&tmv, 0) && tmv.tm_year > 120;
  if (synced) {
    if (cfg.clock_24h) {
      strftime(buf, sizeof(buf), "%H:%M", &tmv);
    } else {
      int h = tmv.tm_hour % 12;
      if (h == 0) h = 12;
      snprintf(buf, sizeof(buf), "%d:%02d", h, tmv.tm_min);
    }
  } else {
    strcpy(buf, "--:--");
  }
  c.text(buf, 280, 3, pal.text, Font::L, textdatum_t::top_center);

  // Feed status + count
  const auto st = services::adsb::status();
  const bool sim = services::adsb::simulationCount() > 0;
  const bool fresh = st.last_ok_ms != 0 && millis() - st.last_ok_ms < config::kFeedStaleMs;
  uint16_t dot = pal.alert;
  const char* state = "OFFLINE";
  if (sim) { dot = pal.accent; state = "SIM"; }
  else if (online && fresh) { dot = pal.ok; state = "LIVE"; }
  else if (online && st.last_ok_ms == 0) { dot = pal.warn; state = "WAIT"; }
  else if (online) { dot = pal.warn; state = "STALE"; }
  c.smoothCircle(kPanelX + 8, 37, 3, dot);
  c.text(state, kPanelX + 15, 37, pal.text_dim, Font::XS, textdatum_t::middle_left);
  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(traffic::count()));
  c.text(buf, 316, 37, pal.text, Font::S, textdatum_t::middle_right);
  c.hline(kPanelX + 4, 47, kPanelW - 8, pal.panel_line);

  // Card: selected or nearest
  int idx = traffic::selectedIndex();
  const bool selected = idx >= 0;
  if (!selected && traffic::count() > 0) idx = 0;
  if (idx < 0) {
    // Say why the scope is empty: no Wi-Fi, no data yet, feed down, or quiet sky.
    const char* l1 = "NO TRAFFIC";
    const char* l2 = nullptr;
    if (!sim && !online) { l1 = "NO WI-FI"; l2 = "reconnecting..."; }
    else if (!sim && st.last_ok_ms == 0) { l1 = "WAITING"; l2 = "for first data"; }
    else if (!sim && !fresh) { l1 = "FEED DOWN"; l2 = "retrying..."; }
    c.text(l1, 280, 60, (l2 && l1[0] != 'W') ? pal.warn : pal.text_dim, Font::XS, textdatum_t::top_center);
    char r[16];
    settings::formatDistance(r, sizeof(r), settings::fetchRadiusKm(), true);
    snprintf(buf, sizeof(buf), "within %s", r);
    c.text(l2 ? l2 : buf, 280, 76, pal.text_dim, Font::XS, textdatum_t::top_center);
  } else {
    const auto& t = traffic::at(idx);
    const auto& ac = t.ac;
    const bool emerg = isEmergency(ac);
    const bool prox = cfg.alert_km > 0 && t.dist_km <= cfg.alert_km && !ac.on_ground;
    const char* title = emerg ? "EMERGENCY" : prox ? "PROXIMITY" : selected ? "SELECTED" : "NEAREST";
    const uint16_t tcol = (emerg || prox) ? pal.alert : (selected ? pal.select : pal.accent);
    if (emerg || prox) c.fillRect(kPanelX + 3, 50, kPanelW - 6, 13, blend(pal.panel_bg, pal.alert, 60));
    c.text(title, 280, 51, tcol, Font::XS, textdatum_t::top_center);
    c.text(">", 314, 51, pal.text_dim, Font::XS, textdatum_t::top_right);
    c.text(ac.callsign, 280, 64, pal.text, Font::M, textdatum_t::top_center);
    snprintf(buf, sizeof(buf), "%s %s", ac.type[0] ? ac.type : "", ac.reg);
    c.text(buf, 280, 80, pal.text_dim, Font::XS, textdatum_t::top_center);

    char v[16];
    settings::formatAltitude(v, sizeof(v), ac.alt_ft, ac.on_ground, ac.alt_valid);
    panelRow(c, 95, "ALT", v, pal.tag_alt);
    if (ac.vrate_fpm > 200) snprintf(v, sizeof(v), "+%d", ac.vrate_fpm);
    else if (ac.vrate_fpm < -200) snprintf(v, sizeof(v), "%d", ac.vrate_fpm);
    else strcpy(v, "level");
    panelRow(c, 107, "V/S", v, pal.text);
    settings::formatSpeed(v, sizeof(v), ac.gs_knots);
    panelRow(c, 119, "SPD", v, pal.text);
    snprintf(v, sizeof(v), "%03d", static_cast<int>(lroundf(ac.track_deg)) % 360);
    panelRow(c, 131, "TRK", v, pal.text);
    settings::formatDistance(v, sizeof(v), t.dist_km);
    panelRow(c, 143, "DST", v, pal.text);
    snprintf(v, sizeof(v), "%03d", static_cast<int>(lroundf(t.brg_deg)) % 360);
    panelRow(c, 155, ac.squawk[0] ? "SQK" : "BRG", ac.squawk[0] ? ac.squawk : v,
             emerg ? pal.alert : pal.text);
  }

  drawButton(c, kZoomOut, "-", BtnStyle::Normal, Font::L);
  drawButton(c, kZoomIn, "+", BtnStyle::Normal, Font::L);
  drawButton(c, kListBtn, "", BtnStyle::Normal);
  drawButton(c, kMenuBtn, "", BtnStyle::Normal);
  drawIconList(c, kListBtn.x + kListBtn.w / 2, kListBtn.y + kListBtn.h / 2, pal.button_text);
  drawIconSliders(c, kMenuBtn.x + kMenuBtn.w / 2, kMenuBtn.y + kMenuBtn.h / 2, pal.button_text);
}

bool s_online_for_draw = false;

}  // namespace

void radarSetOnline(bool online) { s_online_for_draw = online; }

void radarDraw(Canvas& c, void*) {
  drawRadar(c);
  drawPanel(c, s_online_for_draw);
}

void radarTouch(const touch::Event& e) {
  if (e.type == touch::Type::LongPress && e.x < kPanelX) {
    // Long-press the radar: toggle labels quickly.
    auto& cfg = settings::get();
    cfg.label_mode = (cfg.label_mode + 1) % 3;
    settings::save();
    requestRedraw();
    return;
  }
  if (e.type != touch::Type::Tap) return;
  if (kZoomIn.hit(e.x, e.y)) {
    if (settings::get().range_idx > 0) settings::rangePrev();
  } else if (kZoomOut.hit(e.x, e.y)) {
    if (settings::get().range_idx + 1 < settings::kRangeCount) settings::rangeNext();
  } else if (kListBtn.hit(e.x, e.y)) {
    navigate(ScreenId::List);
  } else if (kMenuBtn.hit(e.x, e.y)) {
    navigate(ScreenId::Menu);
  } else if (e.x < kPanelX) {
    const int hit = traffic::hitTest(e.x, e.y, 18);
    if (hit >= 0) traffic::select(traffic::at(hit).ac.hex);
    else traffic::clearSelection();
  } else if (e.y > 48 && e.y < 165) {
    detailOpen();  // card -> full aircraft details
    return;
  }
  requestRedraw();
}

}  // namespace ui
