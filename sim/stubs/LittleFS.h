#pragma once
// LittleFS backed by a host directory (sim_fs/).
#include <cstdio>
#include <string>

#include "Arduino.h"

class File {
 public:
  File() {}
  explicit File(FILE* f) : f_(f) {}
  explicit operator bool() const { return f_ != nullptr; }
  int read() { return f_ ? fgetc(f_) : -1; }
  size_t readBytes(char* b, size_t n) { return f_ ? fread(b, 1, n, f_) : 0; }
  size_t write(uint8_t c) { return f_ && fputc(c, f_) != EOF ? 1 : 0; }
  size_t write(const uint8_t* b, size_t n) { return f_ ? fwrite(b, 1, n, f_) : 0; }
  void close() { if (f_) fclose(f_); f_ = nullptr; }

 private:
  FILE* f_ = nullptr;
};

class LittleFSSim {
 public:
  bool begin(bool = false) { return true; }
  std::string path(const char* p) { return std::string("sim_fs") + p; }
  bool exists(const char* p) {
    FILE* f = fopen(path(p).c_str(), "rb");
    if (f) fclose(f);
    return f != nullptr;
  }
  File open(const char* p, const char* mode) {
    return File(fopen(path(p).c_str(), mode[0] == 'w' ? "wb" : "rb"));
  }
};
extern LittleFSSim LittleFS;
