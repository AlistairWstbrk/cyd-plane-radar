// Host runtime for the simulator: clock, Serial, NVS/FS globals, FreeRTOS on
// std::thread, hardware (hw::) shims and the embedded font.
#include <Arduino.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <SPI.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <mutex>
#include <random>
#include <thread>

#include "font_data.h"
#include "hardware/display.h"
#include "hardware/display_font.h"

HardwareSerialSim Serial;
EspClassSim ESP;
WiFiSim WiFi;
LittleFSSim LittleFS;
SPIClass SPI;
int g_sim_http_requests = 0;
bool g_sim_http_offline = false;

static std::atomic<unsigned long> g_ms{1000};
unsigned long millis() { return g_ms.load(); }
unsigned long micros() { return g_ms.load() * 1000UL; }
void delay(unsigned long ms) { g_ms += ms; }
void delayMicroseconds(unsigned int) {}
void sim_advance_ms(unsigned long ms) { g_ms += ms; }

uint32_t esp_random() {
  static std::mt19937 rng(12345);  // deterministic runs
  return rng();
}

bool getLocalTime(struct tm* info, uint32_t) {
  memset(info, 0, sizeof(*info));
  info->tm_year = 126;
  info->tm_mon = 10;
  info->tm_mday = 16;
  info->tm_hour = 14;
  info->tm_min = 32 + static_cast<int>((g_ms.load() / 60000) % 28);
  return true;
}

void wifiResetCredentialsAndReboot() { std::printf("[sim] wifi reset requested\n"); }
void applyTimezone() {}

// ---- FreeRTOS -------------------------------------------------------------
struct SimSem {
  std::timed_mutex m;
};
SemaphoreHandle_t xSemaphoreCreateMutex() { return new SimSem(); }
BaseType_t xSemaphoreTake(SemaphoreHandle_t h, TickType_t t) {
  auto* s = static_cast<SimSem*>(h);
  if (t == portMAX_DELAY) {
    s->m.lock();
    return pdTRUE;
  }
  return s->m.try_lock_for(std::chrono::milliseconds(t)) ? pdTRUE : pdFALSE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t h) {
  static_cast<SimSem*>(h)->m.unlock();
  return pdTRUE;
}

struct SimTask {
  std::mutex m;
  std::condition_variable cv;
  uint32_t notify = 0;
};
static thread_local SimTask* t_self = nullptr;

BaseType_t xTaskCreatePinnedToCore(void (*fn)(void*), const char*, uint32_t, void* arg, int,
                                   TaskHandle_t* out, int) {
  auto* task = new SimTask();
  if (out) *out = task;
  std::thread([fn, arg, task] {
    t_self = task;
    fn(arg);
  }).detach();
  return pdTRUE;
}
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t) {
  SimTask* t = t_self;
  std::unique_lock<std::mutex> lk(t->m);
  t->cv.wait(lk, [t] { return t->notify > 0; });
  const uint32_t v = t->notify;
  t->notify = clear ? 0 : t->notify - 1;
  return v;
}
void xTaskNotifyGive(TaskHandle_t h) {
  auto* t = static_cast<SimTask*>(h);
  {
    std::lock_guard<std::mutex> lk(t->m);
    ++t->notify;
  }
  t->cv.notify_one();
}
void vTaskDelay(TickType_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// ---- Hardware ---------------------------------------------------------------
LGFX tft;

namespace hw {
void init() { tft.init(); }
void applyOrientation(bool, bool) {}
void setBacklightPercent(uint8_t pct) { tft.setBrightness(pct); }
void led(uint8_t, uint8_t, uint8_t) {}
}  // namespace hw

// ---- Font -------------------------------------------------------------------
// Font objects live in src/hardware/display_font.cpp; only the data comes from here.
const uint8_t* uiFontBlob(int index, size_t* len) {
  if (index < 0 || index >= 5) { if (len) *len = 0; return nullptr; }
  if (len) *len = kSimFontLens[index];
  return kSimFonts[index];
}
