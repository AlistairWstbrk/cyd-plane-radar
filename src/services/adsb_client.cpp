// ADS-B client for adsb.fi open data.
// Network/framing approach adapted from MatixYo/ESP32-Plane-Radar (MIT); this
// version parses the "ac" array one element at a time so heap use stays flat
// no matter how many aircraft the feed returns, and keeps the nearest N.
#include "services/adsb_client.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cmath>
#include <cstring>

#include "services/http_body_framing.h"

namespace services::adsb {
namespace {

constexpr char kApiBase[] = "https://opendata.adsb.fi/api/v3/lat/";
constexpr float kKmPerNm = 1.852f;
constexpr float kKmPerDeg = 111.0f;
constexpr float kDegToRad = 0.01745329252f;
constexpr int kConnectTimeoutMs = 5000;
constexpr unsigned long kRequestTimeoutMs = 8000;

Aircraft s_aircraft[kMaxAircraft];
size_t s_count = 0;
unsigned long s_last_update_ms = 0;
FeedStatus s_status = {};
SemaphoreHandle_t s_mutex = nullptr;
SemaphoreHandle_t s_net = nullptr;
uint16_t s_sim_count = 0;

void publish(const Aircraft* src, size_t count, uint16_t total, uint16_t fetch_ms) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  memcpy(s_aircraft, src, sizeof(Aircraft) * count);
  s_count = count;
  s_last_update_ms = millis();
  s_status.last_ok_ms = s_last_update_ms;
  s_status.ok_count++;
  s_status.total_in_range = total;
  s_status.fetch_ms = fetch_ms;
  s_status.last_http = 200;
  xSemaphoreGive(s_mutex);
}

void recordError(int code) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_status.err_count++;
  s_status.last_http = code;
  xSemaphoreGive(s_mutex);
}

/** Raw socket byte source with a deadline (see original project for rationale). */
class SocketSource {
 public:
  SocketSource(HTTPClient& http, WiFiClient& stream, unsigned long deadline)
      : http_(&http), stream_(&stream), deadline_(deadline) {}

  int read() {
    if (pos_ >= len_ && !refill()) return -1;
    return static_cast<unsigned char>(buf_[pos_++]);
  }

 private:
  bool refill() {
    pos_ = 0;
    len_ = 0;
    while (static_cast<long>(millis() - deadline_) < 0) {
      const int avail = stream_->available();
      if (avail > 0) {
        const int want = avail > static_cast<int>(sizeof(buf_)) ? sizeof(buf_) : avail;
        const int got = stream_->readBytes(buf_, want);
        if (got > 0) {
          len_ = static_cast<size_t>(got);
          return true;
        }
      }
      if (!http_->connected() && stream_->available() <= 0) break;
      delay(1);
    }
    return false;
  }

  HTTPClient* http_;
  WiFiClient* stream_;
  unsigned long deadline_;
  char buf_[1024];
  size_t pos_ = 0;
  size_t len_ = 0;
};

using BodyReader = services::http::BodyFramer<SocketSource>;

/** Consume input until `needle` has been read. */
bool skipPast(BodyReader& in, const char* needle) {
  const size_t n = strlen(needle);
  size_t matched = 0;
  for (;;) {
    const int c = in.read();
    if (c < 0) return false;
    if (c == needle[matched]) {
      if (++matched == n) return true;
    } else {
      matched = (c == needle[0]) ? 1 : 0;
    }
  }
}

/** After `"ac"` skip whitespace and ':' up to and including '['. */
bool skipToArrayOpen(BodyReader& in) {
  for (;;) {
    const int c = in.read();
    if (c < 0) return false;
    if (c == '[') return true;
    if (c != ':' && c != ' ' && c != '\n' && c != '\r' && c != '\t') return false;
  }
}

/** After an element: true if another element follows (','), false on ']'. */
bool nextElement(BodyReader& in) {
  for (;;) {
    const int c = in.read();
    if (c < 0 || c == ']') return false;
    if (c == ',') return true;
  }
}

bool readFloat(JsonObjectConst o, const char* k, float* out) {
  JsonVariantConst v = o[k];
  if (v.is<float>() || v.is<int>() || v.is<double>()) {
    *out = v.as<float>();
    return true;
  }
  return false;
}

float pickFirst(JsonObjectConst o, std::initializer_list<const char*> keys, float fallback) {
  float v = 0;
  for (const char* k : keys) {
    if (readFloat(o, k, &v)) return v;
  }
  return fallback;
}

void copyTrimmed(JsonObjectConst o, const char* k, char* out, size_t out_len) {
  out[0] = '\0';
  if (!o[k].is<const char*>()) return;
  const char* s = o[k].as<const char*>();
  size_t n = strnlen(s, out_len - 1);
  while (n > 0 && s[n - 1] == ' ') --n;
  memcpy(out, s, n);
  out[n] = '\0';
}

bool parsePlane(JsonObjectConst p, Aircraft* ac) {
  if (!p["lat"].is<float>() || !p["lon"].is<float>()) return false;
  memset(ac, 0, sizeof(*ac));
  ac->lat = p["lat"].as<float>();
  ac->lon = p["lon"].as<float>();
  ac->nose_deg = pickFirst(p, {"true_heading", "mag_heading", "track", "dir"}, 0);
  ac->track_deg = pickFirst(p, {"track", "true_heading", "mag_heading", "dir"}, ac->nose_deg);
  ac->gs_knots = pickFirst(p, {"gs", "tas", "ias"}, 0);

  if (p["alt_baro"].is<const char*>() &&
      strcmp(p["alt_baro"].as<const char*>(), "ground") == 0) {
    ac->on_ground = true;
    ac->alt_valid = true;
    ac->alt_ft = 0;
  } else {
    float alt = 0;
    if (readFloat(p, "alt_baro", &alt) || readFloat(p, "alt_geom", &alt)) {
      ac->alt_ft = static_cast<int32_t>(lroundf(alt));
      ac->alt_valid = true;
    }
  }
  float vr = 0;
  if (readFloat(p, "baro_rate", &vr) || readFloat(p, "geom_rate", &vr)) {
    if (vr > 32000) vr = 32000;
    if (vr < -32000) vr = -32000;
    ac->vrate_fpm = static_cast<int16_t>(vr);
  }
  float seen = 0;
  readFloat(p, "seen_pos", &seen);
  if (seen < 0) seen = 0;
  if (seen > 30) seen = 30;
  ac->pos_age_ms = static_cast<uint32_t>(seen * 1000.0f);

  copyTrimmed(p, "hex", ac->hex, sizeof(ac->hex));
  copyTrimmed(p, "flight", ac->callsign, sizeof(ac->callsign));
  if (ac->callsign[0] == '\0') {
    copyTrimmed(p, "r", ac->callsign, sizeof(ac->callsign));
  }
  if (ac->callsign[0] == '\0') {
    strncpy(ac->callsign, ac->hex, sizeof(ac->callsign) - 1);
    for (char* c = ac->callsign; *c; ++c) *c = toupper(*c);
  }
  copyTrimmed(p, "t", ac->type, sizeof(ac->type));
  copyTrimmed(p, "r", ac->reg, sizeof(ac->reg));
  copyTrimmed(p, "squawk", ac->squawk, sizeof(ac->squawk));
  if (p["category"].is<const char*>()) {
    const char* c = p["category"].as<const char*>();
    if (c[0] >= 'A' && c[0] <= 'D' && c[1] >= '0' && c[1] <= '7') {
      ac->category = static_cast<uint8_t>(((c[0] - 'A' + 1) << 4) | (c[1] - '0'));
    }
  }
  if (p["dbFlags"].is<int>()) {
    ac->military = (p["dbFlags"].as<int>() & 1) != 0;
  }
  return true;
}

float distKm(double clat, double clon, float lat, float lon) {
  const float dx = static_cast<float>(lon - clon) * kKmPerDeg * cosf(static_cast<float>(clat) * kDegToRad);
  const float dy = static_cast<float>(lat - clat) * kKmPerDeg;
  return sqrtf(dx * dx + dy * dy);
}

// Working buffers for a fetch (static: the fetch task stack stays small).
Aircraft s_parsed[kMaxAircraft];
float s_parsed_dist[kMaxAircraft];

/** Insert keeping the nearest kMaxAircraft. */
void keepNearest(size_t* n, const Aircraft& ac, float d) {
  if (*n < kMaxAircraft) {
    s_parsed[*n] = ac;
    s_parsed_dist[*n] = d;
    ++*n;
    return;
  }
  size_t far = 0;
  for (size_t i = 1; i < kMaxAircraft; ++i) {
    if (s_parsed_dist[i] > s_parsed_dist[far]) far = i;
  }
  if (d < s_parsed_dist[far]) {
    s_parsed[far] = ac;
    s_parsed_dist[far] = d;
  }
}

// --- Simulation -------------------------------------------------------------

struct SimPlane {
  float x_km, y_km, track, gs, alt, vr, turn;
};
SimPlane s_sim[kMaxAircraft];
unsigned long s_sim_last_ms = 0;
bool s_sim_seeded = false;
const char* const kSimAirlines[] = {"KLM", "BAW", "DLH", "EZY", "RYR", "AFR", "UAL", "N", "TRA", "SAS"};
const char* const kSimTypes[] = {"B738", "A320", "A21N", "E190", "B77W", "A359", "C172", "PC12", "B789", "DH8D"};

float frand(float lo, float hi) { return lo + (hi - lo) * (esp_random() / 4294967295.0f); }

void simSpawn(SimPlane& p, float radius, bool edge) {
  const float a = frand(0, 6.2831853f);
  const float r = edge ? radius * 0.98f : radius * sqrtf(frand(0.0f, 1.0f));
  p.x_km = sinf(a) * r;
  p.y_km = cosf(a) * r;
  // Head roughly toward the centre when spawning at the edge.
  const float inward = fmodf(a / kDegToRad + 180.0f, 360.0f);
  p.track = edge ? fmodf(inward + frand(-50, 50) + 360.0f, 360.0f) : frand(0, 360);
  p.gs = frand(90, 480);
  p.alt = p.gs < 150 ? frand(1000, 6000) : frand(3000, 39000);
  p.vr = frand(-1500, 1500);
  p.turn = frand(-0.6f, 0.6f);  // deg/s
}

}  // namespace

void init() {
  if (s_mutex == nullptr) s_mutex = xSemaphoreCreateMutex();
  if (s_net == nullptr) s_net = xSemaphoreCreateMutex();
}

bool netLock(uint32_t timeout_ms) {
  return xSemaphoreTake(s_net, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void netUnlock() { xSemaphoreGive(s_net); }

size_t snapshotAircraft(Aircraft* out, size_t max_out, unsigned long* out_last) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  const size_t n = s_count < max_out ? s_count : max_out;
  memcpy(out, s_aircraft, sizeof(Aircraft) * n);
  if (out_last) *out_last = s_last_update_ms;
  xSemaphoreGive(s_mutex);
  return n;
}

FeedStatus status() {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  FeedStatus st = s_status;
  xSemaphoreGive(s_mutex);
  return st;
}

bool fetchUpdate(double clat, double clon, float radius_km, bool include_ground) {
  const unsigned long t0 = millis();
  s_status.last_try_ms = t0;
  String url = kApiBase;
  url += String(clat, 6);
  url += "/lon/";
  url += String(clon, 6);
  url += "/dist/";
  url += String(radius_km / kKmPerNm, 1);

  JsonDocument filter;
  for (const char* key : {"lat", "lon", "true_heading", "mag_heading", "track", "dir", "gs",
                          "tas", "ias", "alt_baro", "alt_geom", "baro_rate", "geom_rate",
                          "seen_pos", "flight", "hex", "t", "r", "squawk", "category",
                          "dbFlags"}) {
    filter[key] = true;
  }

  if (!netLock(15000)) return false;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  bool ok = false;
  do {
    if (!http.begin(client, url)) {
      recordError(-100);
      break;
    }
    static const char* kHeaders[] = {"Transfer-Encoding"};
    http.collectHeaders(kHeaders, 1);
    http.setConnectTimeout(kConnectTimeoutMs);
    http.setTimeout(kRequestTimeoutMs);
    http.addHeader("Accept", "application/json");
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
      Serial.printf("adsb: HTTP %d\n", code);
      recordError(code);
      break;
    }
    WiFiClient* stream = http.getStreamPtr();
    if (stream == nullptr) {
      recordError(-101);
      break;
    }
    const auto framing = http.header("Transfer-Encoding").equalsIgnoreCase("chunked")
                             ? services::http::BodyFraming::kChunked
                             : services::http::BodyFraming::kIdentity;
    SocketSource src(http, *stream, millis() + kRequestTimeoutMs);
    BodyReader body(src, framing, http.getSize());

    size_t n = 0;
    uint16_t total = 0;
    if (skipPast(body, "\"ac\"") && skipToArrayOpen(body)) {
      JsonDocument doc;
      for (;;) {
        const DeserializationError err =
            deserializeJson(doc, body, DeserializationOption::Filter(filter));
        if (err) {
          // Empty array ("ac":[]) yields EmptyInput/InvalidInput here: fine.
          break;
        }
        Aircraft ac;
        JsonObjectConst p = doc.as<JsonObjectConst>();
        if (parsePlane(p, &ac) && (include_ground || !ac.on_ground)) {
          ++total;
          keepNearest(&n, ac, distKm(clat, clon, ac.lat, ac.lon));
        }
        if (!nextElement(body)) break;
      }
      ok = !body.framingError();
    } else {
      Serial.println("adsb: no \"ac\" array in response");
      recordError(-102);
    }
    if (ok) {
      publish(s_parsed, n, total, static_cast<uint16_t>(millis() - t0));
      Serial.printf("adsb: %u aircraft (%u in range) %lums heap %u\n",
                    static_cast<unsigned>(n), total, millis() - t0, ESP.getFreeHeap());
    } else if (body.framingError()) {
      recordError(-103);
    }
  } while (false);
  http.end();
  netUnlock();
  return ok;
}

void injectForTest(const Aircraft* list, size_t count) {
  if (count > kMaxAircraft) count = kMaxAircraft;
  publish(list, count, static_cast<uint16_t>(count), 0);
}

void setSimulation(uint16_t count) {
  if (count > kMaxAircraft) count = kMaxAircraft;
  s_sim_count = count;
  s_sim_seeded = false;
}

uint16_t simulationCount() { return s_sim_count; }

void simulateUpdate(double clat, double clon, float radius_km) {
  const unsigned long now = millis();
  const size_t n = s_sim_count;
  if (!s_sim_seeded) {
    for (size_t i = 0; i < n; ++i) simSpawn(s_sim[i], radius_km, false);
    s_sim_seeded = true;
    s_sim_last_ms = now;
  }
  const float dt = (now - s_sim_last_ms) / 1000.0f;
  s_sim_last_ms = now;
  const float cos_lat = cosf(static_cast<float>(clat) * kDegToRad);

  for (size_t i = 0; i < n; ++i) {
    SimPlane& p = s_sim[i];
    p.track = fmodf(p.track + p.turn * dt + 360.0f, 360.0f);
    const float d = p.gs * kKmPerNm / 3600.0f * dt;
    p.x_km += sinf(p.track * kDegToRad) * d;
    p.y_km += cosf(p.track * kDegToRad) * d;
    p.alt += p.vr / 60.0f * dt;
    if (p.alt < 500) { p.alt = 500; p.vr = fabsf(p.vr); }
    if (p.alt > 41000) { p.alt = 41000; p.vr = -fabsf(p.vr); }
    if (sqrtf(p.x_km * p.x_km + p.y_km * p.y_km) > radius_km) simSpawn(p, radius_km, true);

    Aircraft& ac = s_parsed[i];
    memset(&ac, 0, sizeof(ac));
    ac.lat = static_cast<float>(clat + p.y_km / kKmPerDeg);
    ac.lon = static_cast<float>(clon + p.x_km / (kKmPerDeg * cos_lat));
    ac.track_deg = p.track;
    ac.nose_deg = p.track;
    ac.gs_knots = p.gs;
    ac.alt_ft = static_cast<int32_t>(p.alt);
    ac.alt_valid = true;
    ac.vrate_fpm = static_cast<int16_t>(p.vr);
    snprintf(ac.hex, sizeof(ac.hex), "5%05x", static_cast<unsigned>(i));
    const char* al = kSimAirlines[i % 10];
    if (al[0] == 'N' && al[1] == '\0') {
      snprintf(ac.callsign, sizeof(ac.callsign), "N%uSIM", static_cast<unsigned>(100 + i));
    } else {
      snprintf(ac.callsign, sizeof(ac.callsign), "%s%u", al, static_cast<unsigned>(1000 + i * 37 % 9000));
    }
    strncpy(ac.type, kSimTypes[(i * 7) % 10], sizeof(ac.type) - 1);
    snprintf(ac.reg, sizeof(ac.reg), "PH-S%02u", static_cast<unsigned>(i % 100));
    snprintf(ac.squawk, sizeof(ac.squawk), "%04u", static_cast<unsigned>(1000 + (i * 113) % 6000));
    if (i == 3) strcpy(ac.squawk, "7700");
    ac.category = 0x13;
    if (i == 5) { ac.on_ground = true; ac.gs_knots = 15; }
  }
  publish(s_parsed, n, static_cast<uint16_t>(n), 0);
}

}  // namespace services::adsb
