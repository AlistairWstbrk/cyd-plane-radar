#include "services/web_portal.h"

#include <Arduino.h>
#include <WiFi.h>
#include <Update.h>
#include <WiFiManager.h>

#include <ArduinoJson.h>

#include <cmath>
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
#include "ui/status_screens.h"
#include "ui/traffic.h"
#include "web_pages.h"

void applyTimezone();

namespace web {
namespace {

WiFiManager* s_wm = nullptr;

WiFiManager::WM_WebServer& srv() { return *s_wm->server; }

void sendJson(int code, JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  srv().sendHeader("Cache-Control", "no-store");
  srv().send(code, "application/json", out);
}

void sendError(int code, const char* msg) {
  JsonDocument d;
  d["error"] = msg;
  sendJson(code, d);
}

void sendOk() {
  JsonDocument d;
  d["ok"] = true;
  sendJson(200, d);
}

void sendPage(const char* body) {
  auto& s = srv();
  s.sendHeader("Cache-Control", "no-store");
  s.setContentLength(CONTENT_LENGTH_UNKNOWN);
  s.send(200, "text/html", "");
  s.sendContent(pages::kShellStart);
  s.sendContent(pages::kHead);
  s.sendContent(pages::kShellBody);
  s.sendContent(body);
  s.sendContent(pages::kShellEnd);
  s.sendContent("");
}

bool parseBody(JsonDocument& doc) {
  if (!srv().hasArg("plain")) return false;
  return !deserializeJson(doc, srv().arg("plain"));
}

// ------------------------------------------------------- firmware update ---
// Replaces WiFiManager's /u handler (registered first, so it wins): reports
// errors instead of failing silently, shows progress on the screen and
// restarts only after a verified write.

String s_ota_err;
bool s_ota_started = false;

void otaFail(const char* what) {
  if (s_ota_err.length()) return;
  s_ota_err = what;
  s_ota_err += ": ";
  s_ota_err += Update.errorString();
}

void handleOtaUpload() {
  HTTPUpload& up = srv().upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      s_ota_err = "";
      s_ota_started = true;
      Serial.printf("OTA: receiving %s\n", up.filename.c_str());
      ui::status::firmwareUpdate(0);
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) otaFail("begin");
      break;
    case UPLOAD_FILE_WRITE:
      if (!s_ota_err.length() && Update.write(up.buf, up.currentSize) != up.currentSize) otaFail("write");
      break;
    case UPLOAD_FILE_END:
      if (!s_ota_err.length() && !Update.end(true)) otaFail("verify");
      Serial.printf("OTA: %u bytes, %s\n", static_cast<unsigned>(up.totalSize),
                    s_ota_err.length() ? s_ota_err.c_str() : "ok");
      break;
    default:  // aborted
      Update.abort();
      s_ota_err = "upload interrupted";
      break;
  }
}

void handleOtaDone() {
  String msg;
  const bool ok = s_ota_started && !s_ota_err.length() && Update.isFinished();
  if (!s_ota_started) s_ota_err = "no file received";
  msg = ok ? "<div class='card'><h2>Update installed</h2><p class='pr-hint'>The radar is restarting. "
             "This page reconnects in a few seconds.</p></div>"
             "<script>setTimeout(function(){location.href='/'},12000)</script>"
           : "<div class='card'><h2>Update failed</h2><p class='pr-hint'>The old firmware is still installed. "
             "Use the <b>firmware.bin</b> file from the release (not the merged image).</p><p class='pr-hint'>" +
                 s_ota_err + "</p><a class='btn' href='/update'>Try again</a></div>";
  auto& s = srv();
  s.sendHeader("Cache-Control", "no-store");
  s.sendHeader("Connection", "close");
  s.setContentLength(CONTENT_LENGTH_UNKNOWN);
  s.send(ok ? 200 : 500, "text/html", "");
  s.sendContent(pages::kShellStart);
  s.sendContent(pages::kHead);
  s.sendContent(pages::kShellBody);
  s.sendContent(msg);
  s.sendContent(pages::kShellEnd);
  s.sendContent("");
  s_ota_started = false;
  if (!ok) {
    ui::status::firmwareUpdate(-1);
    delay(2500);
    ui::requestRedraw();
    return;
  }
  ui::status::firmwareUpdate(1);
  s.client().flush();
  delay(800);
  ESP.restart();
}

void handleCss() {
  srv().sendHeader("Cache-Control", "max-age=600");
  srv().send(200, "text/css", pages::kCss);
}

void fillStatus(JsonObject st) {
  const auto fs = services::adsb::status();
  const bool online = WiFi.status() == WL_CONNECTED;
  st["online"] = online;
  st["ip"] = online ? WiFi.localIP().toString() : String(config::kPortalIp);
  st["ssid"] = online ? WiFi.SSID() : String("");
  st["rssi"] = online ? WiFi.RSSI() : 0;
  st["heap"] = ESP.getFreeHeap();
  st["uptime_s"] = millis() / 1000;
  st["ok"] = fs.ok_count;
  st["err"] = fs.err_count;
  st["fetch_ms"] = fs.fetch_ms;
  st["last_http"] = fs.last_http;
  st["count"] = ui::traffic::count();
  st["sim"] = services::adsb::simulationCount();
}

void handleState() {
  JsonDocument d;
  const auto& c = settings::get();
  JsonObject s = d["settings"].to<JsonObject>();
  s["lat"] = serialized(String(c.lat, 6));
  s["lon"] = serialized(String(c.lon, 6));
  s["range_idx"] = c.range_idx;
  s["dist_units"] = c.dist_units;
  s["alt_units"] = c.alt_units;
  s["speed_units"] = c.speed_units;
  s["label_mode"] = c.label_mode;
  s["theme"] = c.theme;
  s["brightness"] = c.brightness;
  s["alert_km"] = c.alert_km;
  s["fetch_s"] = c.fetch_s;
  s["max_alt_kft"] = c.max_alt_kft;
  s["show_runways"] = c.show_runways;
  s["show_trails"] = c.show_trails;
  s["show_ground"] = c.show_ground;
  s["alt_colors"] = c.alt_colors;
  s["sweep"] = c.sweep;
  s["alert_led"] = c.alert_led;
  s["status_led"] = c.status_led;
  s["clock_24h"] = c.clock_24h;
  s["flip"] = c.flip;
  s["invert"] = c.invert;
  s["tz"] = c.tz;
  s["night_mode"] = c.night_mode;
  s["night_start_h"] = c.night_start_h;
  s["night_end_h"] = c.night_end_h;
  s["night_brightness"] = c.night_brightness;
  s["night_theme"] = c.night_theme;
  {
    uint16_t hi;
    float km;
    if (services::search::nearest(c.lat, c.lon, &hi, &km)) {
      const auto& e = services::search::entry(hi);
      JsonObject h = d["home"].to<JsonObject>();
      h["ident"] = e.ident;
      h["iata"] = e.iata;
      h["name"] = services::search::name(e);
      h["city"] = services::search::city(e);
      h["region"] = services::search::region(e);
      h["km"] = serialized(String(km, 1));
    }
  }
  JsonArray r = d["ranges"].to<JsonArray>();
  for (size_t i = 0; i < settings::kRangeCount; ++i) {
    char b[16];
    settings::formatRangeLabel(b, sizeof(b), settings::kRangePresets[i].ring3_km);
    r.add(String(b));
  }
  fillStatus(d["status"].to<JsonObject>());
  d["version"] = config::kFirmwareVersion;
  sendJson(200, d);
}

template <typename T>
void readU8(JsonObjectConst o, const char* k, T& out, int lo, int hi) {
  if (!o[k].is<int>()) return;
  const int v = o[k].as<int>();
  if (v >= lo && v <= hi) out = static_cast<T>(v);
}

void readBool(JsonObjectConst o, const char* k, bool& out) {
  if (o[k].is<bool>()) out = o[k].as<bool>();
}

void handleSettingsPost() {
  JsonDocument d;
  if (!parseBody(d)) return sendError(400, "invalid JSON");
  JsonObjectConst o = d.as<JsonObjectConst>();
  auto& c = settings::get();
  const bool old_flip = c.flip, old_inv = c.invert;
  char old_tz[sizeof(c.tz)];
  strncpy(old_tz, c.tz, sizeof(old_tz));

  if (o["lat"].is<double>() && o["lon"].is<double>()) {
    const double lat = o["lat"], lon = o["lon"];
    if (std::isfinite(lat) && std::isfinite(lon) && fabs(lat) <= 90 && fabs(lon) <= 180) {
      c.lat = lat;
      c.lon = lon;
    }
  }
  readU8(o, "range_idx", c.range_idx, 0, settings::kRangeCount - 1);
  readU8(o, "dist_units", c.dist_units, 0, 2);
  readU8(o, "alt_units", c.alt_units, 0, 1);
  readU8(o, "speed_units", c.speed_units, 0, 2);
  readU8(o, "label_mode", c.label_mode, 0, 2);
  readU8(o, "theme", c.theme, 0, settings::kThemeCount - 1);
  readU8(o, "brightness", c.brightness, 5, 100);
  readU8(o, "alert_km", c.alert_km, 0, 50);
  readU8(o, "fetch_s", c.fetch_s, 3, 60);
  readU8(o, "max_alt_kft", c.max_alt_kft, 0, 60);
  readBool(o, "show_runways", c.show_runways);
  readBool(o, "show_trails", c.show_trails);
  readBool(o, "show_ground", c.show_ground);
  readBool(o, "alt_colors", c.alt_colors);
  readBool(o, "sweep", c.sweep);
  readBool(o, "alert_led", c.alert_led);
  readBool(o, "status_led", c.status_led);
  readBool(o, "clock_24h", c.clock_24h);
  readBool(o, "flip", c.flip);
  readBool(o, "invert", c.invert);
  readBool(o, "night_mode", c.night_mode);
  readU8(o, "night_start_h", c.night_start_h, 0, 23);
  readU8(o, "night_end_h", c.night_end_h, 0, 23);
  readU8(o, "night_brightness", c.night_brightness, 5, 100);
  readBool(o, "night_theme", c.night_theme);
  if (o["tz"].is<const char*>() && strlen(o["tz"].as<const char*>()) > 0) {
    strncpy(c.tz, o["tz"].as<const char*>(), sizeof(c.tz) - 1);
    c.tz[sizeof(c.tz) - 1] = '\0';
  }
  if (o["sim"].is<int>()) services::adsb::setSimulation(o["sim"].as<int>());
  settings::save();
  if (old_flip != c.flip || old_inv != c.invert) hw::applyOrientation(c.flip, c.invert);
  if (strcmp(old_tz, c.tz) != 0) applyTimezone();
  sendOk();
}

void handleAircraft() {
  JsonDocument d;
  char b[24];
  settings::formatRangeLabel(b, sizeof(b), settings::range().ring3_km);
  d["range"] = b;
  d["outer_km"] = settings::range().outer_km;
  d["sim"] = services::adsb::simulationCount() > 0;
  JsonArray arr = d["aircraft"].to<JsonArray>();
  for (size_t i = 0; i < ui::traffic::count(); ++i) {
    const auto& t = ui::traffic::at(i);
    const auto& ac = t.ac;
    JsonObject a = arr.add<JsonObject>();
    a["hex"] = ac.hex;
    a["call"] = ac.callsign;
    a["type"] = ac.type;
    a["reg"] = ac.reg;
    settings::formatAltitude(b, sizeof(b), ac.alt_ft, ac.on_ground, ac.alt_valid);
    a["alt"] = b;
    settings::formatSpeed(b, sizeof(b), ac.gs_knots);
    a["spd"] = b;
    settings::formatDistance(b, sizeof(b), t.dist_km);
    a["dists"] = b;
    a["gs"] = lroundf(ac.gs_knots);
    a["trk"] = lroundf(ac.track_deg);
    a["dist"] = serialized(String(t.dist_km, 2));
    a["brg"] = lroundf(t.brg_deg);
    a["sqk"] = ac.squawk;
    a["emerg"] = !strcmp(ac.squawk, "7700") || !strcmp(ac.squawk, "7600") || !strcmp(ac.squawk, "7500");
  }
  sendJson(200, d);
}

void handleAirportsGet() {
  JsonDocument d;
  JsonArray arr = d["airports"].to<JsonArray>();
  for (size_t i = 0; i < services::airports::count(); ++i) {
    services::airports::toJson(services::airports::at(i), arr.add<JsonObject>());
  }
  d["max"] = services::airports::kMaxCustomAirports;
  d["online"] = WiFi.status() == WL_CONNECTED;
  JsonObject c = d["centre"].to<JsonObject>();
  c["lat"] = serialized(String(settings::get().lat, 6));
  c["lon"] = serialized(String(settings::get().lon, 6));
  sendJson(200, d);
}

void handleAirportsPost() {
  JsonDocument d;
  if (!parseBody(d)) return sendError(400, "invalid JSON");
  services::airports::Airport ap;
  const char* err = "invalid airport";
  if (!services::airports::fromJson(d.as<JsonObjectConst>(), &ap, &err)) return sendError(400, err);
  if (!services::airports::upsert(ap)) return sendError(507, "airport list full or flash error");
  settings::markChanged();
  sendOk();
}

void handleAirportDelete() {
  if (!services::airports::remove(srv().arg("ident").c_str())) return sendError(404, "not found");
  settings::markChanged();
  sendOk();
}

void handleAirportCenter() {
  const int i = services::airports::find(srv().arg("ident").c_str());
  if (i < 0) return sendError(404, "not found");
  const auto& ap = services::airports::at(i);
  settings::setLocation(ap.lat, ap.lon);
  sendOk();
}

void handleSearch() {
  const auto& c = settings::get();
  services::search::Hit hits[12];
  int total = 0;
  const int n = services::search::run(srv().arg("q").c_str(), c.lat, c.lon, hits, 12, &total);
  JsonDocument d;
  d["total"] = total;
  JsonArray a = d["results"].to<JsonArray>();
  for (int i = 0; i < n; ++i) {
    const auto& e = services::search::entry(hits[i].idx);
    JsonObject o = a.add<JsonObject>();
    o["ident"] = e.ident;
    o["iata"] = e.iata;
    o["name"] = services::search::name(e);
    o["city"] = services::search::city(e);
    o["region"] = services::search::region(e);
    o["km"] = serialized(String(hits[i].dist_km, 1));
    o["lat"] = serialized(String(services::search::lat(e), 5));
    o["lon"] = serialized(String(services::search::lon(e), 5));
  }
  sendJson(200, d);
}

/** Centre the radar on an airport from the built-in list (or a saved custom one). */
void handleLocation() {
  const String id = srv().arg("ident");
  uint16_t idx;
  if (services::search::findIdent(id.c_str(), &idx)) {
    const auto& e = services::search::entry(idx);
    settings::setLocation(services::search::lat(e), services::search::lon(e));
  } else {
    const int i = services::airports::find(id.c_str());
    if (i < 0) return sendError(404, "unknown airport");
    settings::setLocation(services::airports::at(i).lat, services::airports::at(i).lon);
  }
  sendOk();
}

void handleLookupStart() {
  if (WiFi.status() != WL_CONNECTED) return sendError(503, "device offline");
  if (!services::lookup::start(srv().arg("ident").c_str())) return sendError(409, "lookup busy or invalid ident");
  sendOk();
}

void handleLookupStatus() {
  using services::lookup::State;
  JsonDocument d;
  const State st = services::lookup::state();
  d["state"] = st == State::kRunning ? "running" : st == State::kDone ? "done" : st == State::kError ? "error" : "idle";
  d["progress"] = services::lookup::progress();
  d["message"] = services::lookup::message();
  d["ident"] = services::lookup::ident();
  if (st == State::kDone) services::airports::toJson(services::lookup::result(), d["result"].to<JsonObject>());
  sendJson(200, d);
}

// ------------------------------------------------------------------ login ---
// On the home network every page (WiFiManager's too: Wi-Fi, update, restart)
// asks for user "admin" and the device code. A successful login opens a
// session for that client IP that lasts while it keeps using the pages. The
// setup access point is already protected by its WPA2 password.
//
// The gate is registered before every other route, so it sees each request
// first. Handler lookup happens before headers are parsed, which is why the
// decision in canHandle() uses the session table and the Authorization header
// is checked in handle().

constexpr unsigned long kSessionIdleMs = 30UL * 60UL * 1000UL;
constexpr int kSessions = 6;
struct Session {
  uint32_t ip;
  unsigned long last_ms;
};
Session s_sessions[kSessions] = {};
unsigned long s_fail_ms = 0;

uint32_t clientIp() { return static_cast<uint32_t>(srv().client().remoteIP()); }

Session* findSession(uint32_t ip) {
  for (auto& s : s_sessions)
    if (s.ip == ip && s.ip != 0 && millis() - s.last_ms < kSessionIdleMs) return &s;
  return nullptr;
}

void openSession(uint32_t ip) {
  Session* slot = &s_sessions[0];
  for (auto& s : s_sessions) {
    if (s.ip == ip) { slot = &s; break; }
    if (s.last_ms < slot->last_ms || s.ip == 0) slot = &s;
  }
  slot->ip = ip;
  slot->last_ms = millis();
}

bool loginRequired(const String& uri) {
  if (WiFi.getMode() & WIFI_MODE_AP) return false;  // setup AP: WPA2 password already
  if (uri == "/pr.css" || uri == "/favicon.ico") return false;
  Session* s = findSession(clientIp());
  if (!s) return true;
  s->last_ms = millis();
  return false;
}

String urlEncode(const String& v) {
  static const char* hex = "0123456789ABCDEF";
  String o;
  for (size_t i = 0; i < v.length(); ++i) {
    const char c = v[i];
    if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
      o += c;
    } else {
      o += '%';
      o += hex[(c >> 4) & 15];
      o += hex[c & 15];
    }
  }
  return o;
}

class LoginGate : public RequestHandler {
 public:
  bool canHandle(HTTPMethod, String uri) override { return loginRequired(uri); }
  bool handle(WebServer& s, HTTPMethod method, String uri) override {
    if (!s.authenticate(services::device_code::kLoginUser, services::device_code::get())) {
      if (s.hasHeader("Authorization")) {
        // Wrong password: slow down guessing a little.
        if (millis() - s_fail_ms < 1000) delay(600);
        s_fail_ms = millis();
      }
      s.requestAuthentication(DIGEST_AUTH, "Plane Radar", pages::kLoginFail);
      return true;
    }
    openSession(clientIp());
    // Logged in: send the browser back to the same address (307 keeps the
    // method and body), which now goes to the real page.
    String loc = uri;
    if (method == HTTP_GET) {
      char sep = '?';
      for (int i = 0; i < s.args(); ++i) {
        if (s.argName(i) == "plain") continue;
        loc += sep;
        loc += urlEncode(s.argName(i));
        loc += '=';
        loc += urlEncode(s.arg(i));
        sep = '&';
      }
    }
    s.sendHeader("Location", loc);
    s.sendHeader("Cache-Control", "no-store");
    s.send(307, "text/plain", "");
    return true;
  }
};

void handleLookupSave() {
  if (services::lookup::state() != services::lookup::State::kDone) return sendError(409, "no lookup result");
  if (!services::airports::upsert(services::lookup::result())) return sendError(507, "airport list full");
  services::lookup::reset();
  settings::markChanged();
  sendOk();
}

}  // namespace

const char* portalHeadHtml() { return pages::kHead; }
const char* portalMenuHtml() { return pages::kMenu; }

void registerRoutes(WiFiManager& wm) {
  s_wm = &wm;
  auto& s = srv();
  // Must be first: see the login section above. WiFiManager builds a new
  // server (and calls this) each time a portal starts; it owns the handler.
  s.addHandler(new LoginGate());
  s.on("/u", HTTP_POST, handleOtaDone, handleOtaUpload);
  s.on("/pr.css", HTTP_GET, handleCss);
  s.on("/settings", HTTP_GET, [] { sendPage(pages::kSettings); });
  s.on("/airports", HTTP_GET, [] { sendPage(pages::kAirports); });
  s.on("/live", HTTP_GET, [] { sendPage(pages::kLive); });
  s.on("/api/state", HTTP_GET, handleState);
  s.on("/api/settings", HTTP_POST, handleSettingsPost);
  s.on("/api/aircraft", HTTP_GET, handleAircraft);
  s.on("/api/airports", HTTP_GET, handleAirportsGet);
  s.on("/api/airports", HTTP_POST, handleAirportsPost);
  s.on("/api/airports/delete", HTTP_POST, handleAirportDelete);
  s.on("/api/airports/center", HTTP_POST, handleAirportCenter);
  s.on("/api/search", HTTP_GET, handleSearch);
  s.on("/api/location", HTTP_POST, handleLocation);
  s.on("/api/lookup", HTTP_POST, handleLookupStart);
  s.on("/api/lookup", HTTP_GET, handleLookupStatus);
  s.on("/api/lookup/save", HTTP_POST, handleLookupSave);
}

}  // namespace web
