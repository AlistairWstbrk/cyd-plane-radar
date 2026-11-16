#pragma once

#include <cstdint>

#include <driver/gpio.h>

namespace config {

constexpr char kFirmwareVersion[] = "1.1.0-cyd";

/** Full user guide (PDF). Shown as a QR code under Settings > User guide. */
constexpr char kGuideUrl[] = "https://github.com/AlistairWstbrk/cyd-plane-radar/blob/main/docs/guide.pdf";

// --- Wi-Fi portal (same names as the original Plane Radar) ---
constexpr char kPortalApName[] = "PlaneRadar-Setup";
constexpr char kPortalIp[] = "192.168.4.1";
/** mDNS host (no ".local" suffix); browser: http://plane-radar.local */
constexpr char kPortalHostname[] = "plane-radar";
constexpr char kPortalHostUrl[] = "plane-radar.local";

constexpr unsigned long kWifiConnectAttemptMs = 15000;
constexpr uint8_t kWifiConnectAttempts = 3;
constexpr unsigned long kWifiPortalTimeoutSec = 0;  // 0 = no timeout
constexpr unsigned long kWifiConnectingFrameMs = 40;
constexpr unsigned long kWifiDownGraceMs = 4000;
constexpr unsigned long kWifiReconnectIntervalMs = 15000;

// --- BOOT button (GPIO 0 on the CYD, active LOW) ---
constexpr gpio_num_t kBootPin = GPIO_NUM_0;
constexpr unsigned long kBootResetHoldMs = 3000UL;
constexpr unsigned long kBootTapMinMs = 40UL;

// --- Display: ESP32-2432S028R, 2.8" 320x240, HSPI ---
constexpr int kPinTftSclk = 14;
constexpr int kPinTftMosi = 13;
constexpr int kPinTftMiso = 12;
constexpr int kPinTftCs = 15;
constexpr int kPinTftDc = 2;
constexpr int kPinTftRst = -1;
constexpr int kPinTftBacklight = 21;
constexpr uint32_t kTftSpiWriteHz = 40000000;
constexpr uint32_t kTftSpiReadHz = 16000000;

constexpr int kScreenW = 320;
constexpr int kScreenH = 240;

// --- Touch: XPT2046 on its own SPI bus (VSPI) ---
constexpr int kPinTouchSclk = 25;
constexpr int kPinTouchMosi = 32;
constexpr int kPinTouchMiso = 39;
constexpr int kPinTouchCs = 33;
constexpr int kPinTouchIrq = 36;

// --- On-board extras ---
constexpr int kPinLedR = 4;   // RGB LED, active LOW
constexpr int kPinLedG = 16;
constexpr int kPinLedB = 17;

// --- Radar center defaults (overridden via portal / settings) ---
constexpr double kDefaultRadarLat = 52.3676;
constexpr double kDefaultRadarLon = 4.9041;

/** Default timezone (POSIX TZ). Changed in the web settings page. */
constexpr char kDefaultTz[] = "UTC0";

// --- ADS-B (adsb.fi open data; public limit 1 req/s) ---
constexpr unsigned long kAdsbMinFetchIntervalMs = 3000;
/** Redraw cadence of the radar; aircraft are dead-reckoned between fetches. */
constexpr unsigned long kRadarFrameMs = 200;
constexpr unsigned long kRadarFrameSweepMs = 80;
/** Feed considered stale after this long without a successful fetch. */
constexpr unsigned long kFeedStaleMs = 30000;

}  // namespace config
