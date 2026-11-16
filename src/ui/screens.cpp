// Secondary touch screens: traffic list, settings, airports,
// lookup, device info and confirmation dialog.
#include <Arduino.h>

#include <WiFi.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/airport_lookup.h"
#include "services/airport_search.h"
#include "services/airport_store.h"
#include "services/device_code.h"
#include "services/settings.h"
#include "ui/app.h"
#include "ui/theme.h"
#include "ui/traffic.h"

void wifiResetCredentialsAndReboot();

namespace ui {

// ---------------------------------------------------------------- widgets ---

void drawButton(Canvas& c, const Button& b, const char* label, BtnStyle style, Font f) {
  if (!c.rows(b.y, b.y + b.h)) return;
  uint16_t bg = pal.button, fg = pal.button_text;
  if (style == BtnStyle::Primary) { bg = pal.accent; fg = pal.bg; }
  if (style == BtnStyle::Danger) { bg = pal.alert; fg = rgb(255, 255, 255); }
  if (style == BtnStyle::Ghost) { bg = pal.panel_bg; fg = pal.text; }
  c.fillRoundRect(b.x, b.y, b.w, b.h, 6, bg);
  if (style == BtnStyle::Ghost) c.roundRect(b.x, b.y, b.w, b.h, 6, pal.panel_line);
  if (label && *label) c.text(label, b.x + b.w / 2, b.y + b.h / 2 + 1, fg, f, textdatum_t::middle_center);
}

void drawHeader(Canvas& c, const char* title, bool back, const char* right) {
  if (!c.rows(0, 31)) return;
  c.fillRect(0, 0, 320, 30, pal.panel_bg);
  c.hline(0, 30, 320, pal.panel_line);
  if (back) {
    c.wideLine(22, 8, 14, 15, 1.4f, pal.accent);
    c.wideLine(14, 15, 22, 22, 1.4f, pal.accent);
  }
  c.text(title, back ? 40 : 10, 15, pal.text, Font::M, textdatum_t::middle_left);
  if (right) c.text(right, 312, 15, pal.text_dim, Font::XS, textdatum_t::middle_right);
}

void drawIconList(Canvas& c, int cx, int cy, uint16_t col) {
  for (int i = -1; i <= 1; ++i) {
    c.fillRect(cx - 9, cy + i * 6 - 1, 3, 3, col);
    c.fillRect(cx - 4, cy + i * 6 - 1, 13, 2, col);
  }
}

void drawIconSliders(Canvas& c, int cx, int cy, uint16_t col) {
  const int knob[3] = {4, -3, 2};
  for (int i = -1; i <= 1; ++i) {
    c.fillRect(cx - 9, cy + i * 6, 18, 2, col);
    c.fillRect(cx + knob[i + 1] - 1, cy + i * 6 - 2, 3, 6, col);
  }
}

namespace {

void drawScrollbar(Canvas& c, int y0, int h, int first, int visible, int total) {
  if (total <= visible) return;
  c.fillRect(316, y0, 3, h, pal.panel_bg);
  const int bh = h * visible / total < 12 ? 12 : h * visible / total;
  const int by = y0 + (h - bh) * first / (total - visible);
  c.fillRect(316, by, 3, bh, pal.text_dim);
}

// ------------------------------------------------------------------- list ---

constexpr int kListTop = 52;
constexpr int kRowH = 23;
constexpr int kListRows = 8;
int s_list_first = 0;
int s_drag_accum = 0;
enum class ListSort : uint8_t { Dist, Alt, Call };
ListSort s_sort = ListSort::Dist;
uint8_t s_order[services::adsb::kMaxAircraft];
constexpr Button kSortBtn{232, 3, 82, 24};

void buildOrder() {
  const size_t n = traffic::count();
  for (size_t i = 0; i < n; ++i) s_order[i] = static_cast<uint8_t>(i);
  if (s_sort == ListSort::Dist) return;
  // insertion sort (n <= 80)
  for (size_t i = 1; i < n; ++i) {
    const uint8_t v = s_order[i];
    size_t j = i;
    while (j > 0) {
      const auto& a = traffic::at(s_order[j - 1]).ac;
      const auto& b = traffic::at(v).ac;
      const bool gt = s_sort == ListSort::Alt ? a.alt_ft < b.alt_ft
                                              : strcmp(a.callsign, b.callsign) > 0;
      if (!gt) break;
      s_order[j] = s_order[j - 1];
      --j;
    }
    s_order[j] = v;
  }
}

}  // namespace

void listEnter() { s_list_first = 0; }

void listDraw(Canvas& c, void*) {
  c.fill(pal.bg);
  char right[20];
  snprintf(right, sizeof(right), "%u", static_cast<unsigned>(traffic::count()));
  drawHeader(c, "Traffic", true, nullptr);
  c.text(right, 120, 15, pal.text_dim, Font::S, textdatum_t::middle_left);
  const char* sort = s_sort == ListSort::Dist ? "by DIST" : s_sort == ListSort::Alt ? "by ALT" : "by CALL";
  drawButton(c, kSortBtn, sort, BtnStyle::Ghost, Font::XS);

  // Column headers
  c.fillRect(0, 31, 320, 20, pal.panel_bg);
  c.text("CALLSIGN", 6, 41, pal.text_dim, Font::XS, textdatum_t::middle_left);
  c.text("TYPE", 92, 41, pal.text_dim, Font::XS, textdatum_t::middle_left);
  c.text("ALT", 196, 41, pal.text_dim, Font::XS, textdatum_t::middle_right);
  c.text("SPD", 248, 41, pal.text_dim, Font::XS, textdatum_t::middle_right);
  c.text("DIST", 308, 41, pal.text_dim, Font::XS, textdatum_t::middle_right);

  buildOrder();
  const int n = static_cast<int>(traffic::count());
  if (s_list_first > n - kListRows) s_list_first = n - kListRows;
  if (s_list_first < 0) s_list_first = 0;
  if (n == 0) {
    c.text("No aircraft in range", 160, 130, pal.text_dim, Font::S, textdatum_t::middle_center);
  }
  const int sel = traffic::selectedIndex();
  for (int r = 0; r < kListRows && s_list_first + r < n; ++r) {
    const int y = kListTop + r * kRowH;
    if (!c.rows(y, y + kRowH)) continue;
    const int idx = s_order[s_list_first + r];
    const auto& t = traffic::at(idx);
    const auto& ac = t.ac;
    if (idx == sel) c.fillRect(0, y, 314, kRowH, blend(pal.bg, pal.select, 45));
    else if (r % 2) c.fillRect(0, y, 314, kRowH, blend(pal.bg, pal.panel_bg, 160));
    const int my = y + kRowH / 2 + 1;
    c.fillRect(2, y + 5, 3, kRowH - 10, settings::get().alt_colors ? altitudeColor(ac.alt_ft, ac.on_ground) : pal.aircraft);
    const bool emerg = !strcmp(ac.squawk, "7700") || !strcmp(ac.squawk, "7600") || !strcmp(ac.squawk, "7500");
    c.text(ac.callsign, 8, my, emerg ? pal.alert : pal.text, Font::S, textdatum_t::middle_left);
    c.text(ac.type, 92, my, pal.tag_type, Font::XS, textdatum_t::middle_left);
    char v[16];
    settings::formatAltitude(v, sizeof(v), ac.alt_ft, ac.on_ground, ac.alt_valid);
    c.text(v, 196, my, pal.tag_alt, Font::XS, textdatum_t::middle_right);
    settings::formatSpeed(v, sizeof(v), ac.gs_knots);
    c.text(v, 248, my, pal.text, Font::XS, textdatum_t::middle_right);
    settings::formatDistance(v, sizeof(v), t.dist_km);
    c.text(v, 308, my, pal.text, Font::XS, textdatum_t::middle_right);
  }
  drawScrollbar(c, kListTop, kRowH * kListRows, s_list_first, kListRows, n);
}

void listTouch(const touch::Event& e) {
  if (e.type == touch::Type::Down) s_drag_accum = 0;
  if (e.type == touch::Type::Drag) {
    s_drag_accum += e.dy;
    while (s_drag_accum <= -kRowH / 2) { ++s_list_first; s_drag_accum += kRowH / 2; requestRedraw(); }
    while (s_drag_accum >= kRowH / 2) { --s_list_first; s_drag_accum -= kRowH / 2; requestRedraw(); }
    return;
  }
  if (e.type != touch::Type::Tap) return;
  if (kBackBtn.hit(e.x, e.y)) { navigate(ScreenId::Radar); return; }
  if (kSortBtn.hit(e.x, e.y)) {
    s_sort = static_cast<ListSort>((static_cast<int>(s_sort) + 1) % 3);
    requestRedraw();
    return;
  }
  if (e.y >= kListTop) {
    const int r = (e.y - kListTop) / kRowH;
    const int i = s_list_first + r;
    if (r < kListRows && i < static_cast<int>(traffic::count())) {
      traffic::select(traffic::at(s_order[i]).ac.hex);
      navigate(ScreenId::Radar);
    }
  }
}

// ------------------------------------------------------------------- menu ---

namespace {

enum class Item : uint8_t {
  Range, Labels, Trails, Runways, Ground, AltColors, Sweep, Theme, Brightness,
  Dist, Alt, Speed, Alert, AlertLed, StatusLed, Clock, Flip, Invert,
  Find, Airports, Night, NightDim, NightTheme, MaxAlt, Refresh, Demo, Info,
  Calibrate, Restart, ResetWifi,
  Count
};
constexpr int kTilesPerPage = 9;
constexpr int kPages = (static_cast<int>(Item::Count) + kTilesPerPage - 1) / kTilesPerPage;
int s_menu_page = 0;
constexpr Button kPrevBtn{216, 3, 44, 24};
constexpr Button kNextBtn{268, 3, 44, 24};

Button tileRect(int slot) {
  const int col = slot % 3, row = slot / 3;
  return Button{static_cast<int16_t>(4 + col * 106), static_cast<int16_t>(36 + row * 68), 100, 62};
}

const char* itemLabel(Item it) {
  switch (it) {
    case Item::Range: return "RANGE";
    case Item::Labels: return "LABELS";
    case Item::Trails: return "TRAILS";
    case Item::Runways: return "RUNWAYS";
    case Item::Ground: return "GROUND A/C";
    case Item::AltColors: return "ALT COLOURS";
    case Item::Sweep: return "SWEEP";
    case Item::Theme: return "THEME";
    case Item::Brightness: return "BRIGHTNESS";
    case Item::Dist: return "DISTANCE";
    case Item::Alt: return "ALTITUDE";
    case Item::Speed: return "SPEED";
    case Item::Alert: return "ALERT RADIUS";
    case Item::AlertLed: return "ALERT LED";
    case Item::StatusLed: return "STATUS LED";
    case Item::Clock: return "CLOCK";
    case Item::Flip: return "ROTATE 180";
    case Item::Airports: return "AIRPORTS";
    case Item::Info: return "DEVICE INFO";
    case Item::Calibrate: return "TOUCH";
    case Item::Invert: return "INVERT COLOURS";
    case Item::Refresh: return "REFRESH";
    case Item::MaxAlt: return "MAX ALTITUDE";
    case Item::Demo: return "DEMO TRAFFIC";
    case Item::Restart: return "RESTART";
    case Item::ResetWifi: return "RESET WI-FI";
    case Item::Night: return "NIGHT MODE";
    case Item::NightDim: return "NIGHT BRIGHTNESS";
    case Item::NightTheme: return "NIGHT COLOURS";
    case Item::Find: return "FIND AIRPORT";
    default: return "";
  }
}

void itemValue(Item it, char* v, size_t n) {
  const auto& c = settings::get();
  auto onoff = [&](bool b) { snprintf(v, n, "%s", b ? "On" : "Off"); };
  switch (it) {
    case Item::Range: settings::formatRangeLabel(v, n, settings::range().ring3_km); break;
    case Item::Labels: snprintf(v, n, "%s", c.label_mode == 0 ? "Off" : c.label_mode == 1 ? "Callsign" : "Full"); break;
    case Item::Trails: onoff(c.show_trails); break;
    case Item::Runways: onoff(c.show_runways); break;
    case Item::Ground: onoff(c.show_ground); break;
    case Item::AltColors: onoff(c.alt_colors); break;
    case Item::Sweep: onoff(c.sweep); break;
    case Item::Theme: snprintf(v, n, "%s", settings::themeName(c.theme)); break;
    case Item::Brightness:
      snprintf(v, n, "%u%%", c.brightness);
      break;
    case Item::Dist: snprintf(v, n, "%s", settings::distUnitName()); break;
    case Item::Alt: snprintf(v, n, "%s", c.alt_units ? "metres" : "feet"); break;
    case Item::Speed: snprintf(v, n, "%s", c.speed_units == 0 ? "knots" : c.speed_units == 1 ? "km/h" : "mph"); break;
    case Item::Alert:
      if (c.alert_km == 0) snprintf(v, n, "Off"); else settings::formatDistance(v, n, c.alert_km, true);
      break;
    case Item::AlertLed: onoff(c.alert_led); break;
    case Item::StatusLed: onoff(c.status_led); break;
    case Item::Clock: snprintf(v, n, "%s", c.clock_24h ? "24 h" : "12 h"); break;
    case Item::Flip: onoff(c.flip); break;
    case Item::Airports: snprintf(v, n, "%u saved", static_cast<unsigned>(services::airports::count())); break;
    case Item::Info: snprintf(v, n, "IP / login"); break;
    case Item::Calibrate: snprintf(v, n, "Calibrate"); break;
    case Item::Invert: onoff(c.invert); break;
    case Item::Refresh: snprintf(v, n, "%us", c.fetch_s); break;
    case Item::MaxAlt:
      if (c.max_alt_kft == 0) snprintf(v, n, "No limit"); else snprintf(v, n, "%uk ft", c.max_alt_kft);
      break;
    case Item::Demo:
      if (services::adsb::simulationCount()) snprintf(v, n, "%u a/c", services::adsb::simulationCount());
      else snprintf(v, n, "Off");
      break;
    case Item::Restart: snprintf(v, n, "Reboot"); break;
    case Item::ResetWifi: snprintf(v, n, "Clear all"); break;
    case Item::Night:
      if (!c.night_mode) snprintf(v, n, "Off");
      else snprintf(v, n, "%02u-%02u%s", c.night_start_h, c.night_end_h, nightActive() ? " *" : "");
      break;
    case Item::NightDim: snprintf(v, n, "%u%%", c.night_brightness); break;
    case Item::NightTheme: snprintf(v, n, "%s", c.night_theme ? "Red" : "Unchanged"); break;
    case Item::Find: snprintf(v, n, "Search"); break;
    default: v[0] = '\0';
  }
}

void doRestart() { ESP.restart(); }
void doResetWifi() { wifiResetCredentialsAndReboot(); }

void activate(Item it) {
  auto& c = settings::get();
  bool persist = true;
  switch (it) {
    case Item::Range: settings::rangeNext(); persist = false; break;
    case Item::Labels: c.label_mode = (c.label_mode + 1) % 3; break;
    case Item::Trails: c.show_trails = !c.show_trails; break;
    case Item::Runways: c.show_runways = !c.show_runways; break;
    case Item::Ground: c.show_ground = !c.show_ground; break;
    case Item::AltColors: c.alt_colors = !c.alt_colors; break;
    case Item::Sweep: c.sweep = !c.sweep; break;
    case Item::Theme: c.theme = (c.theme + 1) % settings::kThemeCount; break;
    case Item::Brightness: {
      static const uint8_t kLevels[] = {20, 40, 60, 80, 100};
      size_t i = 0;
      while (i < 5 && kLevels[i] <= c.brightness) ++i;
      c.brightness = kLevels[i % 5];
      break;
    }
    case Item::Dist: c.dist_units = (c.dist_units + 1) % 3; break;
    case Item::Alt: c.alt_units = (c.alt_units + 1) % 2; break;
    case Item::Speed: c.speed_units = (c.speed_units + 1) % 3; break;
    case Item::Alert: {
      static const uint8_t kAlerts[] = {0, 2, 3, 5, 10, 20};
      size_t i = 0;
      while (i < 6 && kAlerts[i] != c.alert_km) ++i;
      c.alert_km = kAlerts[(i + 1) % 6];
      break;
    }
    case Item::AlertLed: c.alert_led = !c.alert_led; break;
    case Item::StatusLed: c.status_led = !c.status_led; break;
    case Item::Clock: c.clock_24h = !c.clock_24h; break;
    case Item::Flip: c.flip = !c.flip; hw::applyOrientation(c.flip, c.invert); break;
    case Item::Invert: c.invert = !c.invert; hw::applyOrientation(c.flip, c.invert); break;
    case Item::Refresh: {
      static const uint8_t kRates[] = {3, 5, 10, 15, 30};
      size_t i = 0;
      while (i < 5 && kRates[i] != c.fetch_s) ++i;
      c.fetch_s = kRates[(i + 1) % 5];
      break;
    }
    case Item::MaxAlt: {
      static const uint16_t kMax[] = {0, 5, 10, 20, 30};
      size_t i = 0;
      while (i < 5 && kMax[i] != c.max_alt_kft) ++i;
      c.max_alt_kft = kMax[(i + 1) % 5];
      break;
    }
    case Item::Demo: {
      static const uint16_t kSim[] = {0, 10, 40, 80};
      const uint16_t cur = services::adsb::simulationCount();
      size_t i = 0;
      while (i < 4 && kSim[i] != cur) ++i;
      services::adsb::setSimulation(kSim[(i + 1) % 4]);
      persist = false;
      break;
    }
    case Item::Night: {
      // Off -> each preset -> Off
      size_t i = 0;
      if (c.night_mode) {
        while (i < settings::kNightPresetCount &&
               !(settings::kNightPresets[i][0] == c.night_start_h && settings::kNightPresets[i][1] == c.night_end_h))
          ++i;
        ++i;
      }
      if (i >= settings::kNightPresetCount) {
        c.night_mode = false;
      } else {
        c.night_mode = true;
        c.night_start_h = settings::kNightPresets[i][0];
        c.night_end_h = settings::kNightPresets[i][1];
      }
      break;
    }
    case Item::NightDim: {
      static const uint8_t kDim[] = {5, 10, 20, 40};
      size_t i = 0;
      while (i < 4 && kDim[i] != c.night_brightness) ++i;
      c.night_brightness = kDim[(i + 1) % 4];
      break;
    }
    case Item::NightTheme: c.night_theme = !c.night_theme; break;
    case Item::Find: searchEnter(true); return;
    case Item::Airports: navigate(ScreenId::Airports); return;
    case Item::Info: navigate(ScreenId::Info); return;
    case Item::Calibrate:
      touch::calibrate();
      requestRedraw();
      return;
    case Item::Restart:
      confirmEnter("Restart?", "The radar will reboot.", doRestart, ScreenId::Menu);
      return;
    case Item::ResetWifi:
      confirmEnter("Reset Wi-Fi?", "Clears Wi-Fi, location and settings,\nthen opens the setup portal.",
                   doResetWifi, ScreenId::Menu);
      return;
    default: return;
  }
  if (persist) settings::save();
  requestRedraw();
}

}  // namespace

void menuEnter() {}

void menuDraw(Canvas& c, void*) {
  c.fill(pal.bg);
  static const char* kTitles[] = {"Display", "Units & alerts", "Airports & night", "System"};
  drawHeader(c, s_menu_page < 4 ? kTitles[s_menu_page] : "Settings", true);
  drawButton(c, kPrevBtn, "<", BtnStyle::Ghost, Font::M);
  drawButton(c, kNextBtn, ">", BtnStyle::Ghost, Font::M);
  char pg[8];
  snprintf(pg, sizeof(pg), "%d/%d", s_menu_page + 1, kPages);
  c.text(pg, 208, 15, pal.text_dim, Font::XS, textdatum_t::middle_right);

  for (int slot = 0; slot < kTilesPerPage; ++slot) {
    const int idx = s_menu_page * kTilesPerPage + slot;
    if (idx >= static_cast<int>(Item::Count)) break;
    const Item it = static_cast<Item>(idx);
    const Button b = tileRect(slot);
    if (!c.rows(b.y, b.y + b.h)) continue;
    const bool danger = it == Item::ResetWifi;
    c.fillRoundRect(b.x, b.y, b.w, b.h, 8, danger ? blend(pal.panel_bg, pal.alert, 70) : pal.panel_bg);
    c.roundRect(b.x, b.y, b.w, b.h, 8, pal.panel_line);
    c.text(itemLabel(it), b.x + 8, b.y + 8, pal.text_dim, Font::XS);
    char v[20];
    itemValue(it, v, sizeof(v));
    c.text(v, b.x + 8, b.y + b.h - 10, danger ? pal.alert : pal.accent, Font::M, textdatum_t::bottom_left);
  }
}

void menuTouch(const touch::Event& e) {
  if (e.type == touch::Type::Drag) return;
  if (e.type == touch::Type::Release && abs(e.x - e.sx) > 60) {  // swipe pages
    s_menu_page = (s_menu_page + (e.x < e.sx ? 1 : kPages - 1)) % kPages;
    requestRedraw();
    return;
  }
  if (e.type != touch::Type::Tap) return;
  if (kBackBtn.hit(e.x, e.y)) { navigate(ScreenId::Radar); return; }
  if (kPrevBtn.hit(e.x, e.y)) { s_menu_page = (s_menu_page + kPages - 1) % kPages; requestRedraw(); return; }
  if (kNextBtn.hit(e.x, e.y)) { s_menu_page = (s_menu_page + 1) % kPages; requestRedraw(); return; }
  for (int slot = 0; slot < kTilesPerPage; ++slot) {
    const int idx = s_menu_page * kTilesPerPage + slot;
    if (idx >= static_cast<int>(Item::Count)) break;
    if (tileRect(slot).hit(e.x, e.y)) {
      activate(static_cast<Item>(idx));
      return;
    }
  }
}

// --------------------------------------------------------------- airports ---

namespace {

constexpr int kApTop = 34;
constexpr int kApRowH = 40;
constexpr int kApRows = 5;
int s_ap_first = 0;
int s_ap_drag = 0;
constexpr Button kAddBtn{236, 3, 78, 24};
char s_pending_delete[8] = "";

void doDeleteAirport() {
  services::airports::remove(s_pending_delete);
  settings::markChanged();
}

Button centerBtn(int row) { return Button{214, static_cast<int16_t>(kApTop + row * kApRowH + 6), 46, 28}; }
Button deleteBtn(int row) { return Button{266, static_cast<int16_t>(kApTop + row * kApRowH + 6), 46, 28}; }

}  // namespace

void airportsEnter() { s_ap_first = 0; }

void airportsDraw(Canvas& c, void*) {
  c.fill(pal.bg);
  drawHeader(c, "Airports", true);
  drawButton(c, kAddBtn, "FIND", BtnStyle::Primary, Font::S);
  const int n = static_cast<int>(services::airports::count());
  if (s_ap_first > n - kApRows) s_ap_first = n - kApRows;
  if (s_ap_first < 0) s_ap_first = 0;
  if (n == 0) {
    c.text("No custom airports yet", 160, 90, pal.text, Font::M, textdatum_t::middle_center);
    c.text("Tap FIND and type a name, city or code", 160, 118, pal.text_dim, Font::XS, textdatum_t::middle_center);
    c.text("(\"tampa\", \"KTPA\" or \"TPA\"), or use the web page:", 160, 134, pal.text_dim, Font::XS, textdatum_t::middle_center);
    c.text("http://plane-radar.local/airports", 160, 154, pal.accent, Font::XS, textdatum_t::middle_center);
    char idx[48];
    snprintf(idx, sizeof(idx), "Searchable: %u airports, offline", static_cast<unsigned>(services::search::count()));
    c.text(idx, 160, 200, pal.text_dim, Font::XS, textdatum_t::middle_center);
    return;
  }
  for (int r = 0; r < kApRows && s_ap_first + r < n; ++r) {
    const auto& ap = services::airports::at(s_ap_first + r);
    const int y = kApTop + r * kApRowH;
    if (!c.rows(y, y + kApRowH)) continue;
    if (r % 2) c.fillRect(0, y, 314, kApRowH, blend(pal.bg, pal.panel_bg, 160));
    c.text(ap.ident, 8, y + 4, pal.custom_runway, Font::M);
    char info[56];
    snprintf(info, sizeof(info), "%.26s  (%u rwy)", ap.name[0] ? ap.name : "Custom", ap.runway_count);
    c.text(info, 8, y + 23, pal.text_dim, Font::XS);
    drawButton(c, centerBtn(r), "HOME", BtnStyle::Ghost, Font::XS);
    drawButton(c, deleteBtn(r), "DEL", BtnStyle::Danger, Font::XS);
  }
  drawScrollbar(c, kApTop, kApRowH * kApRows, s_ap_first, kApRows, n);
}

void airportsTouch(const touch::Event& e) {
  if (e.type == touch::Type::Down) s_ap_drag = 0;
  if (e.type == touch::Type::Drag) {
    s_ap_drag += e.dy;
    if (s_ap_drag <= -kApRowH / 2) { ++s_ap_first; s_ap_drag = 0; requestRedraw(); }
    if (s_ap_drag >= kApRowH / 2) { --s_ap_first; s_ap_drag = 0; requestRedraw(); }
    return;
  }
  if (e.type != touch::Type::Tap) return;
  if (kBackBtn.hit(e.x, e.y)) { navigate(ScreenId::Menu); return; }
  if (kAddBtn.hit(e.x, e.y)) {
    searchEnter(true);
    return;
  }
  const int n = static_cast<int>(services::airports::count());
  for (int r = 0; r < kApRows && s_ap_first + r < n; ++r) {
    const auto& ap = services::airports::at(s_ap_first + r);
    if (centerBtn(r).hit(e.x, e.y)) {
      settings::setLocation(ap.lat, ap.lon);
      navigate(ScreenId::Radar);
      return;
    }
    if (deleteBtn(r).hit(e.x, e.y)) {
      strncpy(s_pending_delete, ap.ident, sizeof(s_pending_delete) - 1);
      static char body[64];
      snprintf(body, sizeof(body), "Remove %s from the radar?", ap.ident);
      confirmEnter("Delete airport", body, doDeleteAirport, ScreenId::Airports);
      return;
    }
  }
}

// ----------------------------------------------------------------- lookup ---

namespace {

constexpr Button kLkLeft{4, 202, 100, 34};
constexpr Button kLkRight{216, 202, 100, 34};
// Three-button row when the lookup came from the search screen.
constexpr Button kLk3Left{4, 202, 98, 34};
constexpr Button kLk3Mid{111, 202, 98, 34};
constexpr Button kLk3Right{218, 202, 98, 34};
ScreenId s_lk_back = ScreenId::Airports;
int s_lk_index = -1;  // search index entry (coordinates if the lookup fails)
services::lookup::State s_lk_last = services::lookup::State::kIdle;
uint8_t s_lk_progress = 255;

void drawRunwayPreview(Canvas& c, const services::airports::Airport& ap, int bx, int by, int bw, int bh) {
  c.fillRoundRect(bx, by, bw, bh, 6, pal.panel_bg);
  if (ap.runway_count == 0) {
    c.text("no runway geometry", bx + bw / 2, by + bh / 2, pal.text_dim, Font::XS, textdatum_t::middle_center);
    return;
  }
  // Fit all runway ends into the box (flat projection around the airport).
  const float cl = cosf(ap.lat * 0.01745329f);
  float minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
  auto px = [&](float lon) { return (lon - ap.lon) * cl; };
  auto py = [&](float lat) { return lat - ap.lat; };
  for (uint8_t i = 0; i < ap.runway_count; ++i) {
    const auto& r = ap.runways[i];
    for (float x : {px(r.le_lon), px(r.he_lon)}) { if (x < minx) minx = x; if (x > maxx) maxx = x; }
    for (float y : {py(r.le_lat), py(r.he_lat)}) { if (y < miny) miny = y; if (y > maxy) maxy = y; }
  }
  const float span = fmaxf(fmaxf(maxx - minx, maxy - miny), 1e-4f);
  const float k = (fminf(bw, bh) - 16) / span;
  const float mx = (minx + maxx) / 2, my = (miny + maxy) / 2;
  const int cx = bx + bw / 2, cy = by + bh / 2;
  for (uint8_t i = 0; i < ap.runway_count; ++i) {
    const auto& r = ap.runways[i];
    const int x0 = cx + static_cast<int>((px(r.le_lon) - mx) * k);
    const int y0 = cy - static_cast<int>((py(r.le_lat) - my) * k);
    const int x1 = cx + static_cast<int>((px(r.he_lon) - mx) * k);
    const int y1 = cy - static_cast<int>((py(r.he_lat) - my) * k);
    c.wideLine(x0, y0, x1, y1, 2.0f, pal.custom_runway);
  }
}

}  // namespace

void lookupSetOrigin(ScreenId back, int index_idx) {
  s_lk_back = back;
  s_lk_index = index_idx;
}

bool lookupTick() {
  const auto st = services::lookup::state();
  const uint8_t p = services::lookup::progress();
  if (st != s_lk_last || p != s_lk_progress) {
    s_lk_last = st;
    s_lk_progress = p;
    return true;
  }
  return false;
}

void lookupDraw(Canvas& c, void*) {
  using services::lookup::State;
  c.fill(pal.bg);
  const bool from_search = s_lk_back == ScreenId::Search;
  char title[40];
  snprintf(title, sizeof(title), "Airport %s", services::lookup::ident());
  drawHeader(c, title, false);
  const State st = services::lookup::state();
  if (s_lk_index >= 0 && st != State::kDone) {
    const auto& e = services::search::entry(static_cast<uint16_t>(s_lk_index));
    c.text(services::search::name(e), 160, 52, pal.text, Font::M, textdatum_t::middle_center);
  }
  if (st == State::kRunning || st == State::kIdle) {
    c.text(services::lookup::message(), 160, 96, pal.text, Font::S, textdatum_t::middle_center);
    c.fillRoundRect(30, 116, 260, 14, 7, pal.panel_bg);
    const int w = 260 * services::lookup::progress() / 100;
    if (w > 6) c.fillRoundRect(30, 116, w, 14, 7, pal.accent);
    c.text("Fetching runways from OurAirports", 160, 150, pal.text_dim, Font::XS, textdatum_t::middle_center);
    drawButton(c, kLkLeft, "HIDE", BtnStyle::Ghost, Font::S);
    return;
  }
  if (st == State::kError) {
    c.text("Lookup failed", 160, 86, pal.alert, Font::M, textdatum_t::middle_center);
    c.text(services::lookup::message(), 160, 112, pal.text, Font::XS, textdatum_t::middle_center);
    if (s_lk_index >= 0) {
      c.text("HOME still moves the radar here", 160, 150, pal.text_dim, Font::XS, textdatum_t::middle_center);
      c.text("(runways need a connection)", 160, 166, pal.text_dim, Font::XS, textdatum_t::middle_center);
      drawButton(c, kLk3Left, "BACK", BtnStyle::Ghost, Font::S);
      drawButton(c, kLk3Mid, "HOME", BtnStyle::Normal, Font::S);
      drawButton(c, kLk3Right, "RETRY", BtnStyle::Primary, Font::S);
    } else {
      drawButton(c, kLkLeft, "BACK", BtnStyle::Ghost, Font::S);
      drawButton(c, kLkRight, "RETRY", BtnStyle::Primary, Font::S);
    }
    return;
  }
  const auto& ap = services::lookup::result();
  c.text(ap.ident, 10, 38, pal.custom_runway, Font::L);
  char line[64];
  int y = 66;
  if (s_lk_index >= 0) {
    const auto& e = services::search::entry(static_cast<uint16_t>(s_lk_index));
    if (e.iata[0]) c.text(e.iata, 10 + c.textWidth(ap.ident, Font::L) + 8, 44, pal.text_dim, Font::S);
    c.text(services::search::name(e), 10, y, pal.text, Font::XS);
    const char* city = services::search::city(e);
    snprintf(line, sizeof(line), "%.22s%s%s", city, *city ? ", " : "", services::search::region(e));
    c.text(line, 10, y + 15, pal.text_dim, Font::XS);
    y += 32;
  } else {
    snprintf(line, sizeof(line), "%.30s", ap.name);
    c.text(line, 10, y, pal.text, Font::XS);
    y += 18;
  }
  c.text(services::lookup::message(), 10, y, pal.text_dim, Font::XS);
  y += 18;
  for (uint8_t i = 0; i < ap.runway_count && y < 176; ++i, y += 14) {
    c.text(ap.runways[i].name, 10, y, pal.text, Font::XS);
  }
  float dx, dy;
  traffic::offsetKm(ap.lat, ap.lon, &dx, &dy);
  char d[16];
  settings::formatDistance(d, sizeof(d), sqrtf(dx * dx + dy * dy));
  snprintf(line, sizeof(line), "%s from radar", d);
  c.text(line, 10, 194, pal.text_dim, Font::XS, textdatum_t::bottom_left);
  drawRunwayPreview(c, ap, 180, 38, 134, 156);
  if (from_search) {
    drawButton(c, kLk3Left, "BACK", BtnStyle::Ghost, Font::S);
    drawButton(c, kLk3Mid, "HOME", BtnStyle::Normal, Font::S);
    drawButton(c, kLk3Right, "SAVE", BtnStyle::Primary, Font::S);
  } else {
    drawButton(c, kLkLeft, "CANCEL", BtnStyle::Ghost, Font::S);
    drawButton(c, kLkRight, "SAVE", BtnStyle::Primary, Font::S);
  }
}

void lookupTouch(const touch::Event& e) {
  using services::lookup::State;
  if (e.type != touch::Type::Tap) return;
  const State st = services::lookup::state();
  const bool three = (s_lk_back == ScreenId::Search && st == State::kDone) ||
                     (s_lk_index >= 0 && st == State::kError);
  const Button& left = three ? kLk3Left : kLkLeft;
  const Button& right = three ? kLk3Right : kLkRight;
  if (left.hit(e.x, e.y)) {
    if (st != State::kRunning) services::lookup::reset();
    navigate(s_lk_back);
    return;
  }
  if (three && kLk3Mid.hit(e.x, e.y)) {
    // HOME: centre the radar on this airport (and keep its runways).
    if (st == State::kDone) {
      const auto& ap = services::lookup::result();
      services::airports::upsert(ap);
      settings::setLocation(ap.lat, ap.lon);
    } else {
      const auto& en = services::search::entry(static_cast<uint16_t>(s_lk_index));
      settings::setLocation(services::search::lat(en), services::search::lon(en));
    }
    services::lookup::reset();
    settings::markChanged();
    navigate(ScreenId::Radar);
    return;
  }
  if (right.hit(e.x, e.y)) {
    if (st == State::kDone) {
      services::airports::upsert(services::lookup::result());
      services::lookup::reset();
      settings::markChanged();
      navigate(ScreenId::Airports);
    } else if (st == State::kError) {
      char id[8];
      strncpy(id, services::lookup::ident(), sizeof(id));
      services::lookup::start(id);
      requestRedraw();
    }
  }
}
// ------------------------------------------------------------------- info ---

void infoDraw(Canvas& c, void*) {
  c.fill(pal.bg);
  drawHeader(c, "Device info", true);
  const bool online = WiFi.status() == WL_CONNECTED;
  char url[48];
  if (online) snprintf(url, sizeof(url), "http://%s/", WiFi.localIP().toString().c_str());
  else snprintf(url, sizeof(url), "http://%s/", config::kPortalIp);
  c.fillRect(6, 38, 128, 128, rgb(255, 255, 255));
  c.qr(url, 10, 42, 120);
  c.text("Scan to configure", 70, 172, pal.text_dim, Font::XS, textdatum_t::top_center);

  char line[64];
  int y = 36;
  auto row = [&](const char* k, const char* v) {
    c.text(k, 146, y, pal.text_dim, Font::XS);
    c.text(v, 146, y + 12, pal.text, Font::XS);
    y += 29;
  };
  row("WEB", online ? url : "offline");
  snprintf(line, sizeof(line), "http://%s", config::kPortalHostUrl);
  row("MDNS", line);
  if (online) snprintf(line, sizeof(line), "%.20s  %d dBm", WiFi.SSID().c_str(), WiFi.RSSI());
  else snprintf(line, sizeof(line), "not connected");
  row("WI-FI", line);
  const auto st = services::adsb::status();
  snprintf(line, sizeof(line), "%lu ok / %lu err  %ums", static_cast<unsigned long>(st.ok_count),
           static_cast<unsigned long>(st.err_count), st.fetch_ms);
  row("FEED", line);
  snprintf(line, sizeof(line), "v%s  heap %uk  up %lum", config::kFirmwareVersion,
           ESP.getFreeHeap() / 1024, millis() / 60000);
  row("SYSTEM", line);
  snprintf(line, sizeof(line), "%s / %s", services::device_code::kLoginUser, services::device_code::get());
  row("WEB LOGIN", line);
  uint16_t home;
  if (services::search::nearest(settings::get().lat, settings::get().lon, &home, nullptr)) {
    snprintf(line, sizeof(line), "Home: %s", services::search::entry(home).ident);
    c.text(line, 70, 190, pal.text_dim, Font::XS, textdatum_t::top_center);
  }
  c.text("BOOT: tap = range, hold 3 s = reset", 160, 226, pal.text_dim, Font::XS, textdatum_t::middle_center);
}

void infoTouch(const touch::Event& e) {
  if (e.type == touch::Type::Tap && kBackBtn.hit(e.x, e.y)) navigate(ScreenId::Menu);
}

// ---------------------------------------------------------------- confirm ---

namespace {
const char* s_cf_title = "";
const char* s_cf_body = "";
void (*s_cf_yes)() = nullptr;
ScreenId s_cf_back = ScreenId::Radar;
constexpr Button kCfNo{40, 160, 110, 38};
constexpr Button kCfYes{170, 160, 110, 38};
}  // namespace

void confirmEnter(const char* title, const char* body, void (*yes)(), ScreenId back) {
  s_cf_title = title;
  s_cf_body = body;
  s_cf_yes = yes;
  s_cf_back = back;
  navigate(ScreenId::Confirm);
}

void confirmDraw(Canvas& c, void*) {
  c.fill(pal.bg);
  c.fillRoundRect(20, 40, 280, 170, 12, pal.panel_bg);
  c.roundRect(20, 40, 280, 170, 12, pal.panel_line);
  c.text(s_cf_title, 160, 70, pal.text, Font::M, textdatum_t::middle_center);
  // Body may contain one '\n'.
  char a[64];
  strncpy(a, s_cf_body, sizeof(a) - 1);
  a[sizeof(a) - 1] = '\0';
  char* nl = strchr(a, '\n');
  if (nl) {
    *nl = '\0';
    c.text(a, 160, 104, pal.text_dim, Font::XS, textdatum_t::middle_center);
    c.text(nl + 1, 160, 120, pal.text_dim, Font::XS, textdatum_t::middle_center);
  } else {
    c.text(a, 160, 110, pal.text_dim, Font::XS, textdatum_t::middle_center);
  }
  drawButton(c, kCfNo, "CANCEL", BtnStyle::Ghost, Font::S);
  drawButton(c, kCfYes, "CONFIRM", BtnStyle::Danger, Font::S);
}

void confirmTouch(const touch::Event& e) {
  if (e.type != touch::Type::Tap) return;
  if (kCfNo.hit(e.x, e.y)) navigate(s_cf_back);
  if (kCfYes.hit(e.x, e.y)) {
    if (s_cf_yes) s_cf_yes();
    navigate(s_cf_back);
  }
}

}  // namespace ui
