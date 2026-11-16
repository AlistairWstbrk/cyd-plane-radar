#include "ui/status_screens.h"

#include <Arduino.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "services/device_code.h"
#include "hardware/display.h"
#include "ui/canvas.h"
#include "ui/theme.h"

namespace ui::status {
namespace {

char s_ssid[33] = "";
unsigned long s_conn_start = 0;
int s_conn_frame = 0;

struct ConnCtx {
  int frame;
};

void drawLogo(Canvas& c, int cx, int cy, int r) {
  for (int i = 1; i <= 3; ++i) c.ring(cx, cy, r * i / 3 - 1, r * i / 3, pal.grid);
  c.wideLine(cx - r, cy, cx + r, cy, 0.8f, pal.grid);
  c.wideLine(cx, cy - r, cx, cy + r, 0.8f, pal.grid);
  // Small aircraft symbol
  c.triangle(cx + r / 3, cy - r / 2 - 6, cx + r / 3 - 5, cy - r / 2 + 4, cx + r / 3 + 5, cy - r / 2 + 4, pal.aircraft);
  c.smoothCircle(cx, cy, 2, pal.center);
}

void drawConnecting(Canvas& c, void* p) {
  const int frame = static_cast<ConnCtx*>(p)->frame;
  c.fill(pal.bg);
  const int cx = 160, cy = 100, r = 62;
  for (int i = 1; i <= 3; ++i) c.ring(cx, cy, r * i / 3 - 1, r * i / 3, pal.grid_dim);
  const float ang = frame * 9.0f;
  for (int i = 8; i >= 1; --i) {
    c.arc(cx, cy, 0, r, ang - 90 - i * 5, ang - 90 - (i - 1) * 5,
          blend(pal.bg, pal.grid, static_cast<uint8_t>(200 - i * 22)));
  }
  c.smoothCircle(cx, cy, 3, pal.center);
  c.text("Connecting to Wi-Fi", 160, 184, pal.text, Font::M, textdatum_t::middle_center);
  c.text(s_ssid, 160, 206, pal.accent, Font::S, textdatum_t::middle_center);
  char t[24];
  snprintf(t, sizeof(t), "%lus", (millis() - s_conn_start) / 1000);
  c.text(t, 314, 232, pal.text_dim, Font::XS, textdatum_t::bottom_right);
  c.text("Hold BOOT 3 s to reset", 6, 232, pal.text_dim, Font::XS, textdatum_t::bottom_left);
}

void drawPortal(Canvas& c, void*) {
  const uint16_t yellow = rgb(255, 214, 0);
  const uint16_t ink = rgb(10, 10, 10);
  c.fill(yellow);
  c.text("PLANE RADAR SETUP", 160, 6, ink, Font::M, textdatum_t::top_center);
  // QR 1: join the setup AP (standard Wi-Fi QR payload, password included)
  const char* code = services::device_code::get();
  char wifi_qr[80];
  snprintf(wifi_qr, sizeof(wifi_qr), "WIFI:T:WPA;S:%s;P:%s;;", config::kPortalApName, code);
  char url_qr[40];
  snprintf(url_qr, sizeof(url_qr), "http://%s/", config::kPortalIp);
  c.fillRect(18, 30, 124, 124, rgb(255, 255, 255));
  c.qr(wifi_qr, 20, 32, 120);
  c.fillRect(178, 30, 124, 124, rgb(255, 255, 255));
  c.qr(url_qr, 180, 32, 120);
  c.text("1. JOIN WI-FI", 80, 158, ink, Font::S, textdatum_t::top_center);
  c.text("2. OPEN PAGE", 240, 158, ink, Font::S, textdatum_t::top_center);
  c.text(config::kPortalApName, 80, 175, ink, Font::XS, textdatum_t::top_center);
  char line[64];
  snprintf(line, sizeof(line), "Password  %s", code);
  c.text(line, 80, 190, ink, Font::S, textdatum_t::top_center);
  c.text(config::kPortalIp, 240, 175, ink, Font::XS, textdatum_t::top_center);
  c.text("(often opens by itself)", 240, 191, ink, Font::XS, textdatum_t::top_center);
  c.hline(10, 210, 300, ink);
  c.text("The password is also the web login (user admin)", 160, 216, ink, Font::XS,
         textdatum_t::top_center);
}

void drawMessage(const char* title, const char* l1, const char* l2, uint16_t col) {
  Canvas c = directCanvas();
  c.fill(pal.bg);
  c.text(title, 160, 90, col, Font::L, textdatum_t::middle_center);
  c.text(l1, 160, 130, pal.text, Font::S, textdatum_t::middle_center);
  c.text(l2, 160, 152, pal.text_dim, Font::XS, textdatum_t::middle_center);
}

}  // namespace

void firmwareUpdate(int state) {
  if (state == 0) drawMessage("UPDATING", "Installing new firmware", "Keep the power connected", pal.accent);
  else if (state > 0) drawMessage("UPDATED", "Restarting...", "", pal.ok);
  else drawMessage("UPDATE FAILED", "The old firmware is still installed", "See the web page for details", pal.alert);
}

void boot() {
  Canvas c = directCanvas();
  c.fill(pal.bg);
  drawLogo(c, 160, 92, 60);
  c.text("PLANE RADAR", 160, 172, pal.text, Font::L, textdatum_t::middle_center);
  char v[32];
  snprintf(v, sizeof(v), "CYD edition  v%s", config::kFirmwareVersion);
  c.text(v, 160, 198, pal.text_dim, Font::XS, textdatum_t::middle_center);
  c.text("live ADS-B from adsb.fi", 160, 214, pal.text_dim, Font::XS, textdatum_t::middle_center);
}

void connectingBegin(const char* ssid) {
  strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
  s_conn_start = millis();
  s_conn_frame = 0;
  connectingTick();
}

void connectingTick() {
  ConnCtx ctx{s_conn_frame++};
  renderFrame(drawConnecting, &ctx);
}

void portal() { renderFrame(drawPortal, nullptr); }

void connectFailed() {
  drawMessage("No connection", "Could not join Wi-Fi", "Retrying in the background. Hold BOOT 3 s to reset.",
              pal.alert);
}

void wifiReset() {
  drawMessage("Reset", "Wi-Fi and settings cleared", "Rebooting into setup...", pal.warn);
}

}  // namespace ui::status
