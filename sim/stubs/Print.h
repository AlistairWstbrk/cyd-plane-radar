#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* b, size_t n) {
    size_t r = 0;
    while (n--) r += write(*b++);
    return r;
  }
  size_t write(const char* s) { return s ? write(reinterpret_cast<const uint8_t*>(s), strlen(s)) : 0; }
  size_t write(const char* b, size_t n) { return write(reinterpret_cast<const uint8_t*>(b), n); }
  size_t print(const char* s) { return write(s); }
};
