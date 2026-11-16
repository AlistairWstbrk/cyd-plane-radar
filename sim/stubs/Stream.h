#pragma once
#include "Print.h"
class Stream : public Print {
 public:
  virtual int available() { return 0; }
  virtual int read() { return -1; }
  virtual int peek() { return -1; }
  size_t write(uint8_t) override { return 1; }
  size_t readBytes(char*, size_t) { return 0; }
  size_t readBytes(uint8_t*, size_t) { return 0; }
  void setTimeout(unsigned long) {}
};
