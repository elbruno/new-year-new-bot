// Goal: Instrument BSP yaw writes in the Wi-Fi firmware build.
// Responsibilities: Connect the wrapped servo write to diagnostic tracing.
// How it works:
// 1. The link-time wrapper observes an SCS servo write.
// 2. It calls the original write and records timing and acknowledgement.
// 3. The trace helper retains the result for diagnostic summaries.
#ifdef NETBOT_WIFI_LAB
#include <esp_timer.h>
#include "BspYawWriteTrace.h"

class SCSCL;
lab::BspYawWriteTrace bspYawWriteTrace;

// Wrap the pinned SCSCL::WritePos ABI without modifying the dependency.
extern "C" int __real__ZN5SCSCL8WritePosEhttt(SCSCL*, uint8_t, uint16_t, uint16_t, uint16_t);
extern "C" int __wrap__ZN5SCSCL8WritePosEhttt(SCSCL* bus, uint8_t id, uint16_t raw,
                                             uint16_t time, uint16_t speed) {
    if (id != 1 || !bspYawWriteTrace.active())
        return __real__ZN5SCSCL8WritePosEhttt(bus, id, raw, time, speed);

    const uint32_t before = esp_timer_get_time();
    const int result = __real__ZN5SCSCL8WritePosEhttt(bus, id, raw, time, speed);
    const uint32_t after = esp_timer_get_time();
    bspYawWriteTrace.record(before, after - before, raw, result == 1);
    return result;
}
#endif
