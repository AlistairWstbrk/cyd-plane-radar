#include "services/airport_lookup.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "services/adsb_client.h"

namespace services::lookup {
namespace {

constexpr char kAirportsUrl[] =
    "https://raw.githubusercontent.com/davidmegginson/ourairports-data/main/airports.csv";
constexpr char kRunwaysUrl[] =
    "https://raw.githubusercontent.com/davidmegginson/ourairports-data/main/runways.csv";
constexpr uint32_t kProbeBytes = 1200;   // > 2 * longest line (468 bytes)
constexpr uint32_t kScanBytes = 4096;
constexpr int kMaxFields = 20;
constexpr float kDegToRad = 0.01745329252f;
constexpr float kMPerDegLat = 111320.0f;

volatile State s_state = State::kIdle;
volatile uint8_t s_progress = 0;
char s_message[96] = "";
char s_ident[8] = "";
airports::Airport s_result;
TaskHandle_t s_task = nullptr;
int s_steps = 0;

void setMsg(const char* m) {
  strncpy(s_message, m, sizeof(s_message) - 1);
  s_message[sizeof(s_message) - 1] = '\0';
}

void step() {
  ++s_steps;
  const int p = 5 + s_steps * 90 / 32;
  s_progress = static_cast<uint8_t>(p > 95 ? 95 : p);
}

/** Minimal CSV line splitter (RFC 4180 quotes, "" escapes). */
struct CsvRow {
  char store[520];
  const char* f[kMaxFields];
  int n = 0;

  bool parse(const char* s, size_t len) {
    n = 0;
    size_t w = 0;
    size_t i = 0;
    while (i <= len && n < kMaxFields) {
      f[n++] = store + w;
      if (i < len && s[i] == '"') {
        ++i;
        while (i < len) {
          if (s[i] == '"') {
            if (i + 1 < len && s[i + 1] == '"') {
              if (w < sizeof(store) - 1) store[w++] = '"';
              i += 2;
              continue;
            }
            ++i;
            break;
          }
          if (w < sizeof(store) - 1) store[w++] = s[i];
          ++i;
        }
        while (i < len && s[i] != ',') ++i;
      } else {
        while (i < len && s[i] != ',' && s[i] != '\r') {
          if (w < sizeof(store) - 1) store[w++] = s[i];
          ++i;
        }
        while (i < len && s[i] != ',') ++i;
      }
      if (w >= sizeof(store) - 1) return false;
      store[w++] = '\0';
      ++i;  // skip ','
    }
    return n > 0;
  }
  const char* get(int i) const { return i < n ? f[i] : ""; }
  float num(int i) const {
    const char* s = get(i);
    if (!*s) return NAN;
    return strtof(s, nullptr);
  }
};

class RangeClient {
 public:
  RangeClient() {
    client_.setInsecure();
    http_.setReuse(true);
    http_.setConnectTimeout(8000);
    http_.setTimeout(10000);
  }
  ~RangeClient() {
    http_.setReuse(false);
    http_.end();
  }

  /** GET bytes [start, end] into out. Returns false on error. */
  bool fetch(const char* url, uint32_t start, uint32_t end, String& out, uint32_t* total) {
    for (int attempt = 0; attempt < 2; ++attempt) {
      if (!http_.begin(client_, url)) continue;
      static const char* kHdr[] = {"Content-Range"};
      http_.collectHeaders(kHdr, 1);
      char range[48];
      snprintf(range, sizeof(range), "bytes=%lu-%lu", static_cast<unsigned long>(start),
               static_cast<unsigned long>(end));
      http_.addHeader("Range", range);
      const int code = http_.GET();
      if (code == 206 || code == 200) {
        out = http_.getString();
        if (total != nullptr) {
          const String cr = http_.header("Content-Range");  // bytes a-b/total
          const int slash = cr.lastIndexOf('/');
          if (slash > 0) *total = strtoul(cr.c_str() + slash + 1, nullptr, 10);
        }
        http_.end();
        step();
        return out.length() > 0;
      }
      Serial.printf("lookup: HTTP %d for %s\n", code, range);
      http_.end();
      client_.stop();
    }
    return false;
  }

 private:
  WiFiClientSecure client_;
  HTTPClient http_;
};

using RowFn = void (*)(const CsvRow& row, void* ctx);

/** Ident of the first complete line after offset 0 of `chunk` (skips partial line). */
bool firstFullLineKey(const String& chunk, int col, char* key, size_t key_len) {
  const int a = chunk.indexOf('\n');
  if (a < 0) return false;
  const int b = chunk.indexOf('\n', a + 1);
  if (b < 0) return false;
  CsvRow row;
  if (!row.parse(chunk.c_str() + a + 1, b - a - 1)) return false;
  strncpy(key, row.get(col), key_len - 1);
  key[key_len - 1] = '\0';
  return true;
}

/** Binary-search a CSV sorted by column `col`; call fn for each row == ident. */
int findRows(RangeClient& rc, const char* url, int col, const char* ident, RowFn fn, void* ctx) {
  String chunk;
  uint32_t total = 0;
  if (!rc.fetch(url, 0, kProbeBytes - 1, chunk, &total) || total == 0) return -1;

  uint32_t lo = 0;
  uint32_t hi = total;
  char key[16];
  while (hi - lo > kScanBytes) {
    const uint32_t mid = lo + (hi - lo) / 2;
    if (!rc.fetch(url, mid, mid + kProbeBytes - 1, chunk, nullptr)) return -1;
    if (!firstFullLineKey(chunk, col, key, sizeof(key))) {
      hi = mid;  // near EOF
      continue;
    }
    if (strcmp(key, ident) < 0) lo = mid; else hi = mid;
  }

  int found = 0;
  String carry;
  bool skip_first = true;
  uint32_t pos = lo;
  for (int guard = 0; guard < 16 && pos < total; ++guard) {
    uint32_t end = pos + kScanBytes - 1;
    if (end >= total) end = total - 1;
    if (!rc.fetch(url, pos, end, chunk, nullptr)) return found > 0 ? found : -1;
    pos = end + 1;
    carry += chunk;
    int start = 0;
    for (;;) {
      const int nl = carry.indexOf('\n', start);
      const bool last_line = nl < 0 && pos >= total;
      if (nl < 0 && !last_line) break;
      const int line_end = nl < 0 ? carry.length() : nl;
      if (skip_first) {
        skip_first = false;
      } else {
        CsvRow row;
        if (line_end > start && row.parse(carry.c_str() + start, line_end - start)) {
          const int cmp = strcmp(row.get(col), ident);
          if (cmp == 0) {
            fn(row, ctx);
            ++found;
          } else if (cmp > 0) {
            return found;
          }
        }
      }
      if (nl < 0) return found;
      start = nl + 1;
    }
    carry = carry.substring(start);
  }
  return found;
}

struct AirportRow {
  bool found = false;
  char type[24] = "";
};

void onAirportRow(const CsvRow& row, void* ctx) {
  auto* a = static_cast<AirportRow*>(ctx);
  a->found = true;
  strncpy(a->type, row.get(2), sizeof(a->type) - 1);
  strncpy(s_result.name, row.get(3), sizeof(s_result.name) - 1);
  s_result.lat = row.num(4);
  s_result.lon = row.num(5);
}

bool isHelipadIdent(const char* s) {
  if (s[0] != 'H') return false;
  return s[1] == '\0' || s[1] == '-' || s[1] == '_' || isdigit(static_cast<unsigned char>(s[1]));
}

/** Runway heading from its designator: "09L" -> 90, compass names "N", "NE"... */
float headingFromIdent(const char* s) {
  if (isdigit(static_cast<unsigned char>(s[0]))) {
    const int num = atoi(s);
    return (num >= 1 && num <= 36) ? num * 10.0f : NAN;
  }
  static const struct { const char* id; float hdg; } kCompass[] = {
      {"NNE", 22.5f}, {"ENE", 67.5f}, {"ESE", 112.5f}, {"SSE", 157.5f}, {"SSW", 202.5f},
      {"WSW", 247.5f}, {"WNW", 292.5f}, {"NNW", 337.5f}, {"NE", 45}, {"SE", 135},
      {"SW", 225}, {"NW", 315}, {"N", 360}, {"E", 90}, {"S", 180}, {"W", 270}};
  for (const auto& c : kCompass) {
    if (strcmp(s, c.id) == 0) return c.hdg;
  }
  return NAN;
}

/** "large_airport" -> "Large airport". */
void prettyType(const char* in, char* out, size_t n) {
  size_t i = 0;
  for (; in[i] && i + 1 < n; ++i) out[i] = in[i] == '_' ? ' ' : in[i];
  out[i] = '\0';
  if (out[0]) out[0] = static_cast<char>(toupper(static_cast<unsigned char>(out[0])));
}

/** Offset (lat, lon) by metres east/north. */
void offsetLatLon(float lat, float lon, float east_m, float north_m, float* olat, float* olon) {
  *olat = lat + north_m / kMPerDegLat;
  *olon = lon + east_m / (kMPerDegLat * cosf(lat * kDegToRad));
}

void onRunwayRow(const CsvRow& row, void*) {
  if (s_result.runway_count >= airports::kMaxRunways) return;
  if (atoi(row.get(7)) == 1) return;  // closed
  const char* le = row.get(8);
  const char* he = row.get(14);
  if (isHelipadIdent(le) && (isHelipadIdent(he) || !*he)) return;

  airports::Runway r = {};
  snprintf(r.name, sizeof(r.name), "%.3s%s%.3s", le, *he ? "/" : "", he);
  float le_lat = row.num(9), le_lon = row.num(10);
  float he_lat = row.num(15), he_lon = row.num(16);
  if (std::isfinite(le_lat) && std::isfinite(le_lon) && std::isfinite(he_lat) &&
      std::isfinite(he_lon)) {
    r.le_lat = le_lat; r.le_lon = le_lon; r.he_lat = he_lat; r.he_lon = he_lon;
  } else {
    // No surveyed ends: synthesise from airport reference point, heading, length.
    if (!std::isfinite(s_result.lat)) return;
    float hdg = row.num(12);
    if (!std::isfinite(hdg)) hdg = headingFromIdent(le);
    if (!std::isfinite(hdg)) return;
    float len_m = atof(row.get(3)) * 0.3048f;
    if (len_m < 100.0f) len_m = 600.0f;
    // Parallel runways: shift L/R by 150 m relative to the landing direction.
    float side = 0;
    const size_t ll = strlen(le);
    if (ll > 0 && le[ll - 1] == 'L') side = -150.0f;
    if (ll > 0 && le[ll - 1] == 'R') side = 150.0f;
    const float rad = hdg * kDegToRad;
    const float ex = sinf(rad), ny = cosf(rad);  // unit vector along landing dir
    const float px = cosf(rad), py = -sinf(rad); // unit vector to the right
    const float cx = px * side, cy = py * side;
    offsetLatLon(s_result.lat, s_result.lon, cx - ex * len_m / 2, cy - ny * len_m / 2, &r.le_lat, &r.le_lon);
    offsetLatLon(s_result.lat, s_result.lon, cx + ex * len_m / 2, cy + ny * len_m / 2, &r.he_lat, &r.he_lon);
  }
  s_result.runways[s_result.runway_count++] = r;
}

void runLookup() {
  s_steps = 0;
  s_progress = 2;
  memset(&s_result, 0, sizeof(s_result));
  strncpy(s_result.ident, s_ident, sizeof(s_result.ident) - 1);
  s_result.lat = NAN;
  s_result.lon = NAN;
  s_result.show_label = true;

  if (WiFi.status() != WL_CONNECTED) {
    setMsg("Device is offline - finish Wi-Fi setup first");
    s_state = State::kError;
    return;
  }
  setMsg("Waiting for network...");
  if (!adsb::netLock(20000)) {
    setMsg("Network busy, try again");
    s_state = State::kError;
    return;
  }
  bool ok = false;
  {
    RangeClient rc;
    setMsg("Searching airport database...");
    AirportRow ar;
    const int a = findRows(rc, kAirportsUrl, 1, s_ident, onAirportRow, &ar);
    if (a < 0) {
      setMsg("Download failed (check internet)");
    } else if (!ar.found) {
      setMsg("Airport not found - use the ICAO / local ident (e.g. EGLL, KJFK, 0A7)");
    } else {
      setMsg("Fetching runways...");
      const int r = findRows(rc, kRunwaysUrl, 2, s_ident, onRunwayRow, nullptr);
      if (r < 0) {
        setMsg("Runway download failed");
      } else {
        ok = true;
        char m[96], type[24];
        prettyType(ar.type, type, sizeof(type));
        snprintf(m, sizeof(m), "%s, %u runway%s", type, s_result.runway_count,
                 s_result.runway_count == 1 ? "" : "s");
        setMsg(m);
      }
    }
  }
  adsb::netUnlock();
  if (ok && !std::isfinite(s_result.lat)) {
    ok = false;
    setMsg("Airport has no coordinates");
  }
  s_progress = 100;
  s_state = ok ? State::kDone : State::kError;
  Serial.printf("lookup %s: %s (%s) heap %u\n", s_ident, ok ? "ok" : "fail", s_message,
                ESP.getFreeHeap());
}

void taskMain(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    runLookup();
  }
}

}  // namespace

void init() {
  if (s_task == nullptr) {
    xTaskCreatePinnedToCore(taskMain, "lookup", 14336, nullptr, 1, &s_task, 0);
  }
}

bool start(const char* ident) {
  if (s_state == State::kRunning) return false;
  if (!airports::normaliseIdent(ident, s_ident, sizeof(s_ident))) return false;
  s_state = State::kRunning;
  s_progress = 0;
  setMsg("Starting...");
  xTaskNotifyGive(s_task);
  return true;
}

State state() { return s_state; }
uint8_t progress() { return s_progress; }
const char* message() { return s_message; }
const char* ident() { return s_ident; }
const airports::Airport& result() { return s_result; }

void reset() {
  if (s_state != State::kRunning) s_state = State::kIdle;
}

}  // namespace services::lookup
