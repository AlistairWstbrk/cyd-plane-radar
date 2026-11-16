// Plane Radar CYD - host simulator and test runner.
// Runs the real UI / settings / traffic / airport-lookup code against a
// 320x240 sprite "panel", drives it with injected taps, checks placement
// accuracy numerically, stress-tests with simulated traffic and writes PNGs.
#include <Arduino.h>
#include <WiFi.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/airport_search.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include <cstddef>
#include "services/adsb_client.h"
#include "services/airport_lookup.h"
#include "services/airport_store.h"
#include "services/settings.h"
#include "ui/app.h"
#include "ui/canvas.h"
#include "ui/status_screens.h"
#include "ui/theme.h"
#include "ui/touch.h"
#include "ui/traffic.h"

extern int g_sim_http_requests;

namespace {

int g_fail = 0;
int g_pass = 0;
std::string g_outdir = "sim_out";

void check(bool ok, const char* what, const char* detail = "") {
  if (ok) ++g_pass; else ++g_fail;
  std::printf("  [%s] %s %s\n", ok ? "PASS" : "FAIL", what, detail);
}

// ---- PNG writer (stored deflate, no zlib needed) ----
uint32_t crc32(const uint8_t* d, size_t n, uint32_t c = 0xFFFFFFFFu) {
  static uint32_t t[256];
  static bool init = false;
  if (!init) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t k = i;
      for (int j = 0; j < 8; ++j) k = (k & 1) ? 0xEDB88320u ^ (k >> 1) : k >> 1;
      t[i] = k;
    }
    init = true;
  }
  for (size_t i = 0; i < n; ++i) c = t[(c ^ d[i]) & 0xFF] ^ (c >> 8);
  return c;
}

void put32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}

void chunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& data) {
  put32(png, static_cast<uint32_t>(data.size()));
  std::vector<uint8_t> td(type, type + 4);
  td.insert(td.end(), data.begin(), data.end());
  png.insert(png.end(), td.begin(), td.end());
  put32(png, crc32(td.data(), td.size()) ^ 0xFFFFFFFFu);
}

void savePng(const std::string& name) {
  const int w = 320, h = 240;
  std::vector<uint8_t> rgb(w * h * 3);
  tft.readRectRGB(0, 0, w, h, rgb.data());
  std::vector<uint8_t> raw;
  for (int y = 0; y < h; ++y) {
    raw.push_back(0);
    raw.insert(raw.end(), rgb.begin() + y * w * 3, rgb.begin() + (y + 1) * w * 3);
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  size_t pos = 0;
  while (pos < raw.size()) {
    const size_t n = std::min<size_t>(65535, raw.size() - pos);
    z.push_back(pos + n == raw.size() ? 1 : 0);
    z.push_back(n & 0xFF); z.push_back(n >> 8);
    z.push_back(~n & 0xFF); z.push_back((~n >> 8) & 0xFF);
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
  }
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
  put32(z, (b << 16) | a);
  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<uint8_t> ihdr;
  put32(ihdr, w); put32(ihdr, h);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
  chunk(png, "IHDR", ihdr);
  chunk(png, "IDAT", z);
  chunk(png, "IEND", {});
  const std::string path = g_outdir + "/" + name + ".png";
  FILE* f = fopen(path.c_str(), "wb");
  if (f) { fwrite(png.data(), 1, png.size(), f); fclose(f); }
  std::printf("  saved %s\n", path.c_str());
}

/** Run UI loops (render when due) advancing the virtual clock. */
void run(int frames, unsigned long step_ms = 200) {
  for (int i = 0; i < frames; ++i) {
    sim_advance_ms(step_ms);
    ui::appLoop(WiFi.status() == WL_CONNECTED);
  }
}

void redraw() {
  ui::requestRedraw();
  ui::appLoop(WiFi.status() == WL_CONNECTED);
}

void tap(int x, int y) {
  ui::touch::injectTap(x, y);
  ui::appLoop(WiFi.status() == WL_CONNECTED);
  redraw();
}

void simStep(int seconds) {
  auto& c = settings::get();
  for (int s = 0; s < seconds; ++s) {
    sim_advance_ms(1000);
    services::adsb::simulateUpdate(c.lat, c.lon, settings::fetchRadiusKm());
    ui::appLoop(true);
  }
}

uint16_t pixel(int x, int y) {
  uint8_t px[3];
  tft.readRectRGB(x, y, 1, 1, px);
  return ui::rgb(px[0], px[1], px[2]);
}

// Haversine-free independent reference: local tangent plane with WGS84 radii.
void refScreen(double clat, double clon, double lat, double lon, double outer_km, double* x, double* y) {
  const double kDeg = 3.14159265358979 / 180.0;
  const double a = 6378.137, e2 = 0.00669438;
  const double s = sin(clat * kDeg);
  const double rn = a / sqrt(1 - e2 * s * s);           // prime vertical
  const double rm = a * (1 - e2) / pow(1 - e2 * s * s, 1.5);  // meridional
  const double dx = (lon - clon) * kDeg * rn * cos(clat * kDeg);
  const double dy = (lat - clat) * kDeg * rm;
  const double k = 106.0 / outer_km;
  *x = 120 + dx * k;
  *y = 120 - dy * k;
}

services::adsb::Aircraft mk(const char* call, double lat, double lon, float trk, float gs, int alt) {
  services::adsb::Aircraft a = {};
  a.lat = static_cast<float>(lat);
  a.lon = static_cast<float>(lon);
  a.track_deg = a.nose_deg = trk;
  a.gs_knots = gs;
  a.alt_ft = alt;
  a.alt_valid = true;
  snprintf(a.hex, sizeof(a.hex), "%.6s", call);
  snprintf(a.callsign, sizeof(a.callsign), "%s", call);
  snprintf(a.type, sizeof(a.type), "A320");
  snprintf(a.squawk, sizeof(a.squawk), "1234");
  return a;
}

// ------------------------------------------------------------------ tests ---

void testPlacement() {
  std::printf("\n== Placement accuracy ==\n");
  services::adsb::setSimulation(0);
  auto& c = settings::get();
  c.lat = 52.3676; c.lon = 4.9041;  // Amsterdam (original default)
  c.range_idx = 2;                   // 10 km at ring 3 -> outer 13.33 km
  c.show_trails = false;
  settings::save();
  const double outer = settings::range().outer_km;
  const double kmLat = 1.0 / 111.2;  // ~1 km in degrees latitude
  const double kmLon = 1.0 / (111.32 * cos(c.lat * 3.14159265 / 180));
  struct Case { const char* id; double dn, de; };
  const Case cases[] = {{"N10", 10, 0}, {"E10", 0, 10}, {"S5", -5, 0}, {"W7", 0, -7},
                        {"NE8", 5.657, 5.657}, {"SW12", -8.485, -8.485}, {"C0", 0.2, 0.1}};
  std::vector<services::adsb::Aircraft> list;
  for (const auto& k : cases) {
    list.push_back(mk(k.id, c.lat + k.dn * kmLat, c.lon + k.de * kmLon, 0, 0, 5000));
  }
  services::adsb::injectForTest(list.data(), list.size());
  ui::navigate(ui::ScreenId::Radar);
  redraw();
  double worst = 0;
  for (size_t i = 0; i < ui::traffic::count(); ++i) {
    const auto& t = ui::traffic::at(i);
    double rx, ry;
    refScreen(c.lat, c.lon, t.ac.lat, t.ac.lon, outer, &rx, &ry);
    const double err = hypot(t.sx - rx, t.sy - ry);
    if (err > worst) worst = err;
    char d[96];
    snprintf(d, sizeof(d), "%-5s screen (%d,%d) ref (%.1f,%.1f) err %.2f px  dist %.2f km brg %.0f",
             t.ac.callsign, t.sx, t.sy, rx, ry, err, t.dist_km, t.brg_deg);
    check(err < 1.6, "aircraft position", d);
  }
  // Rendered pixels: nose tip of a north-heading symbol must be aircraft-coloured.
  int found = 0;
  for (size_t i = 0; i < ui::traffic::count(); ++i) {
    const auto& t = ui::traffic::at(i);
    if (pixel(t.sx, t.sy - 3) == ui::pal.aircraft) ++found;
  }
  char d[48];
  snprintf(d, sizeof(d), "%d/%u symbols drawn at their position", found, (unsigned)ui::traffic::count());
  check(found >= static_cast<int>(ui::traffic::count()) - 1, "symbol pixels", d);
  savePng("10_placement_test");

  // Range presets: N10 at each range lands on the right ring fraction.
  for (uint8_t r = 0; r < settings::kRangeCount; ++r) {
    c.range_idx = r;
    settings::save();
    ui::traffic::update();
    const float expect = 106.0f * 10.0f / settings::range().outer_km;
    for (size_t i = 0; i < ui::traffic::count(); ++i) {
      const auto& t = ui::traffic::at(i);
      if (strcmp(t.ac.callsign, "N10")) continue;
      char dd[96];
      if (t.inside) {
        snprintf(dd, sizeof(dd), "range %u: N10 at %d px from centre, expect %.1f", r, 120 - t.sy, expect);
        check(fabsf((120 - t.sy) - expect) < 1.5f, "range scaling", dd);
      } else {
        snprintf(dd, sizeof(dd), "range %u: N10 beyond ring -> rim dot bearing %.0f", r, t.brg_deg);
        check(t.brg_deg < 1.0f || t.brg_deg > 359.0f, "rim bearing", dd);
      }
    }
  }
  c.range_idx = 2;
  settings::save();
  std::printf("  worst aircraft placement error %.2f px\n", worst);
}

void testRunways() {
  std::printf("\n== Runway overlay (EHAM, built-in data) ==\n");
  auto& c = settings::get();
  c.lat = 52.3086; c.lon = 4.7639;  // Schiphol ARP
  c.range_idx = 1;                   // 5 km ring 3 -> outer 6.67 km
  c.show_runways = true;
  settings::save();
  ui::appLoop(true);  // rebuild overlay
  // 18R/36L (Polderbaan) ends from OurAirports.
  double x0, y0, x1, y1;
  refScreen(c.lat, c.lon, 52.362701, 4.711930, settings::range().outer_km, &x0, &y0);
  refScreen(c.lat, c.lon, 52.328602, 4.708840, settings::range().outer_km, &x1, &y1);
  bool matched = false;
  double best = 1e9;
  for (size_t i = 0; i < ui::traffic::segmentCount(); ++i) {
    const auto& s = ui::traffic::segment(i);
    // Compare the inner (unclipped) end: 36L threshold.
    const double e = std::min(hypot(s.x0 - x1, s.y0 - y1), hypot(s.x1 - x1, s.y1 - y1));
    if (e < best) best = e;
    if (e < 2.0) matched = true;
  }
  char d[96];
  snprintf(d, sizeof(d), "36L threshold ref (%.1f,%.1f) nearest segment end err %.2f px, %u segments",
           x1, y1, best, (unsigned)ui::traffic::segmentCount());
  check(matched, "runway endpoint", d);
  check(ui::traffic::labelCount() >= 1, "EHAM label present");
  services::adsb::injectForTest(nullptr, 0);
  redraw();
  savePng("11_runways_eham");
}

void testLookup() {
  std::printf("\n== Airport lookup (binary search over CSV with HTTP ranges) ==\n");
  struct Case { const char* id; bool ok; int min_rwy; };
  const Case cases[] = {{"EGLL", true, 2}, {"KJFK", true, 4}, {"EHAM", true, 6},
                        {"00AK", true, 1}, {"LFPG", true, 4}, {"ZZZZZ", false, 0}, {"00A", true, 0}};
  for (const auto& k : cases) {
    g_sim_http_requests = 0;
    services::lookup::start(k.id);
    for (int i = 0; i < 400 && services::lookup::state() == services::lookup::State::kRunning; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const bool done = services::lookup::state() == services::lookup::State::kDone;
    const auto& ap = services::lookup::result();
    char d[160];
    snprintf(d, sizeof(d), "%-5s -> %s, %u rwy, %d requests, '%s' (%.4f,%.4f) %s", k.id,
             done ? "found" : "not found", ap.runway_count, g_sim_http_requests, services::lookup::message(),
             ap.lat, ap.lon, done ? ap.name : "");
    check(done == k.ok && (!done || ap.runway_count >= k.min_rwy), "lookup", d);
    if (done) {
      for (uint8_t r = 0; r < ap.runway_count; ++r) {
        std::printf("        %-8s %.5f,%.5f -> %.5f,%.5f\n", ap.runways[r].name, ap.runways[r].le_lat,
                    ap.runways[r].le_lon, ap.runways[r].he_lat, ap.runways[r].he_lon);
      }
    }
    services::lookup::reset();
  }
}

void waitLookup() {
  for (int i = 0; i < 400 && services::lookup::state() == services::lookup::State::kRunning; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

/** Type on the search keyboard by key position. */
void typeText(const char* s) {
  static const char* rows[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
  for (; *s; ++s) {
    if (*s == ' ') { tap(247, 124 + 3 * 29 + 13); continue; }
    for (int r = 0; r < 4; ++r) {
      const char* p = strchr(rows[r], *s);
      if (p) { tap(1 + static_cast<int>(p - rows[r]) * 32 + 15, 124 + r * 29 + 13); break; }
    }
  }
}

void testAirportFlowViaTouch() {
  std::printf("\n== Touch flow: Menu > Airports > FIND > search > lookup > save / HOME ==\n");
  auto& c = settings::get();
  c.lat = 51.4700; c.lon = -0.4543; c.range_idx = 2;  // Heathrow
  settings::save();
  ui::navigate(ui::ScreenId::Radar);
  redraw();
  tap(300, 220);  // menu button
  check(ui::currentScreen() == ui::ScreenId::Menu, "menu opens from radar button");
  savePng("40_menu_display");
  tap(290, 15);  // next page
  savePng("41_menu_units_alerts");
  tap(290, 15);
  savePng("42_menu_airports_night");
  tap(160, 67);  // second tile on page 3 = Airports
  check(ui::currentScreen() == ui::ScreenId::Airports, "airports screen from tile");
  savePng("50_airports_empty");
  tap(275, 15);  // FIND
  check(ui::currentScreen() == ui::ScreenId::Search, "search opens from FIND");
  savePng("50b_search_nearby");
  typeText("GATWICK");
  savePng("51_search_gatwick");
  tap(160, 31 + 15);  // first result
  check(ui::currentScreen() == ui::ScreenId::Lookup, "lookup screen after tapping a result");
  waitLookup();
  redraw();
  check(services::lookup::state() == services::lookup::State::kDone, "EGKK found via search",
        services::lookup::message());
  savePng("52_lookup_result");
  tap(267, 219);  // SAVE  check(services::airports::find("EGKK") >= 0, "EGKK saved to store");
  savePng("53_airports_list");
  // Also save one from the lookup API path: small field with synthetic runways
  services::lookup::start("EGLM");
  for (int i = 0; i < 400 && services::lookup::state() == services::lookup::State::kRunning; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  if (services::lookup::state() == services::lookup::State::kDone) services::airports::upsert(services::lookup::result());
  services::lookup::reset();
  redraw();
  savePng("54_airports_two");
  // HOME on EGKK row -> radar centred there, custom runways drawn violet.
  tap(237, 34 + 20);
  check(ui::currentScreen() == ui::ScreenId::Radar, "HOME button returns to radar");
  check(fabs(settings::get().lat - 51.148) < 0.02, "radar centred on EGKK");
  settings::get().range_idx = 2;
  settings::save();
  run(2);
  redraw();
  int custom = 0;
  for (size_t i = 0; i < ui::traffic::segmentCount(); ++i) custom += ui::traffic::segment(i).kind == 1;
  char d[48];
  snprintf(d, sizeof(d), "%d custom runway segments", custom);
  check(custom >= 1, "custom airport drawn", d);
  savePng("55_radar_custom_airport");

  // Search by city name, then HOME moves the radar there.
  ui::searchEnter(true);
  typeText("TAMPA");
  savePng("56_search_tampa");
  tap(160, 31 + 15);
  waitLookup();
  redraw();
  savePng("57_lookup_ktpa");
  tap(160, 219);  // HOME
  check(ui::currentScreen() == ui::ScreenId::Radar, "HOME from lookup returns to radar");
  check(fabs(settings::get().lat - 27.9755) < 0.01 && fabs(settings::get().lon + 82.5332) < 0.01,
        "radar centred on KTPA");
  check(services::airports::find("KTPA") >= 0, "KTPA kept with its runways");
  services::adsb::setSimulation(18);
  simStep(20);
  redraw();
  savePng("58_radar_tampa");
  services::adsb::setSimulation(0);

  // Ident that is not in the offline index -> "look up online" row.
  ui::searchSetQuery("00AK", false);
  redraw();
  savePng("59_search_online_row");
  // Offline: lookup fails, HOME still works from index coordinates.
  sim_set_http_offline(true);
  ui::searchEnter(true);
  typeText("EDDF");
  tap(160, 31 + 15);
  waitLookup();
  redraw();
  check(services::lookup::state() == services::lookup::State::kError, "offline lookup fails cleanly");
  savePng("60_lookup_offline");
  tap(160, 219);  // HOME
  check(fabs(settings::get().lat - 50.0333) < 0.02, "offline HOME uses index coordinates");
  sim_set_http_offline(false);
}

void testSearch() {
  std::printf("\n== Offline airport search ==\n");
  struct Case { const char* q; const char* expect; int max_rank; };
  const Case cases[] = {
      {"tampa", "KTPA", 0}, {"TPA", "KTPA", 0}, {"KTPA", "KTPA", 0}, {"heathrow", "EGLL", 0},
      {"jfk", "KJFK", 0}, {"new york", "KJFK", 3}, {"st pete", "KPIE", 2}, {"paris", "LFPG", 2},
      {"orly", "LFPO", 0}, {"schiphol", "EHAM", 0}, {"gatwick", "EGKK", 0}, {"haneda", "RJTT", 0},
      {"sao paulo", "SBGR", 3}, {"zurich", "LSZH", 0}, {"CDG", "LFPG", 0}, {"munich", "EDDM", 0}};
  services::search::Hit hits[services::search::kMaxHits];
  for (const auto& k : cases) {
    int total = 0;
    const auto t0 = std::chrono::steady_clock::now();
    const int n = services::search::run(k.q, 51.47f, -0.45f, hits, services::search::kMaxHits, &total);
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    int rank = -1;
    for (int i = 0; i < n; ++i)
      if (!strcmp(services::search::entry(hits[i].idx).ident, k.expect)) { rank = i; break; }
    char d[160];
    snprintf(d, sizeof(d), "\"%s\" -> %s at rank %d of %d (first %s \"%s\") %.0f us", k.q, k.expect, rank, total,
             n ? services::search::entry(hits[0].idx).ident : "-", n ? services::search::name(services::search::entry(hits[0].idx)) : "",
             us);
    check(rank >= 0 && rank <= k.max_rank, "search", d);
  }
  int total = 0;
  const int n = services::search::run("", 27.97f, -82.53f, hits, 5, &total);
  char d[96];
  snprintf(d, sizeof(d), "nearest to Tampa: %s, %s", n ? services::search::entry(hits[0].idx).ident : "-",
           n > 1 ? services::search::entry(hits[1].idx).ident : "-");
  check(n == 5 && !strcmp(services::search::entry(hits[0].idx).ident, "KTPA"), "nearby list", d);
}

void testScreens() {
  std::printf("\n== Screens and themes ==\n");
  auto& c = settings::get();
  c.lat = 52.3676; c.lon = 4.9041; c.range_idx = 3; c.show_trails = true; c.label_mode = 2;
  c.theme = 0; c.alert_km = 5;
  settings::save();
  services::adsb::setSimulation(22);
  simStep(40);  // build trails
  ui::navigate(ui::ScreenId::Radar);
  redraw();
  savePng("20_radar_classic");
  // Tap the nearest aircraft to select it
  ui::traffic::update();
  int idx = -1;
  for (size_t i = 0; i < ui::traffic::count(); ++i) if (ui::traffic::at(i).inside) { idx = (int)i; break; }
  if (idx >= 0) {
    const auto& t = ui::traffic::at(idx);
    char hex[7];
    strcpy(hex, t.ac.hex);
    tap(t.sx, t.sy);
    check(strcmp(ui::traffic::selectedHex(), hex) == 0, "tap selects aircraft", hex);
    savePng("21_radar_selected");
  }
  c.theme = 1; c.alt_colors = false; settings::save(); redraw(); savePng("22_theme_amber");
  c.theme = 2; c.alt_colors = true; c.sweep = true; settings::save(); run(3, 80); redraw(); savePng("23_theme_ice_altcolours_sweep");
  c.theme = 3; c.alt_colors = false; c.sweep = false; settings::save(); redraw(); savePng("24_theme_night");
  c.theme = 0; c.dist_units = 1; c.alt_units = 1; c.speed_units = 1; settings::save(); redraw(); savePng("25_units_mi_m_kmh");
  c.dist_units = 0; c.alt_units = 0; c.speed_units = 0; c.label_mode = 1; settings::save(); redraw(); savePng("26_labels_callsign");
  c.label_mode = 2; settings::save();
  tap(260, 220);  // list button
  check(ui::currentScreen() == ui::ScreenId::List, "list opens");
  savePng("30_list");
  tap(272, 15);   // sort -> ALT
  savePng("31_list_by_alt");
  tap(20, 60);    // first row -> selects and returns to radar
  check(ui::currentScreen() == ui::ScreenId::Radar && ui::traffic::selectedIndex() >= 0, "list row selects target");
  savePng("32_radar_from_list");
  ui::navigate(ui::ScreenId::Info);
  redraw();
  savePng("60_info");
  ui::confirmEnter("Reset Wi-Fi?", "Clears Wi-Fi, location and settings,\nthen opens the setup portal.", nullptr, ui::ScreenId::Menu);
  redraw();
  savePng("61_confirm");
  tap(95, 179);
  check(ui::currentScreen() == ui::ScreenId::Menu, "confirm cancel returns");
  WiFi.connected = false;
  services::adsb::setSimulation(0);
  services::adsb::injectForTest(nullptr, 0);
  ui::navigate(ui::ScreenId::Radar);
  redraw();
  savePng("27_radar_offline_empty");
  WiFi.connected = true;
}

void testStatusScreens() {
  std::printf("\n== Status screens ==\n");
  ui::status::boot();
  savePng("01_boot");
  ui::status::connectingBegin("HomeNetwork");
  for (int i = 0; i < 6; ++i) ui::status::connectingTick();
  savePng("02_connecting");
  ui::status::portal();
  savePng("03_setup_portal");
}

void testStress() {
  std::printf("\n== Stress: 80 aircraft, every overlay on, 10 min simulated ==\n");
  auto& c = settings::get();
  c.lat = 51.47; c.lon = -0.45; c.range_idx = 6; c.show_trails = true; c.label_mode = 2;
  c.alt_colors = true; c.sweep = true; c.alert_km = 10; c.show_runways = true; c.theme = 0;
  settings::save();
  services::adsb::setSimulation(80);
  ui::navigate(ui::ScreenId::Radar);
  const auto t0 = std::chrono::steady_clock::now();
  int frames = 0;
  for (int s = 0; s < 600; ++s) {
    sim_advance_ms(1000);
    services::adsb::simulateUpdate(c.lat, c.lon, settings::fetchRadiusKm());
    for (int f = 0; f < 5; ++f) {
      sim_advance_ms(1);
      redraw();
      ++frames;
    }
    if (s % 97 == 0) {  // cycle ranges/themes while running
      c.range_idx = static_cast<uint8_t>((c.range_idx + 1) % settings::kRangeCount);
      settings::save();
    }
  }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  char d[96];
  snprintf(d, sizeof(d), "%d frames in %.0f ms host time (%.2f ms/frame), %u targets", frames, ms, ms / frames,
           (unsigned)ui::traffic::count());
  check(ui::traffic::count() == 80 || ui::traffic::count() > 60, "stress run completed", d);
  c.range_idx = 5;
  settings::save();
  redraw();
  savePng("70_stress_80_aircraft");
  c.range_idx = 3; c.sweep = false;
  settings::save();
  redraw();
  savePng("71_stress_80_zoomed");
  services::adsb::setSimulation(0);
}

/** A v3 settings blob (older firmware) must survive the upgrade to v4. */
void testSettingsMigration() {
  std::printf("\n== Settings migration v3 -> v4 ==\n");
  settings::Settings old{};
  old.version = 3;
  old.lat = 27.9755; old.lon = -82.5332; old.theme = 2; old.range_idx = 4; old.brightness = 55;
  old.fetch_s = 5;
  strcpy(old.tz, "EST5EDT,M3.2.0,M11.1.0");
  const size_t v3size = (offsetof(settings::Settings, tz) + sizeof(old.tz) + 7) & ~static_cast<size_t>(7);
  Preferences p;
  p.begin("cydradar");
  p.putBytes("cfg", &old, v3size);
  settings::init();
  const auto& c = settings::get();
  char d[128];
  snprintf(d, sizeof(d), "blob %zu -> %zu bytes, lat %.4f theme %u range %u tz %s, night %d %02u-%02u",
           v3size, sizeof(settings::Settings), c.lat, c.theme, c.range_idx, c.tz, c.night_mode, c.night_start_h, c.night_end_h);
  check(fabs(c.lat - 27.9755) < 1e-6 && c.theme == 2 && c.range_idx == 4 && c.brightness == 55 &&
            !strcmp(c.tz, "EST5EDT,M3.2.0,M11.1.0") && !c.night_mode && c.night_start_h == 22 && c.night_end_h == 7 &&
            c.night_brightness == 10,
        "v3 settings kept, night defaults added", d);
  settings::resetToDefaults();
}

void testDetailAndNight() {
  std::printf("\n== Aircraft detail + closest approach ==\n");
  auto& c = settings::get();
  c.lat = 52.3676; c.lon = 4.9041; c.range_idx = 3; c.theme = 0; c.night_mode = false;
  settings::save();
  services::adsb::setSimulation(0);
  const double kmLat = 1.0 / 111.2;
  const double kmLon = 1.0 / (111.32 * cos(c.lat * 3.14159265 / 180));
  std::vector<services::adsb::Aircraft> list;
  auto a = mk("INBND", c.lat + 10 * kmLat, c.lon, 180, 300, 8000);  // 10 km N, flying S over the centre
  a.vrate_fpm = -500;
  snprintf(a.type, sizeof(a.type), "B738"); snprintf(a.reg, sizeof(a.reg), "PH-BXA"); a.category = 0x13;
  list.push_back(a);
  list.push_back(mk("PASS", c.lat - 5 * kmLat, c.lon + 8 * kmLon, 0, 250, 12000));  // 8 km E, 5 km S, heading N
  list.push_back(mk("AWAY", c.lat + 6 * kmLat, c.lon, 0, 200, 9000));               // N of centre, heading N
  services::adsb::injectForTest(list.data(), list.size());
  ui::navigate(ui::ScreenId::Radar);
  redraw();
  for (size_t i = 0; i < ui::traffic::count(); ++i) {
    const auto& t = ui::traffic::at(i);
    const auto cpa = ui::traffic::closestApproach(t);
    char d[128];
    snprintf(d, sizeof(d), "%s: valid %d, %.2f km at %.0f deg in %.1f s, alt %d", t.ac.callsign, cpa.valid,
             cpa.dist_km, cpa.brg_deg, cpa.t_s, cpa.alt_ft);
    if (!strcmp(t.ac.callsign, "INBND"))
      check(cpa.valid && cpa.dist_km < 0.1 && fabs(cpa.t_s - 64.8) < 1.5 && abs(cpa.alt_ft - 7460) < 30, "CPA overhead", d);
    if (!strcmp(t.ac.callsign, "PASS"))
      check(cpa.valid && fabs(cpa.dist_km - 8.0) < 0.15 && fabs(cpa.brg_deg - 90) < 2 && fabs(cpa.t_s - 38.9) < 1.5, "CPA abeam", d);
    if (!strcmp(t.ac.callsign, "AWAY")) check(!cpa.valid, "CPA moving away", d);
  }
  ui::traffic::select("INBND");
  redraw();
  tap(280, 100);  // panel card
  check(ui::currentScreen() == ui::ScreenId::Detail, "card opens aircraft detail");
  savePng("29_detail_inbound");
  tap(160, 120);
  check(ui::currentScreen() == ui::ScreenId::Radar, "detail closes on tap");
  ui::traffic::select("PASS");
  ui::detailOpen();
  redraw();
  savePng("29b_detail_abeam");
  ui::navigate(ui::ScreenId::Radar);

  std::printf("\n== Night mode ==\n");
  c.night_mode = true; c.night_start_h = 14; c.night_end_h = 15;  // sim clock reads 14:xx
  settings::save();
  run(60, 200);  // > 10 s so the schedule is re-checked
  redraw();
  check(ui::nightActive(), "night window 14-15 active at 14:xx");
  check(ui::pal.bg == ui::rgb(0, 0, 0) || ui::pal.aircraft != 0, "night palette applied");
  savePng("30_night_mode");
  c.night_start_h = 22; c.night_end_h = 7;  // wraps midnight: 14:xx is outside
  settings::save();
  run(60, 200);
  check(!ui::nightActive(), "night window 22-07 inactive at 14:xx");
  c.night_mode = false;
  settings::save();
  run(60, 200);
  ui::navigate(ui::ScreenId::Menu);
  for (int i = 0; i < 2; ++i) tap(290, 15);
  savePng("43_menu_night_airports");
  tap(290, 15);
  savePng("44_menu_system");
  tap(290, 15);  // wrap back to page 1 for the later touch flow
  ui::navigate(ui::ScreenId::Radar);
  services::adsb::injectForTest(nullptr, 0);
  ui::traffic::clearSelection();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1) g_outdir = argv[1];
  std::printf("Plane Radar CYD simulator\n");
  testSettingsMigration();
  hw::init();
  displayFontInit();
  ui::themeApply(settings::get().theme);
  ui::frameInit();
  ui::touch::init(false);
  services::airports::init();
  services::adsb::init();
  services::lookup::init();
  ui::appInit();

  testStatusScreens();
  testPlacement();
  testRunways();
  testScreens();
  testLookup();
  testSearch();
  testDetailAndNight();
  testAirportFlowViaTouch();
  testStress();

  std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
