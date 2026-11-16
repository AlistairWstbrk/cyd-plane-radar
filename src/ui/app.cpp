#include "ui/app.h"

#include <Arduino.h>

#include <ctime>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/airport_store.h"
#include "services/settings.h"
#include "ui/theme.h"
#include "ui/traffic.h"

namespace ui {

void radarSetOnline(bool online);

namespace {

ScreenId s_screen = ScreenId::Radar;
bool s_dirty = true;
unsigned long s_last_frame = 0;
unsigned long s_last_traffic = 0;
uint32_t s_airport_rev = 0;
unsigned long s_feed_seen = 0;
unsigned long s_led_pulse_until = 0;
bool s_last_online = false;
bool s_night = false;
unsigned long s_night_checked = 0;
uint8_t s_theme_applied = 255;
uint32_t s_frame_count = 0;
unsigned long s_frame_ms_total = 0;

using DrawFn = void (*)(Canvas&, void*);
using TouchFn = void (*)(const touch::Event&);

struct ScreenDef {
  DrawFn draw;
  TouchFn touch;
};

ScreenDef def(ScreenId id) {
  switch (id) {
    case ScreenId::List: return {listDraw, listTouch};
    case ScreenId::Menu: return {menuDraw, menuTouch};
    case ScreenId::Airports: return {airportsDraw, airportsTouch};
    case ScreenId::Search: return {searchDraw, searchTouch};
    case ScreenId::Lookup: return {lookupDraw, lookupTouch};
    case ScreenId::Info: return {infoDraw, infoTouch};
    case ScreenId::Guide: return {guideDraw, guideTouch};
    case ScreenId::Confirm: return {confirmDraw, confirmTouch};
    case ScreenId::Detail: return {detailDraw, detailTouch};
    default: return {radarDraw, radarTouch};
  }
}

void updateLed(bool online) {
  const auto& cfg = settings::get();
  const unsigned long now = millis();
  if (cfg.alert_led && traffic::alertCount() > 0) {
    const bool on = (now / 250) % 2 == 0;
    hw::led(on ? 255 : 0, 0, 0);
    return;
  }
  if (!cfg.status_led) {
    hw::led(0, 0, 0);
    return;
  }
  if (!online && services::adsb::simulationCount() == 0) {
    const bool on = (now % 3000) < 120;
    hw::led(on ? 120 : 0, on ? 40 : 0, 0);
    return;
  }
  if (now < s_led_pulse_until) hw::led(0, 90, 20);
  else hw::led(0, 0, 0);
}

/** Night window check against local time (needs NTP; off until synced). */
bool computeNight() {
  const auto& cfg = settings::get();
  if (!cfg.night_mode || cfg.night_start_h == cfg.night_end_h) return false;
  struct tm tmv;
  if (!getLocalTime(&tmv, 0) || tmv.tm_year < 120) return false;
  const int h = tmv.tm_hour, s = cfg.night_start_h, e = cfg.night_end_h;
  return s < e ? (h >= s && h < e) : (h >= s || h < e);
}

uint8_t effectiveTheme() {
  const auto& cfg = settings::get();
  return (s_night && cfg.night_theme) ? settings::kThemeNight : cfg.theme;
}

void applyTheme() {
  s_theme_applied = effectiveTheme();
  themeApply(s_theme_applied);
}

}  // namespace

bool nightActive() { return s_night; }

void appInit() {
  applyTheme();
  traffic::rebuildOverlay();
  s_airport_rev = services::airports::revision();
}

void requestRedraw() { s_dirty = true; }

ScreenId currentScreen() { return s_screen; }

void navigate(ScreenId id) {
  if (id == ScreenId::List) listEnter();
  if (id == ScreenId::Menu) menuEnter();
  if (id == ScreenId::Airports) airportsEnter();
  if (id == ScreenId::List || id == ScreenId::Radar) traffic::update();
  s_screen = id;
  s_dirty = true;
}

void onBootTap() {
  // Original control: BOOT tap cycles the range preset.
  settings::rangeNext();
  if (s_screen != ScreenId::Radar) navigate(ScreenId::Radar);
  s_dirty = true;
}

void appLoop(bool online) {
  const auto& cfg = settings::get();
  const unsigned long now = millis();
  if (now - s_night_checked >= 10000 || s_night_checked == 0) {
    s_night_checked = now;
    s_night = computeNight();
  }
  if (s_night) {
    hw::setBacklightPercent(cfg.night_brightness < cfg.brightness ? cfg.night_brightness : cfg.brightness);
  } else {
    hw::setBacklightPercent(cfg.brightness);
  }

  if (settings::consumeChanged() || services::airports::revision() != s_airport_rev) {
    s_airport_rev = services::airports::revision();
    applyTheme();
    traffic::rebuildOverlay();
    s_dirty = true;
  }
  if (effectiveTheme() != s_theme_applied) {
    applyTheme();
    traffic::rebuildOverlay();
    s_dirty = true;
  }
  if (online != s_last_online) {
    s_last_online = online;
    s_dirty = true;
  }
  radarSetOnline(online);

  const touch::Event e = touch::poll();
  if (e.type != touch::Type::None) def(s_screen).touch(e);

  // New feed data: LED pulse + alert check.
  const auto st = services::adsb::status();
  if (st.last_ok_ms != s_feed_seen) {
    s_feed_seen = st.last_ok_ms;
    s_led_pulse_until = now + 120;
    traffic::update();
    traffic::consumeNewAlert();
    if (s_screen == ScreenId::List || s_screen == ScreenId::Detail) s_dirty = true;
  }
  updateLed(online);

  bool due = s_dirty;
  if (s_screen == ScreenId::Radar) {
    const unsigned long interval = cfg.sweep ? config::kRadarFrameSweepMs : config::kRadarFrameMs;
    if (now - s_last_frame >= interval) due = true;
  } else if (s_screen == ScreenId::Detail) {
    if (now - s_last_frame >= 1000) due = true;
  } else if (s_screen == ScreenId::Lookup) {
    if (lookupTick()) due = true;
  } else if (s_screen == ScreenId::Info) {
    if (now - s_last_frame >= 5000) due = true;
  }
  if (screenshotPending()) due = true;
  if (!due) return;

  if (s_screen == ScreenId::Radar ||
      ((s_screen == ScreenId::List || s_screen == ScreenId::Detail) && now - s_last_traffic > 1000)) {
    traffic::update();
    s_last_traffic = now;
  }
  const unsigned long t0 = millis();
  renderFrame(def(s_screen).draw, nullptr);
  s_frame_ms_total += millis() - t0;
  if (++s_frame_count == 50) {
    Serial.printf("ui: %lu ms/frame avg, %u targets, heap %u (min %u)\n", s_frame_ms_total / 50,
                  static_cast<unsigned>(traffic::count()), ESP.getFreeHeap(), ESP.getMinFreeHeap());
    s_frame_count = 0;
    s_frame_ms_total = 0;
  }
  s_last_frame = now;
  s_dirty = false;
}

}  // namespace ui
