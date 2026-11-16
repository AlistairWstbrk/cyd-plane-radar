/**
 * Plane Radar - CYD edition.
 * Live ADS-B radar for the ESP32-2432S028R "Cheap Yellow Display".
 * Based on MatixYo/ESP32-Plane-Radar (MIT).
 */

#include <Arduino.h>
#include <WiFi.h>

#include <cstdlib>
#include <cstring>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/adsb_client.h"
#include "services/airport_lookup.h"
#include "services/airport_store.h"
#include "services/device_code.h"
#include "services/settings.h"
#include "services/wifi_setup.h"
#include "ui/app.h"
#include "ui/canvas.h"
#include "ui/status_screens.h"
#include "ui/theme.h"
#include "ui/touch.h"
#include "ui/traffic.h"

void applyTimezone() {
  configTzTime(settings::get().tz, "pool.ntp.org", "time.google.com");
}

namespace {

unsigned long s_wifi_down_since = 0;
unsigned long s_last_reconnect_ms = 0;
bool s_time_configured = false;

void adsbTask(void*) {
  for (;;) {
    const auto& cfg = settings::get();
    if (services::adsb::simulationCount() > 0) {
      services::adsb::simulateUpdate(cfg.lat, cfg.lon, settings::fetchRadiusKm());
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if (WiFi.status() == WL_CONNECTED) {
      services::adsb::fetchUpdate(cfg.lat, cfg.lon, settings::fetchRadiusKm(), cfg.show_ground);
    }
    unsigned long wait = cfg.fetch_s * 1000UL;
    if (wait < config::kAdsbMinFetchIntervalMs) wait = config::kAdsbMinFetchIntervalMs;
    // Sleep in slices so a range/location change triggers a quick refetch.
    const uint8_t range = cfg.range_idx;
    const double lat = cfg.lat;
    for (unsigned long t = 0; t < wait; t += 250) {
      vTaskDelay(pdMS_TO_TICKS(250));
      if (settings::get().range_idx != range || settings::get().lat != lat ||
          services::adsb::simulationCount() > 0) {
        if (t >= 1000) break;  // keep >= 1 s between requests (API limit)
      }
    }
  }
}

// ---------------------------------------------------------------- serial ---
// Test harness: "help" lists commands. Used for screenshots and stress tests.

char s_line[96];
size_t s_line_len = 0;

void printHelp() {
  Serial.println(
      "Commands:\n"
      "  code                 print the setup Wi-Fi password / web login\n"
      "  shot                 screenshot (binary, see tools/screenshot.py)\n"
      "  tap X Y              inject a touch tap\n"
      "  screen NAME          radar|list|menu|airports|info|search\n"
      "  sim N                N simulated aircraft (0 = live)\n"
      "  range I              range preset index\n"
      "  loc LAT LON          set radar centre\n"
      "  set KEY V            theme|labels|trails|runways|sweep|altc|ground|alert|units|alt|speed|flip|invert|bright\n"
      "  select I             select target by list index (-1 clears)\n"
      "  find TEXT            airport search screen with query (empty = nearby)\n"
      "  lookup IDENT         online airport lookup (result printed)\n"
      "  airports             list custom airports\n"
      "  stats                heap / feed / ui stats\n"
      "  panel                read panel ID registers\n"
      "  cal                  run touch calibration\n"
      "  reboot");
}

void cmdSet(const char* key, int v) {
  auto& c = settings::get();
  if (!strcmp(key, "theme")) c.theme = v % settings::kThemeCount;
  else if (!strcmp(key, "labels")) c.label_mode = v % 3;
  else if (!strcmp(key, "trails")) c.show_trails = v;
  else if (!strcmp(key, "runways")) c.show_runways = v;
  else if (!strcmp(key, "sweep")) c.sweep = v;
  else if (!strcmp(key, "altc")) c.alt_colors = v;
  else if (!strcmp(key, "ground")) c.show_ground = v;
  else if (!strcmp(key, "alert")) c.alert_km = v;
  else if (!strcmp(key, "units")) c.dist_units = v % 3;
  else if (!strcmp(key, "alt")) c.alt_units = v % 2;
  else if (!strcmp(key, "speed")) c.speed_units = v % 3;
  else if (!strcmp(key, "bright")) { c.brightness = v; }
  else if (!strcmp(key, "flip")) { c.flip = v; hw::applyOrientation(c.flip, c.invert); }
  else if (!strcmp(key, "invert")) { c.invert = v; hw::applyOrientation(c.flip, c.invert); }
  else { Serial.printf("unknown key %s\n", key); return; }
  settings::save();
  ui::requestRedraw();
  Serial.printf("ok %s=%d\n", key, v);
}

void runCommand(char* line) {
  char* cmd = strtok(line, " ");
  if (cmd == nullptr) return;
  char* a1 = strtok(nullptr, " ");
  char* a2 = strtok(nullptr, " ");
  if (!strcmp(cmd, "help")) printHelp();
  else if (!strcmp(cmd, "code")) Serial.printf("login %s / %s\n", services::device_code::kLoginUser, services::device_code::get());
  else if (!strcmp(cmd, "shot")) { ui::requestScreenshot(); ui::requestRedraw(); }
  else if (!strcmp(cmd, "tap") && a1 && a2) { ui::touch::injectTap(atoi(a1), atoi(a2)); Serial.println("ok tap"); }
  else if (!strcmp(cmd, "sim") && a1) { services::adsb::setSimulation(atoi(a1)); Serial.printf("ok sim %d\n", atoi(a1)); }
  else if (!strcmp(cmd, "range") && a1) {
    const int i = atoi(a1);
    if (i >= 0 && i < static_cast<int>(settings::kRangeCount)) { settings::get().range_idx = i; settings::save(); }
    Serial.println("ok range");
  } else if (!strcmp(cmd, "loc") && a1 && a2) {
    Serial.println(settings::setLocationFromStrings(a1, a2) ? "ok loc" : "bad loc");
  } else if (!strcmp(cmd, "set") && a1 && a2) cmdSet(a1, atoi(a2));
  else if (!strcmp(cmd, "select") && a1) {
    const int i = atoi(a1);
    ui::traffic::update();
    if (i >= 0 && i < static_cast<int>(ui::traffic::count())) ui::traffic::select(ui::traffic::at(i).ac.hex);
    else ui::traffic::clearSelection();
    ui::requestRedraw();
    Serial.println("ok select");
  } else if (!strcmp(cmd, "screen") && a1) {
    using ui::ScreenId;
    if (!strcmp(a1, "radar")) ui::navigate(ScreenId::Radar);
    else if (!strcmp(a1, "list")) ui::navigate(ScreenId::List);
    else if (!strcmp(a1, "menu")) ui::navigate(ScreenId::Menu);
    else if (!strcmp(a1, "airports")) ui::navigate(ScreenId::Airports);
    else if (!strcmp(a1, "info")) ui::navigate(ScreenId::Info);
    else if (!strcmp(a1, "search")) ui::searchEnter(true);
    Serial.println("ok screen");
  } else if (!strcmp(cmd, "find")) {
    char q[24];
    snprintf(q, sizeof(q), "%s%s%s", a1 ? a1 : "", a2 ? " " : "", a2 ? a2 : "");
    ui::searchSetQuery(q, false);
    Serial.printf("ok find \"%s\"\n", q);
  } else if (!strcmp(cmd, "lookup") && a1) {
    Serial.println(services::lookup::start(a1) ? "ok lookup started" : "lookup busy/invalid");
    ui::navigate(ui::ScreenId::Lookup);
  } else if (!strcmp(cmd, "airports")) {
    for (size_t i = 0; i < services::airports::count(); ++i) {
      const auto& ap = services::airports::at(i);
      Serial.printf("  %s %.5f,%.5f %u rwy %s\n", ap.ident, ap.lat, ap.lon, ap.runway_count, ap.name);
    }
    Serial.println("ok airports");
  } else if (!strcmp(cmd, "stats")) {
    const auto st = services::adsb::status();
    Serial.printf("heap %u min %u maxblk %u | feed ok %lu err %lu http %d %ums | targets %u | wifi %d %s\n",
                  ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(),
                  static_cast<unsigned long>(st.ok_count), static_cast<unsigned long>(st.err_count),
                  st.last_http, st.fetch_ms, static_cast<unsigned>(ui::traffic::count()),
                  WiFi.status(), WiFi.localIP().toString().c_str());
  } else if (!strcmp(cmd, "panel")) {
    tft.startWrite();
    const uint32_t id04 = tft.getPanel()->readCommand(0x04, 1, 3);
    const uint32_t idd3 = tft.getPanel()->readCommand(0xD3, 1, 3);
    tft.endWrite();
    Serial.printf("RDDID(04)=%06lx  ID4(D3)=%06lx  (ILI9341 -> D3=..9341)\n",
                  static_cast<unsigned long>(id04), static_cast<unsigned long>(idd3));
  } else if (!strcmp(cmd, "cal")) { ui::touch::calibrate(); ui::requestRedraw(); }
  else if (!strcmp(cmd, "reboot")) ESP.restart();
  else Serial.printf("unknown: %s (try help)\n", cmd);
}

void pollSerial() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      s_line[s_line_len] = '\0';
      if (s_line_len > 0) runCommand(s_line);
      s_line_len = 0;
    } else if (s_line_len < sizeof(s_line) - 1) {
      s_line[s_line_len++] = static_cast<char>(c);
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\nPlane Radar CYD v%s\n", config::kFirmwareVersion);

  bootButtonInit();
  settings::init();
  hw::init();
  hw::applyOrientation(settings::get().flip, settings::get().invert);
  displayFontInit();
  ui::themeApply(settings::get().theme);
  if (!ui::frameInit()) Serial.println("ERROR: frame buffer allocation failed");
  ui::status::boot();

  // Hold a finger on the screen during boot to recalibrate touch.
  int32_t tx, ty;
  const bool force_cal = tft.getTouch(&tx, &ty) > 0;
  delay(force_cal ? 0 : 600);
  ui::touch::init(force_cal);

  services::airports::init();
  services::adsb::init();
  services::lookup::init();
  ui::appInit();

  if (wifiShowsSetupScreenOnBoot()) ui::status::portal();
  if (wifiSetupConnect()) {
    applyTimezone();
    s_time_configured = true;
  }

  xTaskCreatePinnedToCore(adsbTask, "adsb", 16384, nullptr, 1, nullptr, 0);
  ui::navigate(ui::ScreenId::Radar);
  Serial.println("Type 'help' for test commands.");
}

void loop() {
  pollSerial();
  bootButtonPollLongPress();
  if (bootButtonConsumeTap()) ui::onBootTap();
  wifiLoop();

  const bool online = WiFi.status() == WL_CONNECTED;
  if (online) {
    s_wifi_down_since = 0;
    if (!s_time_configured) {
      applyTimezone();
      s_time_configured = true;
    }
  } else {
    if (s_wifi_down_since == 0) s_wifi_down_since = millis();
    if (millis() - s_wifi_down_since >= config::kWifiDownGraceMs &&
        millis() - s_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      s_last_reconnect_ms = millis();
      Serial.println("WiFi down - reconnecting in background");
      WiFi.reconnect();
    }
  }

  ui::appLoop(online);
  delay(5);
}
