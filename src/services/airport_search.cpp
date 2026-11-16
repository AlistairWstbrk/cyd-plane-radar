#include "services/airport_search.h"

#include <cctype>
#include <cmath>
#include <cstring>

namespace services::search {
namespace {

using data::airport_index::kCount;
using data::airport_index::kEntries;

constexpr int kMaxTokens = 4;
constexpr int kTokLen = 16;

inline char up(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; }
inline bool alnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

/** Case-insensitive prefix compare of `tok` (upper case) at `s`. */
bool prefixAt(const char* s, const char* tok) {
  while (*tok) {
    if (up(*s) != *tok) return false;
    ++s;
    ++tok;
  }
  return true;
}

/** 2 = a word in `s` starts with tok (3 when it is the first word), 1 = substring, 0 = none. */
int matchText(const char* s, const char* tok, size_t tl) {
  int best = 0;
  for (size_t i = 0; s[i]; ++i) {
    if (up(s[i]) != tok[0]) continue;
    if (!prefixAt(s + i, tok)) continue;
    const bool word_start = i == 0 || !alnum(s[i - 1]);
    if (word_start) return i == 0 ? 3 : 2;
    if (tl >= 3) best = 1;
  }
  return best;
}

int tokenScore(const Entry& e, const char* tok) {
  const size_t tl = strlen(tok);
  if (!strcmp(e.ident, tok)) return 1000;
  if (e.iata[0] && !strcmp(e.iata, tok)) return 950;
  int best = 0;
  if (tl >= 2 && !strncmp(e.ident, tok, tl)) best = 420;
  const int mn = matchText(name(e), tok, tl);
  if (mn == 3) best = best > 360 ? best : 360;
  else if (mn == 2) best = best > 300 ? best : 300;
  const int mc = matchText(city(e), tok, tl);
  if (mc >= 2) best = best > 280 ? best : 280;
  if (!best) {
    // Region suffix ("FL" in "US-FL") or country ("GB").
    const char* r = region(e);
    const char* dash = strchr(r, '-');
    if (!strcmp(r, tok) || (dash && !strcmp(dash + 1, tok)) || (tl == 2 && !strncmp(r, tok, 2)))
      best = 60;
  }
  if (!best && (mn == 1 || mc == 1)) best = 100;
  return best;
}

int sizeBonus(const Entry& e) {
  int b = (e.kind & 3) == 2 ? 40 : (e.kind & 3) == 1 ? 15 : 0;
  if (e.kind & 4) b += 20;
  return b;
}

/** Insert into `out` keeping it sorted (score desc, distance asc). */
void insertHit(Hit* out, int* n, int max, const Hit& h) {
  int pos = *n;
  while (pos > 0) {
    const Hit& p = out[pos - 1];
    const bool before = h.score > p.score || (h.score == p.score && h.dist_km < p.dist_km);
    if (!before) break;
    --pos;
  }
  if (pos >= max) return;
  const int last = *n < max ? *n : max - 1;
  for (int i = last; i > pos; --i) out[i] = out[i - 1];
  out[pos] = h;
  if (*n < max) ++*n;
}

}  // namespace

const Entry& entry(uint16_t idx) { return kEntries[idx < kCount ? idx : 0]; }
size_t count() { return kCount; }
const char* city(const Entry& e) { return e.text + strlen(e.text) + 1; }
const char* region(const Entry& e) {
  const char* c = city(e);
  return c + strlen(c) + 1;
}
float lat(const Entry& e) { return e.lat_e5 * 1e-5f; }
float lon(const Entry& e) { return e.lon_e5 * 1e-5f; }

float distanceKm(float lat0, float lon0, float lat1, float lon1) {
  constexpr float kDeg = 0.01745329252f;
  float dlon = lon1 - lon0;
  if (dlon > 180) dlon -= 360;
  if (dlon < -180) dlon += 360;
  const float x = dlon * kDeg * cosf((lat0 + lat1) * 0.5f * kDeg);
  const float y = (lat1 - lat0) * kDeg;
  return 6371.0f * sqrtf(x * x + y * y);
}

bool nearest(float lat0, float lon0, uint16_t* idx, float* km) {
  Hit h;
  int total = 0;
  if (run("", lat0, lon0, &h, 1, &total) < 1) return false;
  if (idx) *idx = h.idx;
  if (km) *km = h.dist_km;
  return true;
}

bool findIdent(const char* ident, uint16_t* idx) {
  int lo = 0, hi = static_cast<int>(kCount) - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    const int c = strcmp(kEntries[mid].ident, ident);
    if (c == 0) {
      if (idx) *idx = static_cast<uint16_t>(mid);
      return true;
    }
    if (c < 0) lo = mid + 1; else hi = mid - 1;
  }
  return false;
}

int run(const char* query, float lat0, float lon0, Hit* out, int max, int* total) {
  char toks[kMaxTokens][kTokLen];
  int nt = 0;
  for (const char* p = query ? query : ""; *p && nt < kMaxTokens;) {
    while (*p && !alnum(*p) && *p != '-') ++p;
    int l = 0;
    while (*p && (alnum(*p) || *p == '-' || *p == '.' || *p == '\'')) {
      if (l < kTokLen - 1 && *p != '.' && *p != '\'') toks[nt][l++] = up(*p);
      ++p;
    }
    if (l) toks[nt++][l] = '\0';
  }

  int n = 0, matched = 0;
  for (size_t i = 0; i < kCount; ++i) {
    const Entry& e = kEntries[i];
    int score = 0;
    bool ok = true;
    for (int t = 0; t < nt && ok; ++t) {
      const int s = tokenScore(e, toks[t]);
      if (s == 0) ok = false;
      score += s;
    }
    if (!ok) continue;
    ++matched;
    if (nt) score += sizeBonus(e);
    const float d = distanceKm(lat0, lon0, lat(e), lon(e));
    // Nearby list: distance only. Search: score first, then distance.
    Hit h{static_cast<uint16_t>(i), static_cast<int16_t>(score > 32000 ? 32000 : score), d};
    if (nt == 0) h.score = 0;
    insertHit(out, &n, max, h);
  }
  if (total) *total = matched;
  return n;
}

}  // namespace services::search
