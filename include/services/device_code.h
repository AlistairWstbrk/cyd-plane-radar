#pragma once

/**
 * Per-device secret, created at random on first boot and kept in NVS.
 * It is the setup Wi-Fi password and the web login password (user "admin").
 * Shown on the CYD's setup screen and Settings > Device info.
 */
namespace services::device_code {

constexpr char kLoginUser[] = "admin";
constexpr int kLength = 8;

/** The 8-character code (lower case letters and digits, no look-alikes). */
const char* get();

}  // namespace services::device_code
