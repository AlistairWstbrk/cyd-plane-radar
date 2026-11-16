// Airport search: on-screen keyboard with live results from the built-in
// index (name, city, region, ICAO or IATA). Tapping a result looks up its
// runways online (services::lookup) and offers SAVE / HOME.
#include <Arduino.h>

#include <cstdio>
#include <cstring>

#include "services/airport_lookup.h"
#include "services/airport_search.h"
#include "services/settings.h"
#include "ui/app.h"
#include "ui/theme.h"

namespace ui {
namespace {

namespace srch = services::search;

char s_q[24] = "";
bool s_typing = true;
bool s_stale = true;
srch::Hit s_hits[srch::kMaxHits];
int s_n = 0;
int s_total = 0;
bool s_online_row = false;  // "look up <query> online" row after the hits
int s_first = 0;
int s_drag = 0;

constexpr int kTop = 31;
constexpr int kRowH = 30;
constexpr int kTypingRows = 3;
constexpr int kListRows = 7;
constexpr int kKeyTop = 124, kKeyH = 27, kKeyPitch = 29, kKeyW = 30;
const char* const kRows[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
constexpr Button kField{40, 3, 228, 24};
constexpr Button kDel{289, kKeyTop + 2 * kKeyPitch, 30, kKeyH};
constexpr Button kSpace{225, kKeyTop + 3 * kKeyPitch, 44, kKeyH};
constexpr Button kDone{271, kKeyTop + 3 * kKeyPitch, 48, kKeyH};

Button keyRect(int row, int col) {
  return Button{static_cast<int16_t>(1 + col * (kKeyW + 2)),
                static_cast<int16_t>(kKeyTop + row * kKeyPitch), kKeyW, kKeyH};
}

bool looksLikeIdent(const char* q) {
  const size_t n = strlen(q);
  if (n < 3 || n > 7) return false;
  bool digit = false;
  for (size_t i = 0; i < n; ++i) {
    const char c = q[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return false;
    if (c < 'A') digit = true;
  }
  // ICAO-style 4 letters, or anything with digits ("00AK", "FD24", "US-0035").
  return n == 4 || digit;
}

void recompute() {
  const auto& cfg = settings::get();
  s_n = srch::run(s_q, cfg.lat, cfg.lon, s_hits, srch::kMaxHits, &s_total);
  s_online_row = false;
  if (looksLikeIdent(s_q)) {
    s_online_row = true;
    for (int i = 0; i < s_n; ++i)
      if (!strcmp(srch::entry(s_hits[i].idx).ident, s_q)) s_online_row = false;
  }
  s_first = 0;
  s_stale = false;
}

int itemCount() { return s_n + (s_online_row ? 1 : 0); }
int visibleRows() { return s_typing ? kTypingRows : kListRows; }

/** Copy `s` into `out`, shortened with ".." to fit `maxw` pixels. */
void fit(Canvas& c, const char* s, char* out, size_t n, int maxw, Font f) {
  snprintf(out, n, "%s", s);
  if (c.textWidth(out, f) <= maxw) return;
  size_t len = strlen(out);
  while (len > 1) {
    out[--len] = '\0';
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s..", out);
    if (c.textWidth(tmp, f) <= maxw) {
      snprintf(out, n, "%s", tmp);
      return;
    }
  }
}

void drawRow(Canvas& c, int r, int item) {
  const int y = kTop + r * kRowH;
  if (!c.rows(y, y + kRowH)) return;
  if (r % 2) c.fillRect(0, y, 314, kRowH, blend(pal.bg, pal.panel_bg, 160));
  char buf[64], line[64];
  if (s_online_row && item == s_n) {
    c.text(s_q, 6, y + 4, pal.custom_runway, Font::S);
    c.text("not in list", 6, y + 18, pal.text_dim, Font::XS);
    c.text("Look up online", 72, y + 4, pal.text, Font::XS);
    c.text("small fields, strips, any OurAirports ident", 72, y + 17, pal.text_dim, Font::XS);
    return;
  }
  const auto& h = s_hits[item];
  const auto& e = srch::entry(h.idx);
  c.text(e.ident, 6, y + 4, pal.accent, Font::S);
  if (e.iata[0]) c.text(e.iata, 6, y + 18, pal.text_dim, Font::XS);
  settings::formatDistance(buf, sizeof(buf), h.dist_km);
  const int dw = c.textWidth(buf, Font::XS);
  c.text(buf, 312, y + 4, pal.text_dim, Font::XS, textdatum_t::top_right);
  fit(c, srch::name(e), line, sizeof(line), 312 - 72 - dw - 6, Font::XS);
  c.text(line, 72, y + 4, pal.text, Font::XS);
  const char* city = srch::city(e);
  snprintf(buf, sizeof(buf), "%s%s%s", city, *city ? ", " : "", srch::region(e));
  fit(c, buf, line, sizeof(line), 240, Font::XS);
  c.text(line, 72, y + 17, pal.text_dim, Font::XS);
}

void drawKeyboard(Canvas& c) {
  if (!c.rows(kKeyTop - 2, 240)) return;
  c.fillRect(0, kKeyTop - 3, 320, 240 - kKeyTop + 3, pal.panel_bg);
  for (int r = 0; r < 4; ++r) {
    for (int k = 0; kRows[r][k]; ++k) {
      const char lbl[2] = {kRows[r][k], 0};
      drawButton(c, keyRect(r, k), lbl, BtnStyle::Normal, Font::S);
    }
  }
  drawButton(c, kDel, "DEL", BtnStyle::Ghost, Font::XS);
  drawButton(c, kSpace, "SPACE", BtnStyle::Ghost, Font::XS);
  drawButton(c, kDone, "LIST", BtnStyle::Primary, Font::XS);
}

void selectItem(int item) {
  if (s_online_row && item == s_n) {
    if (services::lookup::start(s_q)) {
      lookupSetOrigin(ScreenId::Search, -1);
      navigate(ScreenId::Lookup);
    }
    return;
  }
  const auto& h = s_hits[item];
  if (services::lookup::start(srch::entry(h.idx).ident)) {
    lookupSetOrigin(ScreenId::Search, h.idx);
    navigate(ScreenId::Lookup);
  }
}

void edited() {
  s_stale = true;
  requestRedraw();
}

}  // namespace

void searchEnter(bool clear) {
  if (clear) s_q[0] = '\0';
  s_typing = true;
  s_stale = true;
  navigate(ScreenId::Search);
}

void searchSetQuery(const char* q, bool typing) {
  size_t n = 0;
  for (; q && q[n] && n < sizeof(s_q) - 1; ++n) s_q[n] = (q[n] >= 'a' && q[n] <= 'z') ? q[n] - 32 : q[n];
  s_q[n] = '\0';
  s_typing = typing;
  s_stale = true;
  navigate(ScreenId::Search);
}

void searchDraw(Canvas& c, void*) {
  if (s_stale) recompute();
  c.fill(pal.bg);
  // Header: back arrow, query field, match count.
  if (c.rows(0, 31)) {
    c.fillRect(0, 0, 320, 30, pal.panel_bg);
    c.hline(0, 30, 320, pal.panel_line);
    c.wideLine(22, 8, 14, 15, 1.4f, pal.accent);
    c.wideLine(14, 15, 22, 22, 1.4f, pal.accent);
    c.fillRoundRect(kField.x, kField.y, kField.w, kField.h, 5, pal.bg);
    c.roundRect(kField.x, kField.y, kField.w, kField.h, 5, s_typing ? pal.accent : pal.panel_line);
    if (s_q[0]) {
      c.text(s_q, kField.x + 8, 15, pal.text, Font::M, textdatum_t::middle_left);
      if (s_typing) {
        const int cx = kField.x + 9 + c.textWidth(s_q, Font::M);
        c.fillRect(cx, 8, 2, 15, pal.accent);
      }
    } else {
      if (s_typing) c.fillRect(kField.x + 8, 8, 2, 15, pal.accent);
      c.text("Name, city, ICAO or IATA", kField.x + 14, 15, pal.text_dim, Font::XS,
             textdatum_t::middle_left);
    }
    char cnt[16];
    if (!s_q[0]) snprintf(cnt, sizeof(cnt), "nearby");
    else snprintf(cnt, sizeof(cnt), "%d", s_total);
    c.text(cnt, 314, 15, pal.text_dim, Font::XS, textdatum_t::middle_right);
  }

  const int n = itemCount();
  const int vis = visibleRows();
  if (s_first > n - vis) s_first = n - vis;
  if (s_first < 0) s_first = 0;
  if (n == 0) {
    c.text("No matching airports", 160, kTop + 30, pal.text_dim, Font::S, textdatum_t::middle_center);
  }
  for (int r = 0; r < vis && s_first + r < n; ++r) drawRow(c, r, s_first + r);
  if (!s_typing) {
    // scrollbar
    if (n > vis) {
      const int h = kRowH * vis;
      const int bh = h * vis / n < 12 ? 12 : h * vis / n;
      const int by = kTop + (h - bh) * s_first / (n - vis);
      c.fillRect(316, kTop, 3, h, pal.panel_bg);
      c.fillRect(316, by, 3, bh, pal.text_dim);
    }
    return;
  }
  drawKeyboard(c);
}

void searchTouch(const touch::Event& e) {
  using touch::Type;
  if (e.type == Type::Down) s_drag = 0;
  if (e.type == Type::Drag && e.y > kTop && (!s_typing || e.y < kKeyTop)) {
    s_drag += e.dy;
    while (s_drag <= -kRowH / 2) { ++s_first; s_drag += kRowH / 2; requestRedraw(); }
    while (s_drag >= kRowH / 2) { --s_first; s_drag -= kRowH / 2; requestRedraw(); }
    return;
  }
  if (e.type == Type::LongPress && s_typing && kDel.hit(e.x, e.y)) {
    s_q[0] = '\0';
    edited();
    return;
  }
  if (e.type != Type::Tap) return;
  if (e.y < 30 && e.x < 40) {
    services::lookup::reset();
    navigate(ScreenId::Airports);
    return;
  }
  if (e.y < 30) {
    s_typing = true;
    requestRedraw();
    return;
  }
  const int vis = visibleRows();
  if (e.y >= kTop && e.y < kTop + vis * kRowH && (!s_typing || e.y < kKeyTop - 3)) {
    const int item = s_first + (e.y - kTop) / kRowH;
    if (item < itemCount()) selectItem(item);
    return;
  }
  if (!s_typing) return;
  const size_t len = strlen(s_q);
  if (kDel.hit(e.x, e.y)) {
    if (len) s_q[len - 1] = '\0';
    edited();
    return;
  }
  if (kSpace.hit(e.x, e.y)) {
    if (len && s_q[len - 1] != ' ' && len < sizeof(s_q) - 1) { s_q[len] = ' '; s_q[len + 1] = '\0'; }
    edited();
    return;
  }
  if (kDone.hit(e.x, e.y)) {
    s_typing = false;
    requestRedraw();
    return;
  }
  for (int r = 0; r < 4; ++r) {
    for (int k = 0; kRows[r][k]; ++k) {
      if (keyRect(r, k).hit(e.x, e.y)) {
        if (len < sizeof(s_q) - 1) { s_q[len] = kRows[r][k]; s_q[len + 1] = '\0'; }
        edited();
        return;
      }
    }
  }
}

}  // namespace ui
