#pragma once
// Host-simulator stand-in for the Arduino core (only what the firmware uses).
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "WString.h"

#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif

#define IRAM_ATTR
#define PROGMEM
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define CHANGE 3
#define ADC_0db 0

#define pgm_read_byte(a) (*reinterpret_cast<const uint8_t*>(a))
#define pgm_read_word(a) (*reinterpret_cast<const uint16_t*>(a))
#define pgm_read_dword(a) (*reinterpret_cast<const uint32_t*>(a))
#define pgm_read_ptr(a) (*reinterpret_cast<void* const*>(a))
#define memcpy_P memcpy
#define strlen_P strlen
#define strncpy_P strncpy
#define FPSTR(p) (p)
#define F(s) (s)

typedef uint8_t byte;
using std::isfinite;

unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void sim_advance_ms(unsigned long ms);   // simulated clock

inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return HIGH; }
inline int analogRead(int) { return 200; }
inline void analogSetPinAttenuation(int, int) {}
uint32_t esp_random();

struct tm;
bool getLocalTime(struct tm* info, uint32_t ms = 5000);
inline void configTzTime(const char*, const char*, const char* = nullptr, const char* = nullptr) {}

class HardwareSerialSim {
 public:
  void begin(unsigned long) {}
  void updateBaudRate(unsigned long) {}
  int printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
  }
  size_t print(const char* s) { return fputs(s, stdout) >= 0 ? strlen(s) : 0; }
  size_t println(const char* s = "") { std::printf("%s\n", s); return strlen(s) + 1; }
  size_t println(const String& s) { return println(s.c_str()); }
  size_t write(const uint8_t* b, size_t n) { return fwrite(b, 1, n, stdout); }
  size_t write(uint8_t c) { return fputc(c, stdout) >= 0 ? 1 : 0; }
  int available() { return 0; }
  int read() { return -1; }
  void flush() { fflush(stdout); }
};
extern HardwareSerialSim Serial;

class EspClassSim {
 public:
  uint32_t getFreeHeap() { return 180000; }
  uint32_t getMinFreeHeap() { return 150000; }
  uint32_t getMaxAllocHeap() { return 110000; }
  void restart() { std::printf("[sim] ESP.restart()\n"); }
};
extern EspClassSim ESP;

class IPAddress {
 public:
  IPAddress(int a = 0, int b = 0, int c = 0, int d = 0) : a_(a), b_(b), c_(c), d_(d) {}
  String toString() const {
    char s[20];
    snprintf(s, sizeof(s), "%d.%d.%d.%d", a_, b_, c_, d_);
    return String(s);
  }
  bool operator!=(const IPAddress& o) const { return a_ != o.a_ || b_ != o.b_ || c_ != o.c_ || d_ != o.d_; }

 private:
  int a_, b_, c_, d_;
};
