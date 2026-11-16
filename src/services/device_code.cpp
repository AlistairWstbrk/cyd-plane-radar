#include "services/device_code.h"

#include <Arduino.h>
#include <Preferences.h>

#if defined(ESP_PLATFORM) && !defined(PLANE_RADAR_SIM)
#include <bootloader_random.h>
#define PR_HW_ENTROPY 1
#endif

namespace services::device_code {
namespace {

constexpr char kNs[] = "prsec";
constexpr char kKey[] = "code";
// No 0/o, 1/l/i: easy to read off a small screen and type on a phone.
constexpr char kAlphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";

char s_code[kLength + 1] = {};

bool valid(const char* c) {
  for (int i = 0; i < kLength; ++i) {
    bool ok = false;
    for (const char* a = kAlphabet; *a; ++a) ok |= (*a == c[i]);
    if (!ok) return false;
  }
  return true;
}

}  // namespace

const char* get() {
  if (s_code[0]) return s_code;
  Preferences p;
  if (p.begin(kNs, false)) {
    char buf[kLength] = {};
    if (p.getBytes(kKey, buf, kLength) == kLength && valid(buf)) {
      memcpy(s_code, buf, kLength);
    } else {
      // Radio may be off this early: switch on the hardware entropy source.
#ifdef PR_HW_ENTROPY
      bootloader_random_enable();
#endif
      const size_t n = sizeof(kAlphabet) - 1;
      for (int i = 0; i < kLength; ++i) s_code[i] = kAlphabet[esp_random() % n];
#ifdef PR_HW_ENTROPY
      bootloader_random_disable();
#endif
      p.putBytes(kKey, s_code, kLength);
    }
    p.end();
  }
  if (!s_code[0]) strcpy(s_code, "planerad");  // NVS unavailable: still a valid WPA2 key
  s_code[kLength] = '\0';
  return s_code;
}

}  // namespace services::device_code
