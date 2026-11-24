#if defined(NETBOT_WIFI_LAB)
#include "CameraSccb.h"

namespace lab { CameraSccb cameraSccb; }

extern "C" {
int __wrap_SCCB_Init(int sda, int scl) {
    return sda == 12 && scl == 11 && lab::cameraSccb.ready() ? 0 : -1;
}
int __wrap_SCCB_Use_Port(int port) {
    return port == M5.In_I2C.getPort() && lab::cameraSccb.ready() ? 0 : -1;
}
int __wrap_SCCB_Deinit() { return lab::cameraSccb.uncertain ? -1 : 0; }
int __wrap_SCCB_Install_Device(uint8_t address) {
    return address == 0x21 && lab::cameraSccb.ready() ? 0 : -1;
}
int __wrap_SCCB_Probe(uint8_t address) {
    if (address != 0x21) return -1;
    uint8_t id = 0;
    return lab::cameraSccb.transfer(address, 0, id, false) && id == 0x9B ? 0 : -1;
}
uint8_t __wrap_SCCB_Read(uint8_t address, uint8_t reg) {
    uint8_t value = 0;
    if (address != 0x21 || !lab::cameraSccb.transfer(address, reg, value, false))
        lab::cameraSccb.failed = true;
    return value;
}
int __wrap_SCCB_Write(uint8_t address, uint8_t reg, uint8_t value) {
    return address == 0x21 && lab::cameraSccb.transfer(address, reg, value, true) ? 0 : -1;
}
// CoreS3 GC0308 uses only 8-bit registers. Other sensor paths fail explicitly.
uint8_t __wrap_SCCB_Read16(uint8_t, uint16_t) { lab::cameraSccb.failed = true; return 0; }
int __wrap_SCCB_Write16(uint8_t, uint16_t, uint8_t) { lab::cameraSccb.failed = true; return -1; }
uint16_t __wrap_SCCB_Read_Addr16_Val16(uint8_t, uint16_t) { lab::cameraSccb.failed = true; return 0; }
int __wrap_SCCB_Write_Addr16_Val16(uint8_t, uint16_t, uint16_t) {
    lab::cameraSccb.failed = true;
    return -1;
}
}
#endif
