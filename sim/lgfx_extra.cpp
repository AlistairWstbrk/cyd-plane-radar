// Pulls in the LovyanGFX pieces the simulator links (Panel_Device base only;
// the full panel hub has GCC-only VLAs that MSVC rejects).
#define LGFX_V1_IMPLEMENTATION
#include <lgfx/v1/panel/Panel_Device.inl>

namespace lgfx {
inline namespace v1 {
void pinMode(int_fast16_t, pin_mode_t) {}
}  // namespace v1
}  // namespace lgfx
