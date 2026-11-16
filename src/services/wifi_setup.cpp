// Wi-Fi provisioning with WiFiManager. Flow and BOOT-button behaviour are kept
// from MatixYo/ESP32-Plane-Radar (MIT); the portal is restyled and extended
// with Radar settings / Airports / Live pages (see web_portal.cpp).
#include "services/wifi_setup.h"

#include <WiFi.h>
#include <WiFiManager.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include <Preferences.h>
#include <esp_system.h>
#include <esp_wifi.h>

#ifdef WM_MDNS
#include <ESPmDNS.h>
#endif

#include "config.h"
#include "services/device_code.h"
#include "services/airport_search.h"
#include "services/settings.h"
#include "services/web_portal.h"
#include "ui/status_screens.h"

namespace {

portMUX_TYPE s_boot_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_boot_tap_pending = false;
volatile bool s_boot_is_down = false;
volatile unsigned long s_boot_down_ms = 0;
bool s_long_press_handled = false;
bool s_boot_interrupt_attached = false;

void IRAM_ATTR onBootButtonIsr() {
  const bool down = digitalRead(config::kBootPin) == LOW;
  const unsigned long now = millis();
  portENTER_CRITICAL_ISR(&s_boot_mux);
  if (down) {
    s_boot_is_down = true;
    s_boot_down_ms = now;
  } else if (s_boot_is_down) {
    const unsigned long held = now - s_boot_down_ms;
    if (held >= config::kBootTapMinMs && held < config::kBootResetHoldMs) s_boot_tap_pending = true;
    s_boot_is_down = false;
  }
  portEXIT_CRITICAL_ISR(&s_boot_mux);
}

constexpr char kWifiPrefsNamespace[] = "wifi";
constexpr char kPrefsForcePortalKey[] = "portal";

bool s_force_config_portal = false;
WiFiManager s_wm;
bool s_wm_configured = false;

void stopLanWebPortal();
bool wifiLinkUp();

// --- Parameters on the Wi-Fi page: home airport (replaces coordinates) + units ---
// The picker searches the built-in airport list through /api/search, which the
// device serves in setup mode too, so it works before the radar has internet.
constexpr char kPickerJs[] =
    "<script>(function(){var q=document.getElementById('aq'),r=document.getElementById('ares'),"
    "s=document.getElementById('asel'),t;function h(){return document.getElementById('home_ap')}"
    "function pick(a){h().value=a.ident;s.className='pick on';s.innerHTML='<b>'+a.ident+'</b> '+a.name+"
    "'<small>'+(a.city?a.city+', ':'')+a.region+'</small>';r.innerHTML='';q.value=''}"
    "q.oninput=function(){clearTimeout(t);t=setTimeout(function(){var v=q.value.trim();if(!v){r.innerHTML='';return}"
    "fetch('/api/search?q='+encodeURIComponent(v)).then(function(x){return x.json()}).then(function(j){r.innerHTML='';"
    "j.results.slice(0,6).forEach(function(a){var d=document.createElement('div');d.className='sr';"
    "d.innerHTML='<b>'+a.ident+'</b><span>'+a.name+'<br><small>'+(a.city?a.city+', ':'')+a.region+'</small></span>';"
    "d.onclick=function(){pick(a)};r.appendChild(d)});if(!j.results.length)r.innerHTML="
    "'<p class=pr-hint>No match. Try the city or the airport code.</p>'})},250)};"
    "q.onkeydown=function(e){if(e.key==='Enter'){e.preventDefault();var f=r.querySelector('.sr');if(f)f.click()}};"
    "var f=q.form;if(f)f.addEventListener('submit',function(e){if(!h().value){e.preventDefault();"
    "s.className='pick err';s.textContent='Choose your home airport first';q.focus()}})})();</script>";
char s_airport_html[2600];
WiFiManagerParameter s_param_airport_html(s_airport_html);
WiFiManagerParameter s_param_airport("home_ap", "", "", 8, " type='hidden'");
char s_units_html[420];
WiFiManagerParameter s_param_units_html(s_units_html);
WiFiManagerParameter s_param_units("dist_units", "", "0", 2, " type='hidden'");

/** True until a location other than the built-in default has been chosen. */
bool locationIsDefault() {
  const auto& cfg = settings::get();
  return fabs(cfg.lat - config::kDefaultRadarLat) < 1e-6 && fabs(cfg.lon - config::kDefaultRadarLon) < 1e-6;
}

void refreshPortalParamValues() {
  const auto& cfg = settings::get();
  char current[160] = "No airport chosen yet";
  const char* cls = "pick";
  uint16_t idx;
  if (!locationIsDefault() && services::search::nearest(cfg.lat, cfg.lon, &idx, nullptr)) {
    const auto& e = services::search::entry(idx);
    const char* city = services::search::city(e);
    snprintf(current, sizeof(current), "<b>%s</b> %s<small>%s%s%s</small>", e.ident, services::search::name(e),
             city, *city ? ", " : "", services::search::region(e));
    cls = "pick on";
    s_param_airport.setValue(e.ident, 8);
  } else {
    s_param_airport.setValue("", 8);
  }
  snprintf(s_airport_html, sizeof(s_airport_html),
           "<div class='pr-sec'><h3>Home airport</h3><p class='pr-hint'>The radar centres on this airport. "
           "Type a name, city or code.</p><input id='aq' placeholder='e.g. Tampa, KTPA or TPA' "
           "autocomplete='off' autocapitalize='off'><div id='ares'></div><div id='asel' class='%s'>%s</div></div>%s",
           cls, current, kPickerJs);

  char buf[4];
  snprintf(buf, sizeof(buf), "%u", cfg.dist_units);
  s_param_units.setValue(buf, 2);
  const char* sel[3] = {"", "", ""};
  sel[cfg.dist_units % 3] = " selected";
  snprintf(s_units_html, sizeof(s_units_html),
           "<div class='pr-sec'><h3>Distance units</h3><select id='du' aria-label='Distance units' "
           "style='margin:0' onchange=\"document.getElementById('dist_units').value=this.value\">"
           "<option value='0'%s>Kilometres</option><option value='1'%s>Miles</option>"
           "<option value='2'%s>Nautical miles</option></select></div>",
           sel[0], sel[1], sel[2]);
}

void onPortalParamsSaved() {
  uint16_t idx;
  if (services::search::findIdent(s_param_airport.getValue(), &idx)) {
    const auto& e = services::search::entry(idx);
    settings::setLocation(services::search::lat(e), services::search::lon(e));
    Serial.printf("Home airport: %s\n", e.ident);
  } else {
    Serial.println("No home airport chosen in portal - keeping previous location");
  }
  const int u = atoi(s_param_units.getValue());
  if (u >= 0 && u <= 2) {
    settings::get().dist_units = static_cast<uint8_t>(u);
    settings::save();
  }
  refreshPortalParamValues();
}
void markForceConfigPortal() {
  s_force_config_portal = true;
  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, false)) return;
  prefs.putBool(kPrefsForcePortalKey, true);
  prefs.end();
}

bool consumeForceConfigPortal() {
  bool pending = s_force_config_portal;
  s_force_config_portal = false;
  Preferences prefs;
  if (prefs.begin(kWifiPrefsNamespace, false)) {
    pending = pending || prefs.getBool(kPrefsForcePortalKey, false);
    prefs.remove(kPrefsForcePortalKey);
    prefs.end();
  }
  return pending;
}

bool storedWifiCredentials() {
  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL) {
    WiFi.mode(WIFI_STA);
    delay(50);
  }
  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) return false;
  return conf.sta.ssid[0] != '\0';
}

void ensureWifiManager();

void eraseWifiCredentials() {
  stopLanWebPortal();
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_OFF);
  delay(100);
  ensureWifiManager();
  WiFi.persistent(true);
  s_wm.resetSettings();
  s_wm.erase();
  WiFi.disconnect(true, true);
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  delay(100);
}

void resetWifiCredentials() {
  markForceConfigPortal();
  eraseWifiCredentials();
  settings::resetToDefaults();  // location, units and options (touch calibration kept)
  Serial.println("WiFi credentials, location, and settings cleared");
}

void onConfigPortalApStarted(WiFiManager*) {
  ui::status::portal();
#ifdef WM_MDNS
  if (MDNS.begin(config::kPortalHostname)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("Setup portal: http://%s.local (or http://%s)\n", config::kPortalHostname,
                  config::kPortalIp);
  } else {
    Serial.printf("Setup portal: http://%s (mDNS unavailable)\n", config::kPortalIp);
  }
#else
  Serial.printf("Setup portal: http://%s\n", config::kPortalIp);
#endif
}

bool wifiLinkUp() {
  return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

void ensureWifiManager() {
  if (s_wm_configured) return;
  s_wm.setConfigPortalTimeout(config::kWifiPortalTimeoutSec);
  s_wm.setAPStaticIPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                           IPAddress(255, 255, 255, 0));
  s_wm.setHostname(config::kPortalHostname);
  s_wm.setAPCallback(onConfigPortalApStarted);
  s_wm.setTitle("Plane Radar");
  s_wm.setCustomHeadElement(web::portalHeadHtml());
  s_wm.setCustomMenuHTML(web::portalMenuHtml());
  std::vector<const char*> menu = {"wifi", "custom", "info", "update", "sep", "restart"};
  s_wm.setMenu(menu);
  s_wm.setShowInfoUpdate(true);
  s_wm.setScanDispPerc(true);
  s_wm.setRemoveDuplicateAPs(true);
  s_wm.setWebServerCallback([]() { web::registerRoutes(s_wm); });

  refreshPortalParamValues();
  s_wm.addParameter(&s_param_airport_html);
  s_wm.addParameter(&s_param_airport);
  s_wm.addParameter(&s_param_units_html);
  s_wm.addParameter(&s_param_units);
  s_wm.setSaveParamsCallback(onPortalParamsSaved);
  s_wm_configured = true;
}

void startLanWebPortal() {
  if (!wifiLinkUp() || s_wm.getWebPortalActive() || s_wm.getConfigPortalActive()) return;
  refreshPortalParamValues();
  WiFi.mode(WIFI_STA);
  s_wm.setConfigPortalBlocking(false);
#ifdef WM_MDNS
  MDNS.end();
  if (MDNS.begin(config::kPortalHostname)) MDNS.addService("http", "tcp", 80);
#endif
  s_wm.startWebPortal();
  Serial.printf("LAN config: http://%s.local or http://%s\n", config::kPortalHostname,
                WiFi.localIP().toString().c_str());
}

void stopLanWebPortal() {
  if (!s_wm.getWebPortalActive()) return;
  s_wm.stopWebPortal();
#ifdef WM_MDNS
  MDNS.end();
#endif
}

void prepareSta() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
}

void startStaConnect(const String& ssid, const String& pass) {
  prepareSta();
  if (ssid.length() > 0) WiFi.begin(ssid.c_str(), pass.c_str());
  else WiFi.begin();
}

bool waitForLinkWithUi(unsigned long attempt_ms) {
  const unsigned long deadline = millis() + attempt_ms;
  while (millis() < deadline) {
    if (wifiLinkUp()) return true;
    bootButtonPollLongPress();
    ui::status::connectingTick();
    delay(config::kWifiConnectingFrameMs);
  }
  return wifiLinkUp();
}

bool tryConnectWithUi(const String& ssid, const String& pass) {
  if (wifiLinkUp()) return true;
  ui::status::connectingBegin(ssid.length() > 0 ? ssid.c_str() : "network");
  for (uint8_t attempt = 1; attempt <= config::kWifiConnectAttempts; ++attempt) {
    if (attempt > 1) {
      Serial.printf("WiFi connect retry %u/%u\n", attempt, config::kWifiConnectAttempts);
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      delay(400);
    }
    startStaConnect(ssid, pass);
    if (waitForLinkWithUi(config::kWifiConnectAttemptMs)) return true;
  }
  return false;
}

bool connectSavedNetwork() {
  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL) {
    WiFi.mode(WIFI_STA);
    delay(50);
  }
  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK || conf.sta.ssid[0] == '\0') return false;
  char ssid_buf[sizeof(conf.sta.ssid) + 1] = {};
  memcpy(ssid_buf, conf.sta.ssid, sizeof(conf.sta.ssid));
  char pass_buf[sizeof(conf.sta.password) + 1] = {};
  memcpy(pass_buf, conf.sta.password, sizeof(conf.sta.password));
  return tryConnectWithUi(String(ssid_buf), String(pass_buf));
}

bool openConfigPortal() {
  stopLanWebPortal();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(50);
  ui::status::portal();
  s_wm.setConfigPortalBlocking(false);
  s_wm.startConfigPortal(config::kPortalApName, services::device_code::get());
  while (s_wm.getConfigPortalActive()) {
    bootButtonPollLongPress();
    if (s_wm.process()) return true;
    delay(10);
  }
  return wifiLinkUp();
}

void logConnected() {
  Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

}  // namespace

bool wifiShowsSetupScreenOnBoot() {
  if (s_force_config_portal) return true;
  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, true)) return false;
  const bool pending = prefs.getBool(kPrefsForcePortalKey, false);
  prefs.end();
  return pending;
}

bool wifiBootButtonPressed() { return digitalRead(config::kBootPin) == LOW; }

void bootButtonInit() {
  pinMode(config::kBootPin, INPUT_PULLUP);
  if (s_boot_interrupt_attached) return;
  attachInterrupt(digitalPinToInterrupt(static_cast<uint8_t>(config::kBootPin)), onBootButtonIsr,
                  CHANGE);
  s_boot_interrupt_attached = true;
}

bool bootButtonConsumeTap() {
  portENTER_CRITICAL(&s_boot_mux);
  const bool tap = s_boot_tap_pending;
  s_boot_tap_pending = false;
  portEXIT_CRITICAL(&s_boot_mux);
  return tap;
}

void bootButtonPollLongPress() {
  if (wifiBootButtonPressed()) {
    portENTER_CRITICAL(&s_boot_mux);
    if (!s_boot_is_down) {
      s_boot_is_down = true;
      s_boot_down_ms = millis();
    }
    const unsigned long down_ms = s_boot_down_ms;
    portEXIT_CRITICAL(&s_boot_mux);
    if (!s_long_press_handled && millis() - down_ms >= config::kBootResetHoldMs) {
      s_long_press_handled = true;
      Serial.println("BOOT held - resetting WiFi");
      wifiResetCredentialsAndReboot();
    }
  } else {
    portENTER_CRITICAL(&s_boot_mux);
    s_boot_is_down = false;
    portEXIT_CRITICAL(&s_boot_mux);
    s_long_press_handled = false;
  }
}

void wifiResetCredentialsAndReboot() {
  resetWifiCredentials();
  ui::status::wifiReset();
  delay(800);
  esp_restart();
}

bool wifiReconnect() {
  bootButtonInit();
  Serial.println("WiFi reconnecting...");
  return connectSavedNetwork();
}

void wifiLoop() {
  ensureWifiManager();
  if (wifiLinkUp()) {
    if (!s_wm.getWebPortalActive() && !s_wm.getConfigPortalActive()) startLanWebPortal();
    if (s_wm.getWebPortalActive() || s_wm.getConfigPortalActive()) s_wm.process();
  } else {
    stopLanWebPortal();
  }
}

bool wifiSetupConnect() {
  bootButtonInit();
  ensureWifiManager();
  const bool force_portal = consumeForceConfigPortal();
  WiFi.setAutoReconnect(false);

  if (force_portal) {
    eraseWifiCredentials();
    Serial.println("Opening WiFi setup portal (after reset)");
    if (openConfigPortal() && wifiLinkUp()) {
      WiFi.setAutoReconnect(true);
      logConnected();
      return true;
    }
    ui::status::connectFailed();
    return false;
  }

  Serial.println("Connecting to WiFi (portal opens if needed)...");
  if (wifiLinkUp() || (storedWifiCredentials() && connectSavedNetwork())) {
    WiFi.setAutoReconnect(true);
    logConnected();
    return true;
  }
  Serial.println(storedWifiCredentials() ? "Saved WiFi could not connect - opening setup portal"
                                         : "No saved WiFi - opening setup portal");
  if (openConfigPortal() && wifiLinkUp()) {
    WiFi.setAutoReconnect(true);
    logConnected();
    return true;
  }
  Serial.println("WiFi connection failed");
  ui::status::connectFailed();
  return false;
}
